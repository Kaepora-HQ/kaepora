/**
 * The command line. `run` is a host: it serves the scripts it starts as children. The other commands read the game's
 * tables, the places or the records on this machine, and each builds only what it reads, on the platform and our own
 * tables, which the entry point gives.
 */
import {
	Array as Arr,
	Config,
	Console,
	Context,
	Duration,
	Effect,
	FileSystem,
	Layer,
	Option,
	Path,
	Record as Rec,
	Schema,
	String as Str,
} from "effect";
import { McpServer } from "effect/ai";
import { Argument, CliError, Command, Flag } from "effect/cli";
import type { Origin, PlannedEdit, StatePlan } from "./contract/plan.ts";
import { originText } from "./contract/plan.ts";
import type { Outcome } from "./contract/record.ts";
import type { ChestSpots, StartSpot } from "./contract/spots.ts";
import { StartState } from "./contract/state.ts";
import { NameKind, RunId } from "./contract/values.ts";
import { namesHere, writeNames } from "./data/declarations.ts";
import { GameData } from "./data/game-data.ts";
import { TokenRequired } from "./errors.ts";
import { Far, farUrl, reached, token, writeFarNames } from "./far.ts";
import { host, tables } from "./host/host.ts";
import { Places } from "./host/places.ts";
import { Runs } from "./host/runs.ts";
import {
	instructions,
	protocols,
	registrations,
	serverName,
} from "./mcp/server.ts";
import { readMod, scriptMod } from "./mod.ts";
import { chestSpots, Spots } from "./save/spots.ts";
import { StartStates } from "./save/state.ts";
import { OwnTables, searchNames } from "./save/tables.ts";
import type { ScriptRun } from "./scripts/runner.ts";
import {
	declarationCommand,
	declarationOf,
	Scripts,
	workerCommand,
} from "./scripts/runner.ts";
import { declare, work } from "./scripts/worker.ts";
import { serve as serveOn } from "./serve/server.ts";

/**
 * Runs `here` on this machine, or, with `GAME_TESTING_URL` set, `far` against that machine's `serve`, whose
 * connection closes when it ends.
 */
const hereOrFar = <A, E, R, B, E2, R2>(
	here: Effect.Effect<A, E, R>,
	far: (machine: Far["Service"]) => Effect.Effect<B, E2, R2>
) =>
	Effect.flatMap(
		farUrl,
		Option.match({
			onNone: (): Effect.Effect<
				A | B,
				E | E2 | Layer.Error<ReturnType<typeof Far.layer>>,
				R | Exclude<R2, Far>
			> => here,
			onSome: (url) => Far.use(far).pipe(Effect.provide(Far.layer(url))),
		})
	);

/** The package's version, which `mcp` and `serve` name to their clients. */
export const version = "0.0.0";

/** A handler's failure as the command line shows it: its message. */
const shown = <A, E extends { readonly message: string }, R>(
	effect: Effect.Effect<A, E, R>
) =>
	Effect.mapError(effect, (error) =>
		CliError.UserError.make({ cause: error, userMessage: error.message })
	);

const scriptsHere = Scripts.layer.pipe(Layer.provideMerge(host));

/** The scripts `run` runs: against this machine's game, or with `GAME_TESTING_URL` against the far machine's. */
const scriptsLayer = Layer.unwrap(
	Effect.map(
		farUrl,
		(
			url
		): Layer.Layer<
			Scripts,
			| Layer.Error<typeof scriptsHere>
			| Layer.Error<ReturnType<typeof Scripts.layerRemote>>,
			| Layer.Services<typeof scriptsHere>
			| Layer.Services<ReturnType<typeof Scripts.layerRemote>>
		> => (Option.isSome(url) ? Scripts.layerRemote(url.value) : scriptsHere)
	)
);

const ended = (outcome: Outcome) =>
	outcome._tag === "passed" || outcome._tag === "skipped";

const why = (outcome: Outcome) => {
	switch (outcome._tag) {
		case "failed":
		case "skipped":
		case "stopped":
			return [outcome.reason];
		case "broken":
			return Arr.filter([outcome.reason, outcome.error], Str.isNonEmpty);
		case "unchecked":
			return ["it ended with no check, so it proved nothing"];
		default:
			return [];
	}
};

/** A run's outcome, why, and where its record and pictures are on this machine. */
const report = (
	script: string,
	ran: ScriptRun,
	folder: (run: RunId) => string
) =>
	Effect.forEach(
		[
			Arr.join(
				Arr.filter([ran.outcome._tag, script, ran.run ?? ""], Str.isNonEmpty),
				"  "
			),
			...Arr.map(why(ran.outcome), (line) => `    ${line}`),
			...(ran.run === undefined
				? []
				: [
						`    ${ran.notCopied ?? `record and pictures: ${folder(ran.run)}`}`,
					]),
		],
		(line) => Console.log(line),
		{ concurrency: 1, discard: true }
	);

const run = Command.make(
	"run",
	{
		scripts: Argument.File("script", { mustExist: true }).pipe(
			Argument.variadic({ min: 1 })
		),
		parallel: Flag.Int("parallel").pipe(
			Flag.withDescription(
				"How many scripts run at once, each in a place of its own."
			),
			Flag.withDefault(1)
		),
		rerunBroken: Flag.Boolean("rerun-broken").pipe(
			Flag.withDefault(false),
			Flag.withDescription(
				"Runs a script once more when its run is broken, and reports both runs. The exit status counts each script's last run."
			)
		),
	},
	({ scripts, parallel, rerunBroken }) =>
		Effect.gen(function* () {
			// Built here, so a far machine that cannot be reached says why on stderr.
			const built = yield* shown(Layer.build(scriptsLayer));
			const runner = Context.get(built, Scripts);
			const records = yield* Runs;
			const attempt = (script: string, attemptOf?: RunId) =>
				runner.run(script, { attemptOf }).pipe(
					Effect.catch(
						(failed): Effect.Effect<ScriptRun> =>
							Effect.succeed({
								outcome: {
									_tag: "broken",
									reason: `the script failed (${failed.reason})`,
									error: failed.detail,
								},
								...(failed.run === undefined ? {} : { run: failed.run }),
							})
					),
					Effect.tap((ran) => report(script, ran, records.folder))
				);
			const last = yield* Effect.forEach(
				scripts,
				Effect.fnUntraced(function* (script) {
					const first = yield* attempt(script);
					return rerunBroken &&
						first.outcome._tag === "broken" &&
						first.run !== undefined
						? yield* attempt(script, first.run)
						: first;
				}),
				{ concurrency: parallel }
			);
			const missed = Arr.filter(last, (ran) => !ended(ran.outcome)).length;
			if (missed > 0) {
				return yield* CliError.UserError.make({
					cause: missed,
					userMessage: `${missed} of ${last.length} scripts neither passed nor were skipped.`,
				});
			}
		}).pipe(Effect.scoped, Effect.provide(Runs.layer))
).pipe(
	Command.withDescription(
		"Runs scripts against this machine's game, each in a place of its own, and keeps a record of each run. With GAME_TESTING_URL and GAME_TESTING_TOKEN set, the scripts run here against the game of the machine whose serve the URL reaches, which keeps the records."
	)
);

/**
 * A script's declared state and mod, after its type check against the game's names, `gameNames`, or a state written as
 * JSON, which names no mod.
 */
const declaredIn = Effect.fnUntraced(function* (
	file: string,
	gameNames: string
) {
	const fs = yield* FileSystem.FileSystem;
	const path = yield* Path.Path;
	if (Str.endsWith(".json")(file)) {
		const text = yield* Effect.orDie(fs.readFileString(file));
		const state = yield* Effect.orDie(
			Schema.decodeEffect(Schema.fromJsonString(StartState))(text)
		);
		return { state, mod: undefined };
	}
	const declaration = yield* declarationOf(file, gameNames);
	const folder = scriptMod(path, file, declaration.mod);
	const mod =
		folder === undefined || folder === "none"
			? folder
			: yield* readMod(fs, path, folder.folder);
	return { state: declaration.state, mod };
});

const fromMod = (from: Origin) => from._tag === "mod";

const nameOf = (edit: PlannedEdit) =>
	edit._tag === "key" ? `key ${edit.key}` : edit.name;

/** What the base held, or for a value the mod adds, that the base holds none. */
const wasOf = (edit: PlannedEdit) =>
	edit._tag === "added"
		? "added by the mod: the base has no record, so the game starts it so"
		: `was ${String(edit.base)}`;

const spotKinds: Readonly<Record<StartSpot["kind"], string>> = {
	given: "a place the state gives",
	landing: "a landing of the game",
	"worked out": "a worked-out spot",
	found: "a found spot",
	proven: "a proven spot",
};

/** Where a start stands Link, and what kind of spot that is. */
const standing = (spot: StartSpot) =>
	`Link stands in ${spot.scene}${spot.room === undefined ? "" : ` room ${spot.room}`} at (${Arr.join(Arr.map(spot.position, String), ", ")}), facing ${spot.facing}: ${spotKinds[spot.kind]}, ${spot.about}`;

/**
 * A plan as `plan` prints it: the base, where Link stands, each value the words write, each a place it resets leaves alone, and then under a
 * heading of its own what the mod's new game starts with.
 */
const printPlan = (planned: StatePlan) =>
	Effect.gen(function* () {
		const words = Arr.filter(planned.edits, (edit) => !fromMod(edit.from));
		const started = Arr.filter(planned.edits, (edit) => fromMod(edit.from));
		const left = Arr.filter(planned.leftAlone, (each) => !fromMod(each.from));
		const moved = Arr.filter(planned.leftAlone, (each) => fromMod(each.from));
		yield* Console.log(`From ${planned.base.name}: ${planned.base.about}`);
		if (planned.location !== undefined) {
			yield* Console.log(standing(planned.location));
		}
		for (const edit of words) {
			yield* Console.log(
				`${nameOf(edit)} = ${String(edit.to)}    ${wasOf(edit)}, from ${originText(edit.from)}`
			);
		}
		for (const each of left) {
			yield* Console.log(
				`${each.name} left alone: ${originText(each.from)} names it, and it is not the place's own`
			);
		}
		const mod = Arr.head([...started, ...moved]);
		if (Option.isNone(mod)) {
			return;
		}
		yield* Console.log(
			`From ${originText(mod.value.from)}, as its new game starts:`
		);
		for (const edit of started) {
			yield* Console.log(
				`${nameOf(edit)} = ${String(edit.to)}    ${wasOf(edit)}`
			);
		}
		for (const each of moved) {
			yield* Console.log(
				`${each.name} left alone: the mod's new game starts it otherwise, and play moved it in the base`
			);
		}
	});

const plan = Command.make(
	"plan",
	{ file: Argument.File("script-or-state", { mustExist: true }) },
	({ file }) =>
		shown(
			Effect.flatMap(
				hereOrFar(
					Effect.gen(function* () {
						const states = yield* StartStates;
						const { state, mod } = yield* declaredIn(file, namesHere);
						const { base, edits, leftAlone, location } = yield* states.plan(
							state,
							mod
						);
						const planned: StatePlan = {
							base,
							edits,
							leftAlone,
							...(location === undefined ? {} : { location }),
						};
						return planned;
					}).pipe(Effect.provide(tables)),
					Effect.fnUntraced(function* (far) {
						const names = yield* writeFarNames(far);
						const { state, mod } = yield* declaredIn(file, names);
						return yield* reached(
							far.url,
							far.client.Plan({ state, ...(mod === undefined ? {} : { mod }) })
						);
					})
				),
				printPlan
			)
		)
).pipe(
	Command.withDescription(
		"Prints every value a start state writes, what the base held and the word it came from, without a game. With GAME_TESTING_URL, the far machine plans it."
	)
);

const places = Command.make("places", {}, () =>
	shown(
		Effect.flatMap(
			hereOrFar(
				Effect.flatMap(Places, (machine) => machine.list).pipe(
					Effect.provide(Places.layer)
				),
				(far) => reached(far.url, far.client.Places())
			),
			(listed) =>
				Effect.forEach(
					listed,
					(place) =>
						Console.log(
							place.holder === undefined
								? `${place.place}  free`
								: `${place.place}  held by ${place.holder.owner} since ${place.holder.since}, run ${place.holder.run}`
						),
					{ concurrency: 1, discard: true }
				)
		)
	)
).pipe(
	Command.withDescription(
		"Lists this machine's places and who holds each; with GAME_TESTING_URL, the far machine's."
	)
);

const search = Command.make(
	"search",
	{
		text: Argument.String("text"),
		kind: Flag.Literals("kind", NameKind.literals).pipe(Flag.optional),
		limit: Flag.Int("limit").pipe(Flag.withDefault(40)),
	},
	({ text, kind, limit }) =>
		shown(
			Effect.flatMap(
				hereOrFar(
					Effect.gen(function* () {
						const data = yield* GameData;
						const own = yield* OwnTables;
						const kept = yield* Spots.use((service) => service.all);
						return yield* searchNames(data, own, kept, text, {
							limit,
							kind: Option.getOrUndefined(kind),
						});
					}).pipe(Effect.provide([GameData.layer, Spots.layer])),
					(far) =>
						reached(
							far.url,
							far.client.Search({
								text,
								limit,
								...Option.match(kind, {
									onNone: () => ({}),
									onSome: (named) => ({ kind: named }),
								}),
							})
						)
				),
				(found) =>
					Effect.forEach(
						found,
						(match) =>
							Console.log(`${match.kind}  ${match.name}  ${match.about}`),
						{ concurrency: 1, discard: true }
					)
			)
		)
).pipe(
	Command.withDescription(
		"Finds every name a state or a script takes that contains the text: saved values, items, quests, scenes, landings, event settings, checks, spots and bases. With GAME_TESTING_URL, the far machine finds them."
	)
);

const kindOrder: readonly ChestSpots["spots"][number]["kind"][] = [
	"proven",
	"found",
	"worked out",
];

/** A chest's best spot, of any age, or none. */
const bestOf = (chest: ChestSpots) =>
	Arr.findFirst(kindOrder, (kind) =>
		Arr.some(chest.spots, (spot) => spot.kind === kind)
	).pipe(Option.getOrElse(() => "none"));

/** How many of `chests` have a best spot of each kind, in words. */
const counted = (chests: readonly ChestSpots[]) =>
	Arr.join(
		Arr.flatMapNullishOr([...kindOrder, "none"], (kind) => {
			const count = Arr.filter(
				chests,
				(chest) => bestOf(chest) === kind
			).length;
			return count === 0 ? undefined : `${count} ${kind}`;
		}),
		", "
	);

const spots = Command.make("spots", {}, () =>
	shown(
		Effect.flatMap(
			hereOrFar(
				Effect.gen(function* () {
					const data = yield* GameData;
					const own = yield* OwnTables;
					const kept = yield* Spots;
					return yield* chestSpots(data, own, kept);
				}).pipe(Effect.provide([GameData.layer, Spots.layer])),
				(far) => reached(far.url, far.client.Spots())
			),
			Effect.fnUntraced(function* (chests) {
				const areas = Arr.groupBy(chests, (chest) => chest.area);
				for (const [area, inArea] of Rec.toEntries(areas)) {
					yield* Console.log(
						`${area === "" ? "No chest in the remake" : area}: ${counted(inArea)}`
					);
					for (const chest of inArea) {
						if (chest.spots.length === 0) {
							yield* Console.log(`  none        ${chest.name}`);
						}
						for (const spot of chest.spots) {
							yield* Console.log(
								`  ${Str.padEnd(10)(spot.kind)}  ${chest.name}  ${spot.age}: ${spot.about}`
							);
						}
					}
				}
				yield* Console.log(`${chests.length} chests: ${counted(chests)}.`);
			})
		)
	)
).pipe(
	Command.withDescription(
		"Lists every chest by area, with the spot each age starts on: proven once a look passed from it, found in a save or a running game, worked out before its actor, or none. With GAME_TESTING_URL, the far machine's spots, which are the ones its looks use."
	)
);

const runs = Command.make(
	"runs",
	{
		script: Flag.String("script").pipe(Flag.optional),
		limit: Flag.Int("limit").pipe(Flag.withDefault(20)),
	},
	({ script, limit }) =>
		Effect.gen(function* () {
			const filter = Option.match(script, {
				onNone: () => ({ limit }),
				onSome: (path) => ({ script: path, limit }),
			});
			const found = yield* shown(
				hereOrFar(
					Effect.flatMap(Runs, (records) => records.list(filter)).pipe(
						Effect.provide(Runs.layer)
					),
					(far) => reached(far.url, far.client.Runs(filter))
				)
			);
			for (const summary of found.runs) {
				yield* Console.log(
					Arr.join(
						Arr.filter(
							[
								summary.id,
								summary.outcome._tag,
								summary.script ?? "",
								summary.milliseconds === undefined
									? ""
									: Duration.format(Duration.millis(summary.milliseconds)),
							],
							Str.isNonEmpty
						),
						"  "
					)
				);
			}
			if (found.unreadable > 0) {
				yield* Console.log(
					`${found.unreadable} more records could not be read: a record written by another version of the tool, or one being written.`
				);
			}
		})
).pipe(
	Command.withDescription(
		"Lists past runs on this machine, newest first; with GAME_TESTING_URL, the far machine's runs."
	)
);

const names = Command.make("names", {}, () =>
	shown(
		hereOrFar(writeNames.pipe(Effect.provide(GameData.layer)), writeFarNames)
	)
).pipe(
	Command.withDescription(
		"Writes generated/names.d.ts from the game's tables, so scripts' names and values are checked as they are typed. With GAME_TESTING_URL, from the far machine's."
	)
);

const worker = Command.make(
	workerCommand,
	{
		host: Flag.String("host"),
		run: Flag.String("run").pipe(Flag.withSchema(RunId)),
		attemptOf: Flag.String("attempt-of").pipe(
			Flag.withSchema(RunId),
			Flag.optional
		),
		script: Argument.File("script", { mustExist: true }),
	},
	({ host: where, script, run: id, attemptOf }) =>
		work({
			host: where,
			script,
			run: id,
			attemptOf: Option.getOrUndefined(attemptOf),
		})
).pipe(Command.unlisted);

const declaration = Command.make(
	declarationCommand,
	{ script: Argument.File("script", { mustExist: true }) },
	({ script }) => declare(script)
).pipe(Command.unlisted);

/**
 * A host's services for `mcp` and `serve`, with the generated names written first, so a body's misspelled name fails
 * its type check on this machine.
 */
const hosting = Layer.effectDiscard(writeNames).pipe(
	Layer.provideMerge(Scripts.layer.pipe(Layer.provideMerge(host)))
);

const serve = Command.make("serve", {}, () =>
	shown(
		Effect.gen(function* () {
			const required = yield* Effect.flatMap(token, (set) =>
				Effect.mapError(Effect.fromOption(set), () =>
					TokenRequired.make({ key: "GAME_TESTING_TOKEN" })
				)
			);
			const port = yield* Config.Port("GAME_TESTING_PORT").pipe(
				Config.withDefault(7480)
			);
			return yield* Layer.launch(
				serveOn({ port, token: required, version }).pipe(Layer.provide(hosting))
			);
		})
	)
).pipe(
	Command.withDescription(
		"Serves this machine's games to callers on other machines, on 127.0.0.1 only: the commands over a WebSocket at /rpc, which game-testing run uses with GAME_TESTING_URL, and the MCP tools over HTTP at /mcp. It needs GAME_TESTING_TOKEN, and every request must carry it as Authorization: Bearer <token>. The token is as strong as a shell on this machine: whoever holds it can send a script, and the script runs here as you. A tunnel you run gives it reach, such as ssh -L or tailscale serve. GAME_TESTING_PORT sets the port, 7480 unless set."
	)
);

// The host is built inside the command, so a start that fails says why through the command line, on stderr: stdout
// carries MCP's messages only.
const mcp = Command.make("mcp", {}, () =>
	shown(
		Layer.launch(
			McpServer.layerStdio({
				name: serverName,
				version,
				protocols,
				instructions,
				registrations,
			}).pipe(Layer.provide(hosting))
		)
	)
).pipe(
	Command.withDescription(
		"Serves the game as MCP tools over stdio, for an agent on this machine. Its games run in this machine's places."
	)
);

export const cli = Command.make("game-testing").pipe(
	Command.withDescription(
		"Puts Ocarina of Time in a state, drives it, reads the result and keeps a record."
	),
	Command.withSubcommands([
		run,
		plan,
		places,
		search,
		spots,
		runs,
		names,
		mcp,
		serve,
		worker,
		declaration,
	])
);
