/**
 * Runs a script in a child of this program. A script's code runs only there: the host never imports a script file, so
 * a script that hangs or loops is stopped without taking its host down, and an edited script runs as edited. The
 * child's `Game` is an RPC client of its host over the host's socket; the child never drives the emulator. The script
 * is type-checked first, so a misspelled name fails in seconds, before any game starts.
 *
 * Each wait has one owner. The type check and the child's declaration of itself have limits of their own here. The
 * wait for a place, the image build and the boot's stages have theirs in the host. The script's own limit counts
 * from Link in control.
 */
import type { Cause, Scope } from "effect";
import {
	Array as Arr,
	Context,
	Duration,
	Effect,
	Fiber,
	FileSystem,
	Formatter,
	Layer,
	Option,
	Queue,
	Schema,
	String as Str,
	Stream,
} from "effect";
import { ChildProcess, ChildProcessSpawner } from "effect/process";
import { Outcome } from "../contract/record.ts";
import type { RunId } from "../contract/values.ts";
import { newRunId } from "../contract/values.ts";
import { ScriptFailed } from "../errors.ts";
import { HostSocket } from "../host/host.ts";
import { Runs } from "../host/runs.ts";
import { lastLines, ran, text } from "../process.ts";
import { Declaration } from "../script.ts";

/** The hidden commands a child runs as: a script's run, and only its declaration. */
export const workerCommand = "script-worker";
export const declarationCommand = "script-declaration";

/** The package's folder with no `..` in it: the type check finds a script by its path's text. */
export const packageRoot = Str.slice(
	0,
	-1
)(Bun.fileURLToPath(new URL("../..", import.meta.url)));

/** Where a script sent inline, by `run_script`, is written for its run. Git ignores the folder. */
export const inlineFolder = `${packageRoot}/.cache/inline`;

/** A script with no limit of its own is stopped after this long with Link in control. */
const scriptLimit = Duration.minutes(5);

/** The type check took 1.1 s with every name of the game on the main machine. */
const typecheckLimit = Duration.minutes(2);

/** From the child's start to its script's declaration: Bun's start and the script's import. */
const declarationLimit = Duration.seconds(15);

/** How long a child stopped at a limit has to close its game before it is killed. */
const stopGrace = Duration.seconds(5);

const tailCount = 20;

/** What a child says as it goes, one line each: what its script declares, that Link can move, and how it ended. */
export const ScriptEvent = Schema.Union([
	Schema.TaggedStruct("declared", { declaration: Declaration }),
	Schema.TaggedStruct("started", {}),
	Schema.TaggedStruct("ended", { outcome: Outcome }),
]);
export type ScriptEvent = typeof ScriptEvent.Type;
export const EventLine = Schema.fromJsonString(ScriptEvent);

/** What a run of a script gives: its outcome, and its run's name when the host kept a record of it. */
export interface ScriptRun {
	readonly outcome: Outcome;
	readonly run?: RunId;
}

const options = {
	cwd: packageRoot,
	extendEnv: true,
	forceKillAfter: stopGrace,
};

const tail = (output: string) => Arr.join(lastLines(output, tailCount), "\n");

/** A child of this program: the next event it prints, and why it is gone once its output has ended. */
interface Child {
	readonly gone: Effect.Effect<ScriptFailed>;
	readonly next: Effect.Effect<ScriptEvent, Cause.Done>;
}

const childOf = Effect.fnUntraced(function* (
	spawner: ChildProcessSpawner.ChildProcessSpawner["Service"],
	args: readonly string[]
): Effect.fn.Return<Child, never, Scope.Scope> {
	const handle = yield* Effect.orDie(
		spawner.spawn(
			ChildProcess.make(
				process.execPath,
				[`${packageRoot}/src/main.ts`, ...args],
				options
			)
		)
	);
	const events = yield* Queue.unbounded<ScriptEvent, Cause.Done>();
	// A line that is no event, such as one a script logs, is not the runner's to read.
	yield* Stream.decodeText(handle.stdout).pipe(
		Stream.splitLines,
		Stream.runForEach((line) =>
			Effect.flatMap(
				Effect.option(Schema.decodeEffect(EventLine)(line)),
				Option.match({
					onNone: () => Effect.void,
					onSome: (event) => Queue.offer(events, event),
				})
			)
		),
		Effect.ignore,
		Effect.ensuring(Queue.end(events)),
		Effect.forkScoped
	);
	const stderr = yield* Effect.forkScoped(
		Effect.orElseSucceed(text(handle.stderr), () => "")
	);
	const ending = handle.exitCode.pipe(
		Effect.map((code) => `with status ${code}`),
		Effect.catch((error) =>
			Effect.succeed(
				`by ${error.cause instanceof Error ? error.cause.message : error.message}`
			)
		)
	);
	const child: Child = {
		next: Queue.take(events),
		gone: Effect.gen(function* () {
			const how = yield* ending;
			const said = yield* Fiber.join(stderr);
			return ScriptFailed.make({
				reason: "exited",
				detail: `its process ended ${how} before it finished: ${tail(said)}`,
			});
		}),
	};
	return child;
});

/** The child's next event; a child whose output has ended is gone. */
const nextOf = (child: Child) =>
	child.next.pipe(
		Effect.catchTag("Done", () => Effect.flatMap(child.gone, Effect.fail))
	);

/** Fails with `limit` when `effect` takes longer than `duration`, saying what was late. */
const within = <A, E, R>(
	effect: Effect.Effect<A, E, R>,
	duration: Duration.Duration,
	late: (limit: string) => string
) =>
	Effect.timeoutOrElse(effect, {
		duration,
		orElse: () =>
			Effect.fail(
				ScriptFailed.make({
					reason: "limit",
					detail: late(Duration.format(duration)),
				})
			),
	});

/** The package's type check over the script and the generated names, in a settings file of its own. */
const typecheck = Effect.fn("Scripts.typecheck")(function* (
	spawner: ChildProcessSpawner.ChildProcessSpawner["Service"],
	fs: FileSystem.FileSystem,
	script: string
) {
	// tsc finds a file in its settings by the text of its path, which then holds no `..`.
	const file = yield* Effect.orDie(fs.realPath(script));
	const folder = `${packageRoot}/.cache/typecheck`;
	yield* Effect.orDie(fs.makeDirectory(folder, { recursive: true }));
	const settings = yield* Effect.orDie(
		fs.makeTempFileScoped({ directory: folder, suffix: ".json" })
	);
	yield* Effect.orDie(
		fs.writeFileString(
			settings,
			Formatter.formatJson({
				extends: `${packageRoot}/tsconfig.json`,
				compilerOptions: { incremental: false, tsBuildInfoFile: null },
				include: [file, `${packageRoot}/generated/**/*.ts`],
			})
		)
	);
	const checked = yield* within(
		ran(
			spawner,
			// Its file starts with `#!/usr/bin/env node`, and a machine may have Bun alone.
			ChildProcess.make(
				process.execPath,
				[
					`${packageRoot}/node_modules/.bin/effect-tsgo`,
					"tsc",
					"-p",
					settings,
					"--noEmit",
				],
				options
			)
		),
		typecheckLimit,
		(limit) => `the type check did not end within ${limit} and was stopped`
	);
	if (checked.code !== 0) {
		return yield* ScriptFailed.make({
			reason: "typecheck",
			detail: tail(`${checked.stdout}\n${checked.stderr}`),
		});
	}
}, Effect.scoped);

/** The first thing a child says is its script's declaration. */
const declared = (child: Child) =>
	within(
		nextOf(child).pipe(
			Effect.flatMap((event) =>
				event._tag === "declared"
					? Effect.succeed(event.declaration)
					: Effect.die(new Error(`a script's child said ${event._tag} first`))
			)
		),
		declarationLimit,
		(limit) => `it did not declare itself within ${limit} and was stopped`
	);

/** A script's declaration, from a child, after its type check: the host never imports a script. */
export const declarationOf = Effect.fn("Scripts.declarationOf")(function* (
	script: string
) {
	const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
	const fs = yield* FileSystem.FileSystem;
	yield* typecheck(spawner, fs, script);
	return yield* Effect.scoped(
		Effect.flatMap(childOf(spawner, [declarationCommand, script]), declared)
	);
});

export class Scripts extends Context.Service<
	Scripts,
	{
		/**
		 * Type-checks a script, then runs it to its end in a child, as the run `run`, a new one unless given. A script
		 * that declares it hands over to a person is skipped.
		 */
		readonly run: (
			script: string,
			options?: {
				readonly attemptOf?: RunId | undefined;
				readonly run?: RunId | undefined;
			}
		) => Effect.Effect<ScriptRun, ScriptFailed>;
	}
>()("game-testing/scripts/runner/Scripts") {
	static readonly layer = Layer.effect(
		Scripts,
		Effect.gen(function* () {
			const socket = yield* HostSocket;
			const runs = yield* Runs;
			const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
			const fs = yield* FileSystem.FileSystem;
			/** Whether the host kept a record of the run, so `run` names only runs that `runs` can read. */
			const kept = (id: RunId) =>
				Effect.map(Effect.option(runs.get(id)), Option.isSome);

			const run = Effect.fn("Scripts.run")(function* (
				script: string,
				given?: {
					readonly attemptOf?: RunId | undefined;
					readonly run?: RunId | undefined;
				}
			) {
				yield* typecheck(spawner, fs, script);
				const id = given?.run ?? (yield* newRunId);
				const outcome = yield* Effect.scoped(
					Effect.gen(function* () {
						const child = yield* childOf(spawner, [
							workerCommand,
							"--socket",
							socket.path,
							"--run",
							id,
							...(given?.attemptOf === undefined
								? []
								: ["--attempt-of", given.attemptOf]),
							script,
						]);
						const declaration = yield* declared(child);
						// Nothing here limits the start: the wait for a place, the image build and the boot have their own.
						const next = yield* nextOf(child);
						if (next._tag === "ended") {
							return next.outcome;
						}
						const last = yield* within(
							nextOf(child),
							declaration.limit ?? scriptLimit,
							(limit) => `it ran past its limit of ${limit} and was stopped`
						);
						return last._tag === "ended"
							? last.outcome
							: yield* Effect.die(
									new Error(`a script's child said ${last._tag} once started`)
								);
					})
				).pipe(
					Effect.catchTag("ScriptFailed", (failed) =>
						Effect.flatMap(kept(id), (recorded) =>
							Effect.fail(
								recorded
									? ScriptFailed.make({
											reason: failed.reason,
											detail: failed.detail,
											run: id,
										})
									: failed
							)
						)
					)
				);
				const named = yield* kept(id);
				const finished: ScriptRun = named ? { run: id, outcome } : { outcome };
				return finished;
			});

			return Scripts.of({ run });
		})
	);
}
