/**
 * A host: the process on a machine with the game that runs games for its callers. It serves `GameRpcs` on a Unix
 * socket of its own, `$XDG_RUNTIME_DIR/game-testing/<pid>.sock`, which only its user can open, for the scripts it
 * starts as children. Several hosts on one machine share its places through the places' containers.
 */
import { BunSocketServer } from "@effect/platform-bun";
import { Config, Context, Effect, FileSystem, Layer } from "effect";
import { SecureRandom } from "effect/crypto";
import { RpcSerialization, RpcServer } from "effect/rpc";
import { GameRpcs } from "../contract/rpc.ts";
import { GameData } from "../data/game-data.ts";
import { Launcher } from "../emulator/launcher.ts";
import { Bases } from "../save/bases.ts";
import { Spots } from "../save/spots.ts";
import { StartStates } from "../save/state.ts";
import { Boot } from "./boot.ts";
import { handlers } from "./handlers.ts";
import { Places } from "./places.ts";
import { Runs } from "./runs.ts";
import { Sessions } from "./sessions.ts";

/** The bases, on our own tables and this machine's runs. */
export const bases = Bases.layer.pipe(Layer.provideMerge(Runs.layer));

/** The game's tables, the bases and the start states built on them, on our own tables. */
export const tables = StartStates.layer.pipe(
	Layer.provideMerge(Layer.mergeAll(GameData.layer, Spots.layer, bases))
);

/** Every service a host's commands use, on our own tables. */
export const services = Sessions.layer.pipe(
	Layer.provideMerge(Boot.layer),
	Layer.provideMerge(Layer.mergeAll(Places.layer, Launcher.layer)),
	Layer.provideMerge(tables),
	Layer.provide(SecureRandom.layerWebCrypto)
);

/** Where this host's children reach it. */
export class HostSocket extends Context.Service<
	HostSocket,
	{ readonly path: string }
>()("game-testing/host/host/HostSocket") {
	/** `GameRpcs` on the host's socket, for as long as the layer lives. */
	static readonly layer = Layer.unwrap(
		Effect.gen(function* () {
			const fs = yield* FileSystem.FileSystem;
			const runtime = yield* Config.String("XDG_RUNTIME_DIR");
			const folder = `${runtime}/game-testing`;
			yield* Effect.orDie(
				fs.makeDirectory(folder, { recursive: true, mode: 0o700 })
			);
			const path = `${folder}/${process.pid}.sock`;
			// A file left at this path is a dead process's with this one's id.
			yield* Effect.orDie(fs.remove(path, { force: true }));
			return RpcServer.layer(GameRpcs, { disableFatalDefects: true }).pipe(
				Layer.provide(handlers),
				Layer.provide(RpcServer.layerProtocolSocketServer),
				Layer.provide(BunSocketServer.layer({ path })),
				Layer.provide(RpcSerialization.layerNdjson),
				Layer.merge(Layer.succeed(HostSocket, HostSocket.of({ path })))
			);
		})
	);
}

/** A host that serves its children: every service, and the socket. */
export const host = HostSocket.layer.pipe(Layer.provideMerge(services));
