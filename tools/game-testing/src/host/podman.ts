import { Effect, Stream } from "effect";
import type { ChildProcessSpawner } from "effect/process";
import { ChildProcess } from "effect/process";

export interface Ran {
	readonly code: number;
	readonly stderr: string;
	readonly stdout: string;
}

const text = <E>(stream: Stream.Stream<Uint8Array, E>) =>
	Stream.mkString(Stream.decodeText(stream));

export const podman = (
	spawner: ChildProcessSpawner.ChildProcessSpawner["Service"],
	args: readonly string[]
): Effect.Effect<Ran> =>
	Effect.scoped(
		Effect.gen(function* () {
			const handle = yield* spawner.spawn(ChildProcess.make("podman", args));
			return yield* Effect.all(
				{
					code: handle.exitCode,
					stdout: text(handle.stdout),
					stderr: text(handle.stderr),
				},
				{ concurrency: "unbounded" }
			);
		})
	).pipe(Effect.orDie);
