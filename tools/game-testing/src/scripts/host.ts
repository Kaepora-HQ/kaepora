/**
 * Where a script's child finds its game. On a machine with the game, that is this machine's host. With
 * `GAME_TESTING_URL`, it is a far machine's `serve`: the script runs here, and only its commands and results cross.
 */
import { Context, Effect, Layer, Option, Redacted } from "effect";
import type { RunId } from "../contract/values.ts";
import { Far } from "../far.ts";
import { HostSocket } from "../host/host.ts";
import { Runs } from "../host/runs.ts";

export class ScriptHost extends Context.Service<
	ScriptHost,
	{
		/** The child's flags that name its host. */
		readonly flags: readonly string[];
		/** What the child's environment holds besides this process's, such as a far machine's token. */
		readonly env: Readonly<Record<string, string>>;
		/** Whether the host kept a record of the run, so `run` names only runs that can be read. */
		readonly recorded: (run: RunId) => Effect.Effect<boolean>;
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
				env: {},
				recorded: (id) =>
					Effect.map(Effect.option(runs.get(id)), Option.isSome),
			});
		})
	);

	/**
	 * The far machine's `serve` at `url`, with the token from `GAME_TESTING_TOKEN`. The scripts run here; only their
	 * commands and results cross, and that machine keeps the records.
	 */
	static readonly layerRemote = (url: string) =>
		Layer.effect(
			ScriptHost,
			Effect.gen(function* () {
				const far = yield* Far;
				return ScriptHost.of({
					flags: ["--host", far.url],
					env: Option.match(far.token, {
						onNone: () => ({}),
						onSome: (secret) => ({
							GAME_TESTING_TOKEN: Redacted.value(secret),
						}),
					}),
					recorded: (id) =>
						far.client.Run({ run: id }).pipe(
							Effect.as(true),
							Effect.orElseSucceed(() => false)
						),
				});
			})
		).pipe(Layer.provide(Far.layer(url)));
}
