/**
 * The command line. `run` is a host: it serves the scripts it starts as children. The other commands read the game's
 * tables, the places or the records on this machine, and each builds only what it reads, on the platform and our own
 * tables, which the entry point gives.
 */
import {
	Array as Arr,
	Console,
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
import { StartState } from "./contract/state.ts";
import { NameKind, RunId } from "./contract/values.ts";
import { writeNames } from "./data/declarations.ts";
import { GameData } from "./data/game-data.ts";
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

/** The package's version, which `mcp` and `serve` name to their clients. */
export const version = "0.0.0";

/** A handler's failure as the command line shows it: its message. */
const shown = <A, E extends { readonly message: string }, R>(
	effect: Effect.Effect<A, E, R>
) =>
	Effect.mapError(effect, (error) =>
		CliError.UserError.make({ cause: error, userMessage: error.message })
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

const report = (script: string, ran: ScriptRun) =>
	Effect.forEach(
		[
			Arr.join(
				Arr.filter([ran.outcome._tag, script, ran.run ?? ""], Str.isNonEmpty),
				"  "
			),
			...Arr.map(why(ran.outcome), (line) => `    ${line}`),
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
			const runner = yield* Scripts;
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
					Effect.tap((ran) => report(script, ran))
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
		})
).pipe(
	Command.withDescription(
		"Runs scripts against this machine's game, each in a place of its own, and keeps a record of each run."
	),
	Command.provide(Scripts.layer.pipe(Layer.provideMerge(host)))
);

/** A script's declared state, or a state written as JSON. */
const stateIn = Effect.fnUntraced(function* (file: string) {
	if (!Str.endsWith(".json")(file)) {
		const declaration = yield* declarationOf(file);
		return declaration.state;
	}
	const fs = yield* FileSystem.FileSystem;
	const text = yield* Effect.orDie(fs.readFileString(file));
	return yield* Effect.orDie(
		Schema.decodeEffect(Schema.fromJsonString(StartState))(text)
	);
});

const plan = Command.make(
	"plan",
	{ file: Argument.File("script-or-state", { mustExist: true }) },
	({ file }) =>
		shown(
			Effect.gen(function* () {
				const states = yield* StartStates;
				const state = yield* stateIn(file);
				const planned = yield* states.plan(state);
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
			})
		)
).pipe(
	Command.withDescription(
		"Prints every value a start state writes, what the base held and the word it came from, without a game."
	),
	Command.provide(tables)
);

const places = Command.make("places", {}, () =>
	Effect.gen(function* () {
		const machine = yield* Places;
		for (const place of yield* machine.list) {
			yield* Console.log(
				place.holder === undefined
					? `${place.place}  free`
					: `${place.place}  held by ${place.holder.owner} since ${place.holder.since}, run ${place.holder.run}`
			);
		}
	})
).pipe(
	Command.withDescription("Lists this machine's places and who holds each."),
	Command.provide(Places.layer)
);

const search = Command.make(
	"search",
	{
		text: Argument.String("text"),
		kind: Flag.Literals("kind", NameKind.literals).pipe(Flag.optional),
		limit: Flag.Int("limit").pipe(Flag.withDefault(40)),
	},
	({ text, kind, limit }) =>
		Effect.gen(function* () {
			const data = yield* GameData;
			const own = yield* OwnTables;
			const found = yield* searchNames(data, own, text, {
				limit,
				kind: Option.getOrUndefined(kind),
			});
			for (const match of found) {
				yield* Console.log(`${match.kind}  ${match.name}  ${match.about}`);
			}
		})
).pipe(
	Command.withDescription(
		"Finds every name a state or a script takes that contains the text: saved values, items, quests, scenes, landings, event settings, checks, spots and bases."
	),
	Command.provide(GameData.layer)
);

const runs = Command.make(
	"runs",
	{
		script: Flag.String("script").pipe(Flag.optional),
		limit: Flag.Int("limit").pipe(Flag.withDefault(20)),
	},
	({ script, limit }) =>
		Effect.gen(function* () {
			const records = yield* Runs;
			for (const summary of yield* records.list({
				script: Option.getOrUndefined(script),
				limit,
			})) {
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
		})
).pipe(
	Command.withDescription("Lists past runs on this machine, newest first."),
	Command.provide(Runs.layer)
);

const names = Command.make("names", {}, () => writeNames).pipe(
	Command.withDescription(
		"Writes generated/names.d.ts from the game's tables, so scripts' names and values are checked as they are typed."
	),
	Command.provide(GameData.layer)
);

const worker = Command.make(
	workerCommand,
	{
		socket: Flag.String("socket"),
		run: Flag.String("run").pipe(Flag.withSchema(RunId)),
		attemptOf: Flag.String("attempt-of").pipe(
			Flag.withSchema(RunId),
			Flag.optional
		),
		script: Argument.File("script", { mustExist: true }),
	},
	({ socket, script, run: id, attemptOf }) =>
		work({
			socket,
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
		worker,
		declaration,
	])
);
