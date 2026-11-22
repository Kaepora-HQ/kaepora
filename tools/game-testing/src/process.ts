/** The programs the library runs to their end: podman, the type check. */
import { Array as Arr, Effect, String as Str, Stream } from "effect";
import type { ChildProcess, ChildProcessSpawner } from "effect/process";

/** A program run to its end: what it printed, and how it ended. */
export interface Ran {
	/** Its exit status, absent when a signal ended it. */
	readonly code: number | undefined;
	/** How it ended, for a message: `with status 1`, or the signal that ended it. */
	readonly ended: string;
	readonly stderr: string;
	readonly stdout: string;
}

export const text = <E>(stream: Stream.Stream<Uint8Array, E>) =>
	Stream.mkString(Stream.decodeText(stream));

/** The last `count` lines of a text that hold something. */
export const lastLines = (output: string, count: number) =>
	Arr.takeRight(Arr.filter(Str.split(output, "\n"), Str.isNonEmpty), count);

export const ran = (
	spawner: ChildProcessSpawner.ChildProcessSpawner["Service"],
	command: ChildProcess.Command
): Effect.Effect<Ran> =>
	Effect.scoped(
		Effect.gen(function* () {
			const handle = yield* spawner.spawn(command);
			const ending = handle.exitCode.pipe(
				Effect.map((code) => ({ code, ended: `with status ${code}` })),
				Effect.catch((error) =>
					Effect.succeed({
						code: undefined,
						ended: `by ${error.cause instanceof Error ? error.cause.message : error.message}`,
					})
				)
			);
			const [stdout, stderr, how] = yield* Effect.all(
				[text(handle.stdout), text(handle.stderr), ending],
				{ concurrency: "unbounded" }
			);
			return { ...how, stdout, stderr };
		})
	).pipe(Effect.orDie);
