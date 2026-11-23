import { BunFileSystem } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	ConfigProvider,
	Deferred,
	Duration,
	Effect,
	Exit,
	Fiber,
	FileSystem,
	Layer,
	Option,
	Order,
	String as Str,
} from "effect";
import { UserDirectories } from "effect/persistence";
import { TestClock } from "effect/testing";
import { PlaceId, RunId, SessionId } from "../src/contract/values.ts";
import { Launcher } from "../src/emulator/launcher.ts";
import {
	ImageBuildFailed,
	LaunchFailed,
	NoFreePlace,
	NoGraphicsCard,
} from "../src/errors.ts";
import { Places } from "../src/host/places.ts";
import { fakeLichii } from "./fake-lichii.ts";
import type { Answer, FakeContainer, FakePodman } from "./fake-podman.ts";
import { fakePodman } from "./fake-podman.ts";

const gpu = "nvidia.com/gpu=all";

const holding = {
	session: SessionId.make("session-1"),
	run: RunId.make("20261122-010000-abcd"),
	owner: "scripts/slingshot-chest.ts",
	waitForPlace: Duration.seconds(10),
};

const gone = {
	// Linux gives no process this id: ids stay below pid_max, which is at most 4194304.
	"game-testing.holder": "4194304:1",
	"game-testing.session": "session-0",
	"game-testing.run": "20261121-230000-wxyz",
	"game-testing.owner": "a host that was killed",
	"game-testing.since": "2026-11-22T04:00:00Z",
};

const machine = (
	podman: FakePodman,
	options: {
		readonly places: number;
		readonly card?: string;
		readonly lichii?: string;
	}
) =>
	Effect.gen(function* () {
		const fs = yield* FileSystem.FileSystem;
		const root = yield* fs.makeTempDirectoryScoped();
		yield* fs.writeFileString(`${root}/oot.nup`, "");
		return Layer.mergeAll(Places.layer, Launcher.layer).pipe(
			Layer.provide([podman.layer, UserDirectories.layer]),
			Layer.provide(
				ConfigProvider.layer(
					ConfigProvider.fromUnknown({
						GAME_TESTING_PLACES: String(options.places),
						GAME_TESTING_CARD: options.card ?? "nvidia",
						GAME_TESTING_LICHII: options.lichii ?? `${root}/lichii`,
						GAME_TESTING_GAME_FILE: `${root}/oot.nup`,
						GAME_TESTING_DATA: `${root}/data`,
					})
				)
			)
		);
	});

const take = Effect.gen(function* () {
	const places = yield* Places;
	return yield* places.take(holding);
});

const launched = Effect.gen(function* () {
	const place = yield* take;
	const launcher = yield* Launcher;
	return yield* launcher.launch(place, {
		wire: { line: () => Effect.void },
		textSpeed: "game",
	});
});

/**
 * Moves the test clock on a second at a time, letting real file reads and writes finish in between, so a wait that
 * starts after them still meets its limit.
 */
const clockRuns = Effect.forever(
	TestClock.adjust("1 second").pipe(
		Effect.andThen(TestClock.withLive(Effect.sleep("5 millis")))
	)
);

const placeFolder = (container: FakeContainer) =>
	Str.replace(
		":/place",
		""
	)(Option.getOrThrow(Arr.findFirst(container.args, Str.endsWith(":/place"))));

const flagged = (container: FakeContainer, name: string) =>
	Arr.flatMap(container.args, (arg, at) =>
		arg === name ? Arr.fromOption(Arr.get(container.args, at + 1)) : []
	);

const answering =
	(verb: string, answer: Answer) =>
	(args: readonly string[]): Option.Option<Answer> =>
		Arr.head(args).pipe(
			Option.filter((first) => first === verb),
			Option.as(answer)
		);

const created = (podman: FakePodman) =>
	Effect.map(podman.containers, (held) =>
		Option.getOrThrow(Arr.head([...held.values()]))
	);

describe("places", () => {
	it.effect(
		"takers at once never share a place, and one past the last waits its limit",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({ devices: [gpu] });
				const taking = yield* Effect.forkChild(
					Effect.all(
						Arr.makeBy(3, () => Effect.result(take)),
						{ concurrency: "unbounded" }
					).pipe(Effect.provide(yield* machine(podman, { places: 2 })))
				);
				const taken = yield* Fiber.join(taking).pipe(
					Effect.raceFirst(clockRuns)
				);
				const [places, refused] = Arr.separate(taken);
				expect(
					Arr.sort(
						Arr.map(places, (place) => place.id),
						Order.Number
					)
				).toEqual([PlaceId.make(1), PlaceId.make(2)]);
				expect(refused).toEqual([
					NoFreePlace.make({ places: 2, waited: Duration.seconds(10) }),
				]);
				expect((yield* podman.containers).size).toBe(2);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect("a wait of zero still looks once, and takes a free place", () =>
		Effect.gen(function* () {
			const podman = yield* fakePodman({ devices: [gpu] });
			const place = yield* Effect.gen(function* () {
				const places = yield* Places;
				return yield* places.take({ ...holding, waitForPlace: Duration.zero });
			}).pipe(Effect.provide(yield* machine(podman, { places: 1 })));
			expect(place.id).toBe(PlaceId.make(1));
		}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a place whose holder has gone has its container removed and is taken",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({ devices: [gpu] });
				const left = yield* podman.leave("game-testing-place-1", {
					"game-testing.place": "1",
					...gone,
				});
				const place = yield* take.pipe(
					Effect.provide(yield* machine(podman, { places: 1 }))
				);
				expect(place.id).toBe(PlaceId.make(1));
				expect(yield* podman.removed).toEqual([{ id: left.id, grace: 20 }]);
				expect((yield* podman.containers).get("game-testing-place-1")?.id).toBe(
					place.container
				);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a removal podman refuses fails the take with podman's reason, once",
		() =>
			Effect.gen(function* () {
				const refusal =
					"Error: cannot remove container container-1 as it could not be stopped: given PID did not die within timeout";
				const podman = yield* fakePodman({
					devices: [gpu],
					answer: answering("rm", { code: 125, stderr: `${refusal}\n` }),
				});
				const left = yield* podman.leave("game-testing-place-1", {
					"game-testing.place": "1",
					...gone,
				});
				const failed = yield* Effect.flip(
					take.pipe(Effect.provide(yield* machine(podman, { places: 1 })))
				);
				expect(failed).toEqual(
					LaunchFailed.make({
						place: PlaceId.make(1),
						stage: "spawn",
						detail: `podman rm of ${left.id} failed: ${refusal}`,
						logTail: [],
					})
				);
				const removals = Arr.filter(
					yield* podman.calls,
					(args) => args[0] === "rm"
				);
				expect(removals.length).toBe(1);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a removal podman refuses when the scope closes fails the close with podman's reason",
		() =>
			Effect.gen(function* () {
				const refusal =
					"Error: cannot remove container: it could not be stopped";
				const podman = yield* fakePodman({
					devices: [gpu],
					answer: answering("rm", { code: 125, stderr: `${refusal}\n` }),
				});
				const closed = yield* Effect.exit(
					Effect.scoped(take).pipe(
						Effect.provide(yield* machine(podman, { places: 1 }))
					)
				);
				expect(Exit.isFailure(closed)).toBe(true);
				expect(String(Exit.isFailure(closed) ? closed.cause : "")).toContain(
					refusal
				);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a name podman calls in use in either of its two ways reads as held",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({
					devices: [gpu],
					answer: (args) =>
						args[0] === "create" && Arr.contains(args, "game-testing-place-1")
							? Option.some({
									code: 125,
									stderr:
										'Error: name "game-testing-place-1" is in use: container already exists\n',
								})
							: Option.none(),
				});
				const place = yield* take.pipe(
					Effect.provide(yield* machine(podman, { places: 2 }))
				);
				expect(place.id).toBe(PlaceId.make(2));
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a held place keeps its holder's socket open, and closing the scope closes it",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({ devices: [gpu] });
				const ended = yield* Deferred.make<void>();
				yield* Effect.scoped(
					Effect.gen(function* () {
						const place = yield* take;
						yield* Effect.promise(() =>
							Bun.connect({
								unix: `${place.folder}/holder.sock`,
								socket: {
									data: () => undefined,
									close: () => {
										Deferred.doneUnsafe(ended, Exit.void);
									},
								},
							})
						);
						expect(yield* Deferred.isDone(ended)).toBe(false);
					}).pipe(Effect.provide(yield* machine(podman, { places: 1 })))
				);
				yield* Deferred.await(ended);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a place has no network, its own folder writable, Lichii and the game read-only, and two hours at most",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({ devices: [gpu] });
				const place = yield* take.pipe(
					Effect.provide(yield* machine(podman, { places: 1 }))
				);
				const container = yield* created(podman);
				const root = Str.replace("/data/places/1", "")(place.folder);
				expect(flagged(container, "--network")).toEqual(["none"]);
				expect(flagged(container, "--volume")).toEqual([
					`${place.folder}:/place`,
					`${root}/lichii:/opt/lichii:ro`,
					`${root}/oot.nup:/game/oot.nup:ro`,
				]);
				expect(flagged(container, "--timeout")).toEqual(["7200"]);
				expect(flagged(container, "--device")).toEqual([gpu]);
				expect(Arr.some(container.args, Str.startsWith("--publish"))).toBe(
					false
				);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"an AMD card is passed in as /dev/dri, with the user's groups",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({ devices: [] });
				yield* take.pipe(
					Effect.provide(yield* machine(podman, { places: 1, card: "amd" }))
				);
				const container = yield* created(podman);
				expect(flagged(container, "--device")).toEqual(["/dev/dri"]);
				expect(flagged(container, "--group-add")).toEqual(["keep-groups"]);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a machine whose NVIDIA card podman cannot reach starts nothing and says so",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({ devices: ["nvidia.com/gpu=0"] });
				const refused = yield* Effect.flip(
					take.pipe(Effect.provide(yield* machine(podman, { places: 1 })))
				);
				expect(refused).toEqual(NoGraphicsCard.make({}));
				expect((yield* podman.containers).size).toBe(0);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a Lichii folder that is not an absolute path with no colon is refused where it is read",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({ devices: [gpu] });
				const refused = yield* Effect.flip(
					take.pipe(
						Effect.provide(
							yield* machine(podman, { places: 1, lichii: "lichii-366" })
						)
					)
				);
				expect(refused._tag).toBe("ConfigError");
				expect(String(refused)).toContain("GAME_TESTING_LICHII");
				expect(yield* podman.calls).toEqual([]);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"an image that fails to build fails the take, and the next take builds it again",
		() =>
			Effect.gen(function* () {
				let builds = 0;
				const podman = yield* fakePodman({
					devices: [gpu],
					answer: (args) => {
						if (args[0] === "image") {
							return Option.some({ code: 1 });
						}
						if (args[0] === "build") {
							builds += 1;
							return Option.some(
								builds === 1
									? { code: 100, stderr: "E: Unable to fetch some archives\n" }
									: { code: 0 }
							);
						}
						return Option.none();
					},
				});
				const layer = yield* machine(podman, { places: 1 });
				const places = yield* Effect.provide(
					Effect.gen(function* () {
						const failed = yield* Effect.flip(Effect.scoped(take));
						const place = yield* Effect.scoped(take);
						return { failed, place };
					}),
					layer
				);
				expect(places.failed).toBeInstanceOf(ImageBuildFailed);
				expect(places.failed.message).toContain(
					"E: Unable to fetch some archives"
				);
				expect(places.place.id).toBe(PlaceId.make(1));
				expect(builds).toBe(2);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"closing the scope that holds a place stops its container, giving it 20 seconds",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({ devices: [gpu] });
				const place = yield* Effect.scoped(take).pipe(
					Effect.provide(yield* machine(podman, { places: 1 }))
				);
				expect(yield* podman.removed).toEqual([
					{ id: place.container, grace: 20 },
				]);
				expect((yield* podman.containers).size).toBe(0);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);
});

describe("the launcher", () => {
	it.effect(
		"a launch gives the debug connection once a frame is presented, with the settings written before the start",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const written = yield* Deferred.make<string>();
				const podman = yield* fakePodman({
					devices: [gpu],
					onStart: (container) =>
						Effect.scoped(
							Effect.gen(function* () {
								const folder = placeFolder(container);
								yield* Deferred.succeed(
									written,
									yield* fs.readFileString(
										`${folder}/config/lichii/qt-config.ini`
									)
								);
								yield* fakeLichii({
									path: `${folder}/debug.sock`,
									presents: 3,
								});
								yield* Deferred.await(container.exit);
							})
						).pipe(Effect.orDie),
				});
				const emulator = yield* launched.pipe(
					Effect.raceFirst(clockRuns),
					Effect.provide(yield* machine(podman, { places: 1 }))
				);
				expect((yield* emulator.status).presents).toBe(3);
				expect(yield* Deferred.await(written)).toContain("output_engine=3");
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a container that exits before its debug server answers fails at debug-server with the end of its log",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const podman = yield* fakePodman({
					devices: [gpu],
					onStart: (container) =>
						Effect.gen(function* () {
							yield* fs.writeFileString(
								`${placeFolder(container)}/place.log`,
								"00:00:00.012 [ERROR] [wlr] [backend/backend.c:94] Found no backends\n"
							);
							yield* Deferred.succeed(container.exit, 1);
						}).pipe(Effect.orDie),
				});
				const failed = yield* Effect.flip(
					launched.pipe(Effect.provide(yield* machine(podman, { places: 1 })))
				);
				expect(failed).toEqual(
					LaunchFailed.make({
						place: PlaceId.make(1),
						stage: "debug-server",
						detail: "the container exited with status 1",
						logTail: [
							"00:00:00.012 [ERROR] [wlr] [backend/backend.c:94] Found no backends",
						],
					})
				);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a podman wait that fails is not taken for the container's exit",
		() =>
			Effect.gen(function* () {
				const podman = yield* fakePodman({
					devices: [gpu],
					answer: answering("wait", {
						code: 125,
						stderr: "Error: no container with name or ID found\n",
					}),
				});
				const failed = yield* Effect.flip(
					launched.pipe(Effect.provide(yield* machine(podman, { places: 1 })))
				);
				expect(failed).toEqual(
					LaunchFailed.make({
						place: PlaceId.make(1),
						stage: "debug-server",
						detail:
							"podman wait failed: Error: no container with name or ID found",
						logTail: [],
					})
				);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);
});
