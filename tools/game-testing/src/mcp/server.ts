/**
 * The MCP server: the tools' handlers, how their answers reach a model, and the instructions every session gets. The
 * handlers ask `Machine`, so one set serves a game on this machine and a game on a far one. A picture answers as an MCP
 * image block, the rest as JSON text of at most `answerLimit` bytes: an answer that would be longer is cut at whole
 * entries and says how to read the rest. A run's record is its answer, so the server holds nothing for a caller that is
 * gone, but a game started by hand: its `Open` stream, held here on an idle lease. The same registrations serve stdio
 * (`mcp`) and HTTP (`serve`).
 */
import {
	Array as Arr,
	Cause,
	Deferred,
	Duration,
	Effect,
	Fiber,
	FileSystem,
	Formatter,
	Layer,
	Option,
	Path,
	Predicate,
	Queue,
	Record as Rec,
	Schedule,
	Schema,
} from "effect";
import { McpProtocol, McpSchema, McpServer } from "effect/ai";
import { ChildProcess, ChildProcessSpawner } from "effect/process";
import type { Outcome } from "../contract/record.ts";
import { RunRecord } from "../contract/record.ts";
import { StartState } from "../contract/state.ts";
import type { RunId, SessionId } from "../contract/values.ts";
import { newRunId, Picture } from "../contract/values.ts";
import type { RunNotFound, StartError } from "../errors.ts";
import {
	KeptScriptExists,
	NobodyThere,
	ScriptFailed,
	SessionNotFound,
	SessionNotStartedHere,
} from "../errors.ts";
import type { Opened, Opening } from "../game.ts";
import {
	defaultPlaceWait,
	dialogEvery,
	dialogTimes,
	pressFrames,
	pressUntilEvery,
	pressUntilTimes,
} from "../game.ts";
import type { Person } from "../host/sessions.ts";
import type { Landed, Unreachable } from "../machine.ts";
import { Machine } from "../machine.ts";
import { readMod } from "../mod.ts";
import { ran } from "../process.ts";
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

/** How often `wait_run` and a detached `run_script` read a run's record, which may cross to a far machine. */
const recordSpacing = Duration.seconds(1);

const running: Outcome = { _tag: "running" };

/** A game started by hand stops when no call names its session for this long. */
const leaseIdle = Duration.minutes(10);
const leaseEnded = "no call named its session for 10 minutes";
const stoppedByCaller = "stop_game ended it";
const serverEnded = "the MCP server that started it stopped";

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

/** How a game started by hand ended: its run's outcome, and where its files are on this machine or why they are not. */
interface Stopped {
	readonly folder?: string;
	readonly notCopied?: string;
	readonly outcome: Outcome;
	readonly run: RunId;
}

/** A game started by hand, while it lasts. */
interface Held {
	/** Renews its lease. */
	readonly called: Effect.Effect<void>;
	/** Ends it for `reason`, unless an end came first. It ends whether or not the caller then waits for `stopped`. */
	readonly end: (reason: string) => Effect.Effect<void>;
	readonly opened: Opened;
	readonly stopped: Effect.Effect<Stopped, RunNotFound>;
}

const handlers = tools.toLayer(
	Effect.gen(function* () {
		const machine = yield* Machine;
		const scripts = yield* Scripts;
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
					Effect.map(machine.file(run, name), (png) =>
						Picture.make({ path: name, png })
					),
				{ concurrency: 1 }
			);

		/** Where a landed run's files are: its folder here, or why they are not here. */
		const placeOf = (landed: Landed) =>
			"folder" in landed
				? { folder: landed.folder }
				: { notCopied: landed.notCopied.message };

		/**
		 * A run as run_script and wait_run answer it, from its record. With `pictured`, the pictures its result names come
		 * too: those of a run whose files are here.
		 */
		const ranOf = Effect.fnUntraced(function* (
			record: RunRecord,
			pictured: boolean
		) {
			const named = pictured
				? Arr.intersection(
						Arr.flatMap(record.steps, (step) => step.pictures ?? []),
						textsIn(record.result)
					)
				: [];
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

		/**
		 * An ended run's answer, once its files have landed here: from its record, or with `outcome` and why its files
		 * are not here when the game machine's record could not be read either.
		 */
		const endedOf = Effect.fnUntraced(function* (run: RunId, outcome: Outcome) {
			const landed = yield* machine.landed(run);
			if (landed.record === undefined) {
				const answer: typeof Ran.Type = {
					run,
					outcome,
					...placeOf(landed),
					pictures: [],
				};
				return answer;
			}
			const answer: typeof Ran.Type = {
				...(yield* ranOf(landed.record, "folder" in landed)),
				...placeOf(landed),
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
					notKept: `${file} appeared in the package this server runs from, on your machine, during the run, so the test was not kept`,
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

		/** The games started by hand, by session, while each lasts. */
		const games = new Map<SessionId, Held>();

		/**
		 * A game started by hand, from its start to its end, in a fiber of this server's own. It ends one way: its scope
		 * closes, and the one finalizer sends `Stop` with the first reason given, by stop_game, by its lease, or by this
		 * server's stop, then waits for its `Open` stream to end. A game whose stream ended by itself has nothing to stop.
		 */
		const hold = (
			opening: Opening,
			started: Deferred.Deferred<Opened, StartError | Unreachable>,
			stopped: Deferred.Deferred<Stopped, RunNotFound>
		) =>
			Effect.gen(function* () {
				const opened = yield* machine.open(opening);
				const { session } = opened;
				const ending = yield* Deferred.make<Option.Option<string>>();
				yield* Effect.addFinalizer(() =>
					Deferred.succeed(ending, Option.some(serverEnded)).pipe(
						Effect.andThen(Deferred.await(ending)),
						Effect.flatMap(
							Option.match({
								onNone: () => Effect.void,
								onSome: (reason) =>
									Effect.andThen(
										Effect.ignoreCause(
											machine.call((client) => client.Stop({ session, reason }))
										),
										opened.ended
									),
							})
						)
					)
				);
				yield* Effect.forkScoped(
					Effect.andThen(opened.ended, Deferred.succeed(ending, Option.none()))
				);
				const calls = yield* Queue.sliding<void>(1);
				// The lease: it ends when no call comes for `leaseIdle`.
				yield* Effect.forkScoped(
					Queue.take(calls).pipe(
						Effect.timeoutOption(leaseIdle),
						Effect.repeat({ while: (call) => Option.isSome(call) }),
						Effect.andThen(Deferred.succeed(ending, Option.some(leaseEnded)))
					)
				);
				yield* Effect.acquireRelease(
					Effect.sync(() =>
						games.set(session, {
							opened,
							called: Queue.offer(calls, undefined),
							end: (reason) => Deferred.succeed(ending, Option.some(reason)),
							stopped: Deferred.await(stopped),
						})
					),
					() => Effect.sync(() => games.delete(session))
				);
				yield* Deferred.succeed(started, opened);
				yield* Deferred.await(ending);
				return opened.run;
			}).pipe(
				Effect.scoped,
				Effect.onError((cause) => Deferred.failCause(started, cause)),
				Effect.flatMap((run) =>
					Effect.map(
						machine.landed(run),
						(landed): Stopped => ({
							run,
							outcome: landed.record?.outcome ?? running,
							...placeOf(landed),
						})
					).pipe(
						Effect.exit,
						Effect.flatMap((exit) => Deferred.done(stopped, exit))
					)
				),
				// A start that failed has said so through `started`.
				Effect.ignore
			);

		/** The game this server started by hand with this session. */
		const heldBy = Effect.fnUntraced(function* (session: SessionId) {
			const held = games.get(session);
			if (held !== undefined) {
				return held;
			}
			const listed = yield* machine.call((client) => client.Places());
			return yield* Arr.some(
				listed,
				(place) => place.holder?.session === session
			)
				? SessionNotStartedHere.make({ session })
				: SessionNotFound.make({ session });
		});

		/** The game this server started by hand with this session, whose lease the call renews. */
		const leaseOf = (session: SessionId) =>
			Effect.tap(heldBy(session), (held) => held.called);

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
								onSome: (client) => machine.handOversTo(run, personOf(client)),
							}),
							scripts.run(file, { run })
						)
					).pipe(
						Effect.mapError((failed) =>
							failed._tag === "ScriptFailed" && failed.reason === "typecheck"
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
					const answer = yield* endedOf(ended.run, ended.outcome);
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
				const recorded = machine.record(run).pipe(
					Effect.retry({
						schedule: Schedule.spaced(recordSpacing),
						while: (error) => error._tag === "RunNotFound",
					}),
					Effect.as<typeof Ran.Type>({ run, outcome: running, pictures: [] })
				);
				return yield* Effect.raceFirst(recorded, Fiber.join(fiber));
			}),
			wait_run: Effect.fnUntraced(function* ({ run }) {
				const ended = machine.record(run).pipe(
					Effect.repeat({
						schedule: Schedule.spaced(recordSpacing),
						until: (record) => record.outcome._tag !== "running",
					})
				);
				const latest = yield* ended.pipe(
					Effect.timeoutOrElse({
						duration: waitLimit,
						orElse: () => machine.record(run),
					})
				);
				return latest.outcome._tag === "running"
					? yield* ranOf(latest, false)
					: yield* endedOf(run, latest.outcome);
			}),
			search_names: Effect.fnUntraced(function* ({ text: words, kind, limit }) {
				const names = yield* machine.call((client) =>
					client.Search({
						text: words,
						limit: limit ?? searchLimit,
						...(kind === undefined ? {} : { kind }),
					})
				);
				const count = fitting(names, bytes({ names: [] }) + moreRoom);
				return count === names.length
					? { names }
					: {
							names: Arr.take(names, count),
							more: `${count} of the ${names.length} found are here; ask with more words, or a kind, to see the rest`,
						};
			}),
			plan_state: Effect.fnUntraced(function* ({ state, mod }) {
				const taken =
					mod === undefined || mod === "none"
						? mod
						: yield* readMod(fs, path, mod.folder);
				const { base, start, edits, leftAlone, location } = yield* machine.call(
					(client) =>
						client.Plan({
							state,
							...(taken === undefined ? {} : { mod: taken }),
						})
				);
				const head = {
					base,
					...(start === undefined ? {} : { start }),
					...(location === undefined ? {} : { location }),
				};
				const count = fitting(
					edits,
					bytes({ ...head, edits: [], leftAlone }) + moreRoom
				);
				return count === edits.length
					? { ...head, edits, leftAlone }
					: {
							...head,
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
				const started = yield* Deferred.make<
					Opened,
					StartError | Unreachable
				>();
				const stopped = yield* Deferred.make<Stopped, RunNotFound>();
				const game = yield* Effect.forkIn(
					hold(
						{
							run,
							state,
							owner: "start_game",
							waitForPlace: defaultPlaceWait,
							...(taken === undefined ? {} : { mod: taken }),
							...(textSpeed === undefined ? {} : { textSpeed }),
						},
						started,
						stopped
					),
					host
				);
				// A start its caller gives up on starts nothing.
				const opened = yield* Deferred.await(started).pipe(
					Effect.onInterrupt(() => Fiber.interrupt(game))
				);
				const { session } = opened;
				const read = Rec.toEntries(opened.readBack.values);
				const answered = { session, run: opened.run, place: opened.place };
				const count = fitting(
					read,
					bytes({
						...answered,
						readBack: { values: {}, notRead: opened.readBack.notRead },
					}) + moreRoom
				);
				return {
					...answered,
					readBack: {
						values: Rec.fromEntries(Arr.take(read, count)),
						notRead: opened.readBack.notRead,
					},
					...(count === read.length
						? {}
						: {
								more: `${count} of the ${read.length} values read back are here; read_values reads any of them`,
							}),
				};
			}),
			stop_game: Effect.fnUntraced(function* ({ session }) {
				const held = yield* heldBy(session);
				yield* held.end(stoppedByCaller);
				return yield* held.stopped;
			}),
			press: ({ session, buttons, frames }) =>
				Effect.andThen(
					leaseOf(session),
					machine.call((client) =>
						client.Press({ session, buttons, frames: frames ?? pressFrames })
					)
				),
			press_until: ({ session, buttons, value, expected, times, every }) =>
				Effect.andThen(
					leaseOf(session),
					machine.call((client) =>
						client.PressUntil({
							session,
							buttons,
							value,
							expected,
							times: times ?? pressUntilTimes,
							every: every ?? pressUntilEvery,
						})
					)
				),
			complete_dialog: Effect.fnUntraced(function* ({
				session,
				button,
				times,
				every,
			}) {
				const { run } = (yield* leaseOf(session)).opened;
				const talk = yield* machine.call((client) =>
					client.CompleteDialog({
						session,
						button: button ?? "A",
						times: times ?? dialogTimes,
						every: every ?? dialogEvery,
					})
				);
				return {
					presses: talk.presses,
					pictures: talk.pictures,
					...(talk.pictures.length > picturesPerAnswer
						? {
								more: `the pictures past the fourth are in run ${run}: get_run with pictures shows them`,
							}
						: {}),
					shown: yield* picturesOf(
						run,
						Arr.take(talk.pictures, picturesPerAnswer)
					),
				};
			}),
			stick: ({ session, position, frames }) =>
				Effect.andThen(
					leaseOf(session),
					machine.call((client) => client.Stick({ session, position, frames }))
				),
			read_values: Effect.fnUntraced(function* ({ session, values }) {
				yield* leaseOf(session);
				const read = Rec.toEntries(
					yield* machine.call((client) => client.Read({ session, values }))
				);
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
				Effect.andThen(
					leaseOf(session),
					machine.call((client) => client.Set({ session, values }))
				),
			screenshot: ({ session, label }) =>
				Effect.andThen(
					leaseOf(session),
					machine.call((client) =>
						client.Screenshot({
							session,
							...(label === undefined ? {} : { label }),
						})
					)
				),
			places: () => machine.call((client) => client.Places()),
			list_runs: Effect.fnUntraced(function* ({ script, limit }) {
				const { runs: found, unreadable } = yield* machine.call((client) =>
					client.Runs({
						limit: limit ?? runsLimit,
						...(script === undefined ? {} : { script }),
					})
				);
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
				const record = yield* machine.record(run);
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
