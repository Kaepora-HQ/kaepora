import {
	Context,
	Duration,
	Effect,
	Exit,
	Fiber,
	FileSystem,
	Layer,
	Schedule,
	Scope,
	String as Str,
} from "effect";
import { ChildProcessSpawner } from "effect/process";
import { LaunchFailed } from "../errors.ts";
import type { Place } from "../host/places.ts";
import { placeFiles } from "../host/places.ts";
import { podman } from "../host/podman.ts";
import { lastLines } from "../process.ts";
import type { Emulator, WireLog } from "./emulator.ts";
import { connect } from "./emulator.ts";

/** The debug server answered after 1 to 22 seconds on the main machine, in a place and outside one. */
const serverLimit = Duration.seconds(60);

/** The first frame came 5 to 6 seconds after the start in a place, and 130 outside one with other work running. */
const frameLimit = Duration.minutes(5);

const lookAgain = Duration.millis(500);

const logLines = 20;

export class Launcher extends Context.Service<
	Launcher,
	{
		/**
		 * Writes the place's settings, starts its container and gives the game's debug connection once the first frame is
		 * presented. The connection closes with the scope; the place's own scope then stops the container.
		 */
		readonly launch: (
			place: Place,
			options: { readonly wire: WireLog }
		) => Effect.Effect<Emulator, LaunchFailed, Scope.Scope>;
	}
>()("game-testing/emulator/launcher") {
	static readonly layer = Layer.effect(
		Launcher,
		Effect.gen(function* () {
			const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
			const fs = yield* FileSystem.FileSystem;
			const settings = yield* Effect.orDie(
				fs.readFileString(`${placeFiles}/qt-config.ini`)
			);

			const tail = (path: string) =>
				fs.readFileString(path).pipe(
					Effect.map((text) => lastLines(text, logLines)),
					Effect.catchReason("PlatformError", "NotFound", () =>
						Effect.succeed([])
					),
					Effect.orDie
				);

			const launch = Effect.fn("Launcher.launch")(function* (
				place: Place,
				options: { readonly wire: WireLog }
			) {
				const scope = yield* Effect.scope;
				const failed = (stage: LaunchFailed["stage"], detail: string) =>
					Effect.flatMap(tail(`${place.folder}/place.log`), (logTail) =>
						Effect.fail(
							LaunchFailed.make({ place: place.id, stage, detail, logTail })
						)
					);

				yield* Effect.orDie(
					fs.writeFileString(
						`${place.folder}/config/lichii/qt-config.ini`,
						settings
					)
				);
				const started = yield* podman(spawner, ["start", place.container]);
				if (started.code !== 0) {
					return yield* failed("spawn", Str.trim(started.stderr));
				}
				const exited = yield* Effect.forkIn(
					Effect.map(podman(spawner, ["wait", place.container]), (ran) =>
						ran.code === 0
							? `the container exited with status ${Str.trim(ran.stdout)}`
							: `podman wait failed: ${Str.trim(ran.stderr)}`
					),
					scope
				);
				const before = <A, E extends { readonly message: string }>(
					stage: LaunchFailed["stage"],
					limit: Duration.Duration,
					signal: Effect.Effect<A, E>
				) =>
					signal.pipe(
						Effect.catch((error) => failed(stage, error.message)),
						Effect.timeoutOrElse({
							duration: limit,
							orElse: () =>
								failed(
									stage,
									`it gave no answer within ${Duration.format(limit)}`
								),
						}),
						Effect.raceFirst(
							Effect.flatMap(Fiber.join(exited), (ended) =>
								failed(stage, ended)
							)
						)
					);

				const attempt = Effect.gen(function* () {
					const connection = yield* Scope.fork(scope);
					return yield* connect({
						path: `${place.folder}/debug.sock`,
						wire: options.wire,
						logFileTail: tail(`${place.folder}/data/lichii/log/eden_log.txt`),
					}).pipe(
						Scope.provide(connection),
						Effect.onError(() => Scope.close(connection, Exit.void))
					);
				});
				const emulator = yield* before(
					"debug-server",
					serverLimit,
					attempt.pipe(
						Effect.retry({
							schedule: Schedule.spaced(lookAgain),
							while: (error) => error.reason === "connect",
						})
					)
				);
				yield* before(
					"first-frame",
					frameLimit,
					emulator.status.pipe(
						Effect.repeat({
							schedule: Schedule.spaced(lookAgain),
							until: (status) => status.presents > 0,
						})
					)
				);
				return emulator;
			});

			return Launcher.of({ launch });
		})
	);
}
