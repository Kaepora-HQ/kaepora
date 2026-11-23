/**
 * The MCP server: the tools' handlers, how their answers reach a model, and the instructions every session gets. A
 * picture answers as an MCP image block, the rest as JSON text of at most `answerLimit` bytes: an answer that would be
 * longer is cut at whole entries and says how to read the rest. A run's record on disk is its answer, so the server
 * holds nothing for a caller that is gone. The same registrations serve stdio (`mcp`) and HTTP (`serve`).
 */
import {
	Array as Arr,
	Cause,
	Duration,
	Effect,
	Fiber,
	FileSystem,
	Formatter,
	Layer,
	Option,
	Path,
	Predicate,
	Record as Rec,
	Schedule,
	Schema,
} from "effect";
import { McpProtocol, McpSchema, McpServer } from "effect/ai";
import { ChildProcess, ChildProcessSpawner } from "effect/process";
import type { Outcome } from "../contract/record.ts";
import { RunRecord } from "../contract/record.ts";
import { StartState } from "../contract/state.ts";
import type { RunId } from "../contract/values.ts";
import { newRunId, Picture } from "../contract/values.ts";
import { GameData } from "../data/game-data.ts";
import { KeptScriptExists, NobodyThere, ScriptFailed } from "../errors.ts";
import {
	defaultPlaceWait,
	dialogEvery,
	dialogTimes,
	pressFrames,
	pressUntilEvery,
	pressUntilTimes,
} from "../game.ts";
import { Places } from "../host/places.ts";
import { Runs } from "../host/runs.ts";
import type { Person } from "../host/sessions.ts";
import { Sessions } from "../host/sessions.ts";
import { readMod } from "../mod.ts";
import { ran } from "../process.ts";
import { StartStates } from "../save/state.ts";
import { OwnTables, searchNames } from "../save/tables.ts";
import { inlineFolder, packageRoot, Scripts } from "../scripts/runner.ts";
import type { Ran } from "./tools.ts";
import { tools } from "./tools.ts";

export const serverName = "game-testing";

/** A client on 2026-07-28 names the revision on every request; an older one, such as Codex, opens a session with `initialize`. */
export const protocols = [
	McpProtocol.v2026_07_28,
	McpProtocol.v2025_11_25,
	McpProtocol.v2025_06_18,
] as const;

export const instructions =
	"These tools run Ocarina of Time, the Switch 2 remake, in the Lichii emulator on the game testing machine. Each game runs in a place of its own and starts from a save the tools build. A test takes four steps. Find each name you need with search_names. Write the start state, and check it with plan_state, which starts no game. Send the whole test as one run_script call: it starts the game, runs your body, and answers the outcome and the pictures. Read past runs with list_runs and get_run. To look around by hand instead, start_game, then press, press_until, complete_dialog, stick, read_values and screenshot with its session, and stop_game when you are done.";

/** What a person answers to a hand over: only a yes is done. */
const Done = Schema.Struct({
	done: Schema.Boolean.annotate({ title: "Done" }),
});

/** The most an answer's text holds: clients spill a longer result to a file the model may never read. */
const answerLimit = 20_000;

/** The most pictures one answer holds, each 1.5 to 2.4 MB at 720p. */
const picturesPerAnswer = 4;

/** The longest one `wait_run` call waits, inside the time a client waits for a tool. */
const waitLimit = Duration.seconds(45);

/** How often `wait_run` and a detached `run_script` read a run's record. */
const recordSpacing = Duration.millis(200);

const running: Outcome = { _tag: "running" };
const stoppedByCaller = "stop_game ended it";

const searchLimit = 40;
const runsLimit = 20;

const encoder = new TextEncoder();
const bytes = (value: unknown) =>
	encoder.encode(Formatter.formatJson(value)).length;

/** The bytes an answer keeps for its `more`, which says how to read what was cut. */
const moreRoom = 200;

/** How many leading entries of `items` fit beside the rest of an answer, which takes `others` bytes. */
const fitting = (items: readonly unknown[], others: number) => {
	let size = others;
	let count = 0;
	for (const item of items) {
		size += bytes(item) + 1;
		if (size > answerLimit) {
			break;
		}
		count += 1;
	}
	return count;
};

/** A result as an answer holds it: itself, or what to do when it takes more than half an answer. */
const resultOf = (result: unknown) => {
	if (result === undefined) {
		return {};
	}
	const size = bytes(result);
	return size <= answerLimit / 2
		? { result }
		: {
				more: `the body returned ${size} bytes, more than an answer holds; return less, such as the values a check read`,
			};
};

const image = (picture: Picture): McpSchema.ImageContent => ({
	type: "image",
	data: picture.png,
	mimeType: "image/png",
});

const text = (value: unknown): McpSchema.TextContent => ({
	type: "text",
	text: Formatter.formatJson(value),
});

/** Every text a value holds, at any depth, to find the pictures a script's result names. */
const textsIn = (value: unknown): readonly string[] => {
	if (typeof value === "string") {
		return [value];
	}
	if (Arr.isArray(value)) {
		return Arr.flatMap(value, textsIn);
	}
	return Predicate.isObject(value)
		? Arr.flatMap(
				Rec.values(value as Readonly<Record<string, unknown>>),
				textsIn
			)
		: [];
};

/**
 * The language service's rules a body sent inline is held to: the mistakes that leave a step unrun. The package's
 * style rules hold for a kept script, which the package's own check reads.
 */
const inlineRules = [
	"// @effect-diagnostics *:off",
	"// @effect-diagnostics floatingEffect:error",
	"// @effect-diagnostics missingStarInYieldEffectGen:error",
];

/**
 * A script that `run_script` writes around a body: its declaration, and the body as sent inside `Effect.gen`, from
 * line `bodyStarts`, so a compiler's line and column in the body are the body's own.
 */
const scriptAround = (
	declared: Readonly<Record<string, unknown>>,
	body: string,
	importFrom: string,
	rules: readonly string[] = []
) => {
	const head = [
		...rules,
		'import { Effect } from "effect";',
		`import { script } from "${importFrom}";`,
		"",
		"export default script({",
		...Arr.map(
			Rec.toEntries(declared),
			([key, value]) => `\t${key}: ${Formatter.formatJson(value)},`
		),
		"\trun: (game) =>",
		"\t\tEffect.gen(function* () {",
	];
	return {
		text: Arr.join([...head, body, "\t\t}),", "});", ""], "\n"),
		bodyStarts: head.length + 1,
	};
};

/** Where tsc names a line of a script written inline. */
const inlineLine =
	/\S*\.cache\/inline\/[0-9a-z-]+\.ts\((?<line>\d+),(?<column>\d+)\)/gu;

/** The compiler's lines with each place in the body named `body(line,column)`, as the body was sent. */
const inBody = (detail: string, bodyStarts: number) =>
	detail.replace(inlineLine, (place, line: string, column: string) => {
		const inside = Number(line) - bodyStarts + 1;
		return inside >= 1 ? `body(${inside},${column})` : place;
	});

/** A caller's user, asked through its MCP client. Only a yes is done; a no, a decline or a cancel finds nobody. */
export const personOf =
	(client: McpSchema.McpServerClient["Service"]): Person =>
	(say, within) => {
		const declined = Effect.fail(
			NobodyThere.make({ ask: say, reason: "declined" })
		);
		return McpServer.elicit({ message: say, schema: Done }).pipe(
			Effect.provideService(McpSchema.McpServerClient, client),
			Effect.flatMap(({ done }) => (done ? Effect.void : declined)),
			Effect.catchTag("ElicitationDeclined", () => declined),
			// elicit answers a cancel by interrupting itself.
			Effect.catchCause((cause) =>
				Cause.hasInterruptsOnly(cause) ? declined : Effect.failCause(cause)
			),
			Effect.timeoutOrElse({
				duration: within,
				orElse: () =>
					Effect.fail(NobodyThere.make({ ask: say, reason: "timed-out" })),
			})
		);
	};

const handlers = tools.toLayer(
	Effect.gen(function* () {
		const sessions = yield* Sessions;
		const scripts = yield* Scripts;
		const runs = yield* Runs;
		const places = yield* Places;
		const data = yield* GameData;
		const own = yield* OwnTables;
		const states = yield* StartStates;
		const fs = yield* FileSystem.FileSystem;
		const path = yield* Path.Path;
		const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
		const host = yield* Effect.scope;

		/** The caller's user, when the client can ask them. */
		const caller = Effect.map(
			Effect.serviceOption(McpSchema.McpServerClient),
			Option.filter(
				(client) => client.clientCapabilities.elicitation !== undefined
			)
		);

		const picturesOf = (run: RunId, names: readonly string[]) =>
			Effect.forEach(
				names,
				(name) =>
					Effect.map(runs.picture(run, name), (png) =>
						Picture.make({ path: name, png })
					),
				{ concurrency: 1 }
			);

		/** A run as run_script and wait_run answer it, from its record: the pictures are the ones its result names. */
		const ranOf = Effect.fnUntraced(function* (record: RunRecord) {
			const named = Arr.intersection(
				Arr.flatMap(record.steps, (step) => step.pictures ?? []),
				textsIn(record.result)
			);
			const shown = Arr.take(named, picturesPerAnswer);
			const answer: typeof Ran.Type = {
				run: record.id,
				outcome: record.outcome,
				...resultOf(record.result),
				...(named.length > shown.length
					? {
							more: `the result names ${named.length} pictures and ${shown.length} are here; get_run with pictures gives the rest`,
						}
					: {}),
				pictures: yield* picturesOf(record.id, shown),
			};
			return answer;
		});

		/** A part of a run's record: its head, then as many whole steps from `from` as fit, and their pictures. */
		const partOf = Effect.fnUntraced(function* (
			record: RunRecord,
			from: number,
			withPictures: boolean,
			ofPlan: boolean
		) {
			const { id, outcome, result, steps, plan, ...rest } = yield* Effect.orDie(
				Schema.encodeEffect(RunRecord)(record)
			);
			// The plan can outgrow an answer, so the steps' parts carry its size, and its own parts carry its edits.
			const planned = Option.match(Option.fromNullishOr(plan), {
				onNone: () => ({}),
				onSome: (each) =>
					ofPlan
						? { leftAlone: each.leftAlone }
						: {
								plan: {
									edits: each.edits.length,
									leftAlone: each.leftAlone.length,
									more: "get_run with plan: true reads each edit and where it came from, a part at a time",
								},
							},
			});
			const head = { id, outcome, ...resultOf(result), ...rest, ...planned };
			const entries: readonly unknown[] = ofPlan ? (plan?.edits ?? []) : steps;
			const kind = ofPlan ? "edits" : "steps";
			const asking = (at: number) =>
				`get_run with from: ${at}${ofPlan ? ", plan: true" : ""}`;
			const later = Arr.drop(entries, from);
			const next = `${kind} ${from} to ${entries.length - 1} of ${entries.length} are here; ${asking(entries.length)} gives the next ones`;
			const fit = fitting(
				later,
				bytes({ ...head, part: { from, count: 0, of: 0 }, [kind]: [], next })
			);
			// Each part holds one step at least, so asking again always goes on.
			let count = Math.max(
				Math.min(fit, later.length),
				Math.min(1, later.length)
			);
			const pictured = withPictures && !ofPlan;
			if (pictured) {
				let held = 0;
				const within = Arr.findFirstIndex(Arr.drop(steps, from), (step) => {
					held += step.pictures?.length ?? 0;
					return held > picturesPerAnswer;
				});
				count = Option.match(within, {
					onNone: () => count,
					onSome: (index) => Math.min(count, Math.max(index, 1)),
				});
			}
			const end = from + count;
			return {
				record: {
					...head,
					part: { from, count, of: entries.length },
					[kind]: Arr.take(later, count),
					...(end < entries.length
						? {
								next: `${kind} ${from} to ${end - 1} of ${entries.length} are here; ${asking(end)} gives the next ones`,
							}
						: {}),
				},
				pictures: pictured
					? yield* picturesOf(
							record.id,
							Arr.take(
								Arr.flatMap(
									Arr.take(Arr.drop(steps, from), count),
									(step) => step.pictures ?? []
								),
								picturesPerAnswer
							)
						)
					: [],
			};
		});

		/** Saves a run's script as a kept one under `scripts/`, unless a file there took its name meanwhile. */
		const keepAs = Effect.fnUntraced(function* (
			file: string,
			declared: Readonly<Record<string, unknown>>,
			body: string
		) {
			const written = yield* Effect.exit(
				fs.writeFileString(
					`${packageRoot}/${file}`,
					scriptAround(declared, body, "../src/script.ts").text,
					{ flag: "wx" }
				)
			);
			if (written._tag === "Failure") {
				return {
					notKept: `${file} appeared on the game's machine during the run, so the test was not kept`,
				};
			}
			yield* ran(
				spawner,
				ChildProcess.make(
					`${packageRoot}/node_modules/.bin/biome`,
					["format", "--write", file],
					{ cwd: packageRoot }
				)
			);
			return { kept: file };
		});

		return tools.of({
			run_script: Effect.fnUntraced(function* ({
				about,
				state,
				body,
				mod,
				textSpeed,
				limit,
				keep,
				detach,
			}) {
				const keeping = keep === undefined ? undefined : `scripts/${keep}.ts`;
				if (
					keeping !== undefined &&
					(yield* Effect.orDie(fs.exists(`${packageRoot}/${keeping}`)))
				) {
					return yield* KeptScriptExists.make({ path: keeping });
				}
				const run = yield* newRunId;
				const declared = {
					about,
					state: yield* Effect.orDie(Schema.encodeEffect(StartState)(state)),
					...(mod === undefined ? {} : { mod }),
					...(textSpeed === undefined ? {} : { textSpeed }),
					...(limit === undefined ? {} : { limit }),
				};
				const inline = scriptAround(
					declared,
					body,
					"../../src/script.ts",
					inlineRules
				);
				const file = `${inlineFolder}/${run}.ts`;
				yield* Effect.orDie(
					fs.makeDirectory(inlineFolder, { recursive: true })
				);
				yield* Effect.orDie(fs.writeFileString(file, inline.text));
				// A detached run's caller is gone before a hand over could reach it, so it finds nobody.
				const person = detach === true ? Option.none() : yield* caller;
				const going = Effect.gen(function* () {
					const ended = yield* Effect.scoped(
						Effect.andThen(
							Option.match(person, {
								onNone: () => Effect.void,
								onSome: (client) => sessions.handOversTo(run, personOf(client)),
							}),
							scripts.run(file, { run })
						)
					).pipe(
						Effect.mapError((failed) =>
							failed.reason === "typecheck"
								? ScriptFailed.make({
										reason: failed.reason,
										detail: inBody(failed.detail, inline.bodyStarts),
										...(failed.run === undefined ? {} : { run: failed.run }),
									})
								: failed
						)
					);
					if (ended.run === undefined) {
						const answer: typeof Ran.Type = {
							outcome: ended.outcome,
							pictures: [],
						};
						return answer;
					}
					const answer = yield* ranOf(yield* runs.get(ended.run));
					if (keeping === undefined) {
						return answer;
					}
					const proved =
						ended.outcome._tag === "passed" || ended.outcome._tag === "failed";
					return {
						...answer,
						...(proved
							? yield* keepAs(keeping, declared, body)
							: {
									notKept: `a run that ended ${ended.outcome._tag} proves nothing, so it was not kept`,
								}),
					};
				}).pipe(Effect.ensuring(Effect.ignore(fs.remove(file))));
				if (detach !== true) {
					return yield* going;
				}
				const fiber = yield* Effect.forkIn(going, host);
				const recorded = runs.get(run).pipe(
					Effect.retry({
						schedule: Schedule.spaced(recordSpacing),
						while: (error) => error._tag === "RunNotFound",
					}),
					Effect.as<typeof Ran.Type>({ run, outcome: running, pictures: [] })
				);
				return yield* Effect.raceFirst(recorded, Fiber.join(fiber));
			}),
			wait_run: Effect.fnUntraced(function* ({ run }) {
				const ended = runs.get(run).pipe(
					Effect.repeat({
						schedule: Schedule.spaced(recordSpacing),
						until: (record) => record.outcome._tag !== "running",
					})
				);
				const latest = yield* ended.pipe(
					Effect.timeoutOrElse({
						duration: waitLimit,
						orElse: () => runs.get(run),
					})
				);
				return yield* ranOf(latest);
			}),
			search_names: Effect.fnUntraced(function* ({ text: words, kind, limit }) {
				const names = yield* searchNames(data, own, words, {
					limit: limit ?? searchLimit,
					kind,
				});
				const count = fitting(names, bytes({ names: [] }) + moreRoom);
				return count === names.length
					? { names }
					: {
							names: Arr.take(names, count),
							more: `${count} of the ${names.length} found are here; ask with more words, or a kind, to see the rest`,
						};
			}),
			plan_state: Effect.fnUntraced(function* ({ state, mod }) {
				const planned = yield* states.plan(
					state,
					mod === undefined || mod === "none"
						? mod
						: yield* readMod(fs, path, mod.folder)
				);
				const { edits, leftAlone } = planned;
				const base = { name: planned.base.name, about: planned.base.about };
				const count = fitting(
					edits,
					bytes({ base, edits: [], leftAlone }) + moreRoom
				);
				return count === edits.length
					? { base, edits, leftAlone }
					: {
							base,
							edits: Arr.take(edits, count),
							leftAlone,
							more: `${count} of the ${edits.length} edits are here; plan the state a few words at a time to see the rest`,
						};
			}),
			start_game: Effect.fnUntraced(function* ({ state, mod, textSpeed }) {
				const run = yield* newRunId;
				const taken =
					mod === undefined || mod === "none"
						? mod
						: yield* readMod(fs, path, mod.folder);
				const game = yield* sessions.keep({
					run,
					state,
					mod: taken,
					textSpeed,
					owner: "start_game",
					waitForPlace: defaultPlaceWait,
				});
				const read = Rec.toEntries(game.readBack.values);
				const started = {
					session: game.session,
					run: game.run,
					place: game.place,
				};
				const count = fitting(
					read,
					bytes({
						...started,
						readBack: { values: {}, notRead: game.readBack.notRead },
					}) + moreRoom
				);
				return {
					...started,
					readBack: {
						values: Rec.fromEntries(Arr.take(read, count)),
						notRead: game.readBack.notRead,
					},
					...(count === read.length
						? {}
						: {
								more: `${count} of the ${read.length} values read back are here; read_values reads any of them`,
							}),
				};
			}),
			stop_game: Effect.fnUntraced(function* ({ session }) {
				const game = yield* sessions.stop(session, stoppedByCaller);
				const record = yield* Effect.orDie(runs.get(game.run));
				return { run: record.id, outcome: record.outcome };
			}),
			press: ({ session, buttons, frames }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.press(buttons, frames ?? pressFrames)
				),
			press_until: ({ session, buttons, value, expected, times, every }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.pressUntil(
						buttons,
						value,
						expected,
						times ?? pressUntilTimes,
						every ?? pressUntilEvery
					)
				),
			complete_dialog: Effect.fnUntraced(function* ({
				session,
				button,
				times,
				every,
			}) {
				const game = yield* sessions.get(session);
				const talk = yield* game.completeDialog(
					button ?? "A",
					times ?? dialogTimes,
					every ?? dialogEvery
				);
				return {
					presses: talk.presses,
					pictures: talk.pictures,
					...(talk.pictures.length > picturesPerAnswer
						? {
								more: `the pictures past the fourth are in run ${game.run}: get_run with pictures shows them`,
							}
						: {}),
					shown: yield* picturesOf(
						game.run,
						Arr.take(talk.pictures, picturesPerAnswer)
					),
				};
			}),
			stick: ({ session, position, frames }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.stick(position, frames)
				),
			read_values: Effect.fnUntraced(function* ({ session, values }) {
				const game = yield* sessions.get(session);
				const read = Rec.toEntries(yield* game.read(values));
				const count = fitting(read, bytes({ values: {} }) + moreRoom);
				return {
					values: Rec.fromEntries(Arr.take(read, count)),
					...(count === read.length
						? {}
						: {
								more: `${count} of the ${read.length} values are here; read the rest in another call`,
							}),
				};
			}),
			set_values: ({ session, values }) =>
				Effect.flatMap(sessions.get(session), (game) => game.set(values)),
			screenshot: ({ session, label }) =>
				Effect.flatMap(sessions.get(session), (game) => game.screenshot(label)),
			places: () => places.list,
			list_runs: Effect.fnUntraced(function* ({ script, limit }) {
				const { runs: found, unreadable } = yield* runs.list({
					script,
					limit: limit ?? runsLimit,
				});
				const count = fitting(
					found,
					bytes({ runs: [], unreadable }) + moreRoom
				);
				return count === found.length
					? { runs: found, unreadable }
					: {
							runs: Arr.take(found, count),
							unreadable,
							more: `${count} of the ${found.length} runs are here; ask for one script's runs to see the rest`,
						};
			}),
			get_run: Effect.fnUntraced(function* ({ run, from, pictures, plan }) {
				const record = yield* runs.get(run);
				return yield* partOf(
					record,
					from ?? 0,
					pictures === true,
					plan === true
				);
			}),
		});
	})
);

const answer = (value: unknown) => [text(value)];

/** The tools on the host's services, each answer as JSON text and each picture as an image the model sees. */
export const registrations = McpServer.toolkit(tools, {
	content: {
		run_script: ({ pictures, ...rest }) => [
			text(rest),
			...Arr.map(pictures, image),
		],
		wait_run: ({ pictures, ...rest }) => [
			text(rest),
			...Arr.map(pictures, image),
		],
		search_names: answer,
		plan_state: answer,
		start_game: answer,
		stop_game: answer,
		read_values: answer,
		places: answer,
		list_runs: answer,
		screenshot: (picture) => [image(picture)],
		press_until: answer,
		complete_dialog: ({ shown, ...rest }) => [
			text(rest),
			...Arr.map(shown, image),
		],
		get_run: ({ record, pictures }) => [
			text(record),
			...Arr.map(pictures, image),
		],
	},
}).pipe(Layer.provide(handlers));
