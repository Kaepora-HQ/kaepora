/**
 * Where a script's child finds its game. On a machine with the game, that is this machine's host. With
 * `GAME_TESTING_URL`, it is a far machine's `serve`: the script runs here, and only its commands and results cross.
 * Either way the run's record, pictures and saves end up in a folder on this machine.
 */
import {
	Array as Arr,
	Context,
	Duration,
	Effect,
	Layer,
	Option,
	Redacted,
	Schedule,
} from "effect";
import type { RunId } from "../contract/values.ts";
import { namesHere } from "../data/declarations.ts";
import { CopyFailed } from "../errors.ts";
import { Far, writeFarNames } from "../far.ts";
import { HostSocket } from "../host/host.ts";
import { Runs } from "../host/runs.ts";

/** How long a far run's record may still say running once its script is gone, and how often it is read meanwhile. */
const finalLimit = Duration.minutes(1);
const lookAgain = Duration.millis(500);

export class ScriptHost extends Context.Service<
	ScriptHost,
	{
		/** The child's flags that name its host. */
		readonly flags: readonly string[];
		/** What a script's type check includes for the host's game's names. */
		readonly names: string;
		/** What the child's environment holds besides this process's, such as a far machine's token. */
		readonly env: Readonly<Record<string, string>>;
		/**
		 * Whether the host kept a record of the run, so `run` names only runs that can be read. A kept record is then in
		 * this machine's runs, with its pictures, or the copy failed and says why.
		 */
		readonly recorded: (run: RunId) => Effect.Effect<boolean, CopyFailed>;
	}
>()("game-testing/scripts/host/ScriptHost") {
	/** This machine's host, over its socket. */
	static readonly layer = Layer.effect(
		ScriptHost,
		Effect.gen(function* () {
			const socket = yield* HostSocket;
			const runs = yield* Runs;
			return ScriptHost.of({
				flags: ["--host", socket.path],
				names: namesHere,
				env: {},
				recorded: (id) =>
					Effect.map(Effect.option(runs.get(id)), Option.isSome),
			});
		})
	);

	/**
	 * The far machine's `serve` at `url`, with the token from `GAME_TESTING_TOKEN`. The scripts run here; only their
	 * commands and results cross. Its names are written here first, so a script's type check reads the far game's.
	 * That machine keeps the records, and each run's record, pictures and saves are copied here.
	 */
	static readonly layerRemote = (url: string) =>
		Layer.effect(
			ScriptHost,
			Effect.gen(function* () {
				const far = yield* Far;
				const runs = yield* Runs;
				const names = yield* writeFarNames(far);
				const failed = (run: RunId, reason: string) =>
					Effect.fail(CopyFailed.make({ run, reason }));
				const lost = (run: RunId) =>
					failed(run, "the connection to the game machine was lost");
				/** The record once its outcome is written: the game's machine writes it last, after its place is given back. */
				const final = (run: RunId) =>
					far.client.Run({ run }).pipe(
						Effect.repeat({
							schedule: Schedule.spaced(lookAgain),
							until: (record) => record.outcome._tag !== "running",
						}),
						Effect.timeoutOrElse({
							duration: finalLimit,
							orElse: () =>
								failed(
									run,
									`its record still said running after ${Duration.format(finalLimit)}`
								),
						})
					);
				const copied = Effect.fnUntraced(function* (run: RunId) {
					const record = yield* final(run);
					const files = yield* Effect.forEach(
						[
							...Arr.flatMap(record.steps, (step) => step.pictures ?? []),
							...Arr.flatMap(record.saves ?? [], (save) => [
								save.progress.path,
								save.caption.path,
							]),
						],
						(path) =>
							far.client.RunFile({ run, path }).pipe(
								Effect.map((bytes) => ({ path, bytes })),
								Effect.catchTag("RunNotFound", () =>
									failed(run, `the game machine has no file ${path}`)
								)
							),
						{ concurrency: 1 }
					);
					yield* runs.copy(record, files);
				});
				return ScriptHost.of({
					flags: ["--host", far.url],
					names,
					env: Option.match(far.token, {
						onNone: () => ({}),
						onSome: (secret) => ({
							GAME_TESTING_TOKEN: Redacted.value(secret),
						}),
					}),
					recorded: (run) =>
						copied(run).pipe(
							Effect.as(true),
							Effect.catchTags({
								RunNotFound: () => Effect.succeed(false),
								RpcClientError: () => lost(run),
							})
						),
				});
			})
		).pipe(Layer.provide(Far.layer(url)));
}
