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
	Schema,
	String as Str,
} from "effect";
import { McpServer } from "effect/ai";
import { Argument, CliError, Command, Flag } from "effect/cli";
import type { Outcome } from "./contract/record.ts";
import type { StatePlan } from "./contract/rpc.ts";
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

/** A script's declared state, after its type check against the game's names, `gameNames`, or a state written as JSON. */
const stateIn = Effect.fnUntraced(function* (file: string, gameNames: string) {
	if (!Str.endsWith(".json")(file)) {
		const declaration = yield* declarationOf(file, gameNames);
		return declaration.state;
	}
	const fs = yield* FileSystem.FileSystem;
	const text = yield* Effect.orDie(fs.readFileString(file));
	return yield* Effect.orDie(
		Schema.decodeEffect(Schema.fromJsonString(StartState))(text)
	);
});

/** A plan as `plan` prints it: the base, then each value it writes, then each it leaves alone. */
const printPlan = (planned: StatePlan) =>
	Effect.gen(function* () {
		yield* Console.log(`From ${planned.base.name}: ${planned.base.about}`);
		for (const edit of planned.edits) {
			const name = edit._tag === "value" ? edit.name : `key ${edit.key}`;
			yield* Console.log(
				`${name} = ${String(edit.to)}    was ${String(edit.base)}, from ${edit.from}`
			);
		}
		for (const left of planned.leftAlone) {
			yield* Console.log(
				`${left.name} left alone: ${left.from} names it, and it is not the place's own`
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
						const state = yield* stateIn(file, namesHere);
						const { base, edits, leftAlone } = yield* states.plan(state);
						const planned: StatePlan = { base, edits, leftAlone };
						return planned;
					}).pipe(Effect.provide(tables)),
					Effect.fnUntraced(function* (far) {
						const names = yield* writeFarNames(far);
						const state = yield* stateIn(file, names);
						return yield* reached(far.url, far.client.Plan({ state }));
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
						return yield* searchNames(data, own, text, {
							limit,
							kind: Option.getOrUndefined(kind),
						});
					}).pipe(Effect.provide(GameData.layer)),
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
		runs,
		names,
		mcp,
		serve,
		worker,
		declaration,
	])
);
