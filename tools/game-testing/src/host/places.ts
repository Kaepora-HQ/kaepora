/**
 * The places of one machine. A place is a folder and a rootless podman container named `game-testing-place-<n>`, with no
 * network: Lichii's debug server listens on the container's own loopback, and a relay inside passes it through
 * `debug.sock` in the place's folder. The container's name is the lock: podman lets one create of a name win, so the
 * place belongs to whoever made its container, and the container's labels name that holder. The holder listens on
 * `holder.sock` in the folder for as long as it lives, and the place stops its game when that socket closes.
 */
import type { Scope } from "effect";
import {
	Array as Arr,
	Clock,
	Config,
	Context,
	Duration,
	Effect,
	FileSystem,
	Layer,
	Option,
	Schedule,
	Schema,
	String as Str,
} from "effect";
import { UserDirectories } from "effect/persistence";
import { ChildProcessSpawner } from "effect/process";
import type { PlaceStatus } from "../contract/rpc.ts";
import { PlaceId, RunId, SessionId } from "../contract/values.ts";
import {
	ImageBuildFailed,
	LaunchFailed,
	NoFreePlace,
	NoGameHere,
	NoGraphicsCard,
} from "../errors.ts";
import type { Ran } from "../process.ts";
import { lastLines } from "../process.ts";
import { podman } from "./podman.ts";

const lookAgain = Duration.seconds(2);

/** How long a stopping container has between SIGTERM and SIGKILL. Lichii's clean exit takes 4 to 5 seconds. */
const stopGrace = 20;

/** The cap on every game, whoever holds it. Past it conmon kills the container. */
const gameLimit = Duration.hours(2);

/** The image built in 31 seconds here. */
const buildLimit = Duration.minutes(10);

/** One game held 13.3 GB at control. */
const memory = "16g";

const nvidiaDevice = "nvidia.com/gpu=all";

export const placeFiles = `${import.meta.dir}/../../place`;

/** A host path podman takes into `--volume` as it stands, where a relative path names a volume and a colon splits it. */
const HostPath = Schema.String.check(
	Schema.makeFilter((path) =>
		Str.startsWith("/")(path) && !Str.includes(":")(path)
			? undefined
			: "an absolute path with no colon"
	)
);

/** A place one host holds, with its container made and not started. */
export interface Place {
	readonly container: string;
	/** The place's folder on this machine: Lichii's settings, storage, save, mods, shader cache, logs and sockets. */
	readonly folder: string;
	readonly id: PlaceId;
}

const Devices = Schema.fromJsonString(
	Schema.Array(Schema.Struct({ id: Schema.String }))
);

const Listed = Schema.fromJsonString(
	Schema.Array(
		Schema.Struct({
			Id: Schema.String,
			Labels: Schema.Struct({
				"game-testing.place": Schema.FiniteFromString.pipe(
					Schema.decodeTo(PlaceId)
				),
				/** The holding process's id and its start time, so a reused id is never taken for the holder. */
				"game-testing.holder": Schema.String,
				"game-testing.session": SessionId,
				"game-testing.run": RunId,
				"game-testing.owner": Schema.String,
				"game-testing.since": Schema.String,
			}),
		})
	)
);

const heldIn = (containers: typeof Listed.Type, id: PlaceId) =>
	Arr.findFirst(
		containers,
		(container) => container.Labels["game-testing.place"] === id
	);

/** Podman says a name is taken in one way when storage holds it and another when only its database does. */
const nameInUse = /name "[^"]+" is (?:already )?in use/u;

const taken = (ran: Ran) => nameInUse.test(ran.stderr);

export class Places extends Context.Service<
	Places,
	{
		/**
		 * Takes the first free place and makes its container, which the scope removes when it closes. A place whose holder
		 * has gone is cleaned up and taken. While every place is held, it looks again every 2 seconds, up to
		 * `waitForPlace`, and it always looks once.
		 */
		readonly take: (options: {
			readonly session: SessionId;
			readonly run: RunId;
			/** Who asks, for the places list: a script's path, `mcp`, or a CI job. */
			readonly owner: string;
			readonly waitForPlace: Duration.Duration;
		}) => Effect.Effect<
			Place,
			| NoFreePlace
			| NoGameHere
			| NoGraphicsCard
			| ImageBuildFailed
			| LaunchFailed,
			Scope.Scope
		>;
		readonly list: Effect.Effect<readonly (typeof PlaceStatus.Type)[]>;
	}
>()("game-testing/host/places") {
	static readonly layer = Layer.effect(
		Places,
		Effect.gen(function* () {
			const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
			const fs = yield* FileSystem.FileSystem;
			const directories = yield* UserDirectories.UserDirectories;
			const count = yield* Config.schema(
				Schema.Int.check(Schema.isGreaterThanOrEqualTo(1)),
				"GAME_TESTING_PLACES"
			).pipe(Config.withDefault(1));
			const card = yield* Config.Literals(
				["nvidia", "amd"],
				"GAME_TESTING_CARD"
			);
			const lichii = yield* Config.schema(HostPath, "GAME_TESTING_LICHII");
			const game = yield* Config.option(
				Config.schema(HostPath, "GAME_TESTING_GAME_FILE")
			);
			const data = yield* Config.option(
				Config.schema(HostPath, "GAME_TESTING_DATA")
			);
			const dataFolder = yield* Option.match(data, {
				onSome: Effect.succeed,
				onNone: () => Effect.orDie(directories.data("game-testing")),
			});
			const root = `${dataFolder}/places`;
			const ids = Arr.makeBy(count, (n) => PlaceId.make(n + 1));
			const run = (args: readonly string[]) => podman(spawner, args);

			const startTime = (of: string) =>
				fs.readFileString(`/proc/${of}/stat`).pipe(
					// Field 22, counted from the state, which follows the name's closing bracket.
					Effect.map((stat) =>
						Arr.get(Str.split(Arr.lastNonEmpty(Str.split(stat, ") ")), " "), 19)
					),
					Effect.catchReason(
						"PlatformError",
						"NotFound",
						() => Effect.succeedNone
					),
					Effect.orDie
				);
			const pid = String(process.pid);
			const started = yield* startTime(pid);
			const holder = `${pid}:${Option.getOrThrow(started)}`;
			const gone = Effect.fnUntraced(function* (held: string) {
				const [heldPid = "", heldStart] = Str.split(held, ":");
				const start = yield* startTime(heldPid);
				return !(Option.isSome(start) && start.value === heldStart);
			});

			const image = Effect.gen(function* () {
				const hash = new Bun.CryptoHasher("sha256");
				for (const file of ["Containerfile", "start"]) {
					const bytes = yield* Effect.orDie(
						fs.readFile(`${placeFiles}/${file}`)
					);
					hash.update(bytes);
				}
				const tag = `localhost/game-testing-place:${Str.slice(0, 12)(hash.digest("hex"))}`;
				const exists = yield* run(["image", "exists", tag]);
				if (exists.code === 0) {
					return tag;
				}
				const built = yield* run(["build", "--tag", tag, placeFiles]).pipe(
					Effect.timeoutOrElse({
						duration: buildLimit,
						orElse: () =>
							Effect.fail(
								ImageBuildFailed.make({
									image: tag,
									detail: `it ran past its limit of ${Duration.format(buildLimit)}`,
								})
							),
					})
				);
				if (built.code !== 0) {
					return yield* ImageBuildFailed.make({
						image: tag,
						detail: Arr.join(lastLines(built.stderr, 10), "\n"),
					});
				}
				return tag;
			});

			const listed = Effect.gen(function* () {
				const ran = yield* run([
					"ps",
					"--all",
					"--filter",
					"label=game-testing.place",
					"--format",
					"json",
				]);
				return yield* Schema.decodeEffect(Listed)(ran.stdout);
			}).pipe(Effect.orDie);

			const cardFlags = Effect.gen(function* () {
				if (card === "amd") {
					// RADV comes from the image, and keep-groups carries the user's render group in. Only crun supports keep-groups.
					return ["--device", "/dev/dri", "--group-add", "keep-groups"];
				}
				const ran = yield* run([
					"info",
					"--format",
					"{{json .Host.DiscoveredDevices}}",
				]);
				const devices = yield* Effect.orDie(
					Schema.decodeEffect(Devices)(ran.stdout)
				);
				if (!Arr.some(devices, (device) => device.id === nvidiaDevice)) {
					return yield* NoGraphicsCard.make({});
				}
				return ["--device", nvidiaDevice];
			});

			const removal = (container: string) =>
				Effect.map(
					run(["rm", "--force", "--time", String(stopGrace), container]),
					(ran) =>
						ran.code === 0
							? Option.none()
							: Option.some(
									`podman rm of ${container} failed: ${Str.trim(ran.stderr)}`
								)
				);
			const release = (place: Place) =>
				Effect.flatMap(removal(place.container), (refused) =>
					Option.match(refused, {
						onNone: () => Effect.void,
						onSome: (reason) => Effect.die(new Error(reason)),
					})
				);

			/** Listens on the place's holder socket in the caller's scope, so it closes when the holder ends, however it ends. */
			const holdOn = (place: Place) =>
				Effect.gen(function* () {
					const path = `${place.folder}/holder.sock`;
					yield* Effect.orDie(fs.remove(path, { force: true }));
					yield* Effect.acquireRelease(
						Effect.sync(() =>
							Bun.listen({ unix: path, socket: { data: () => undefined } })
						),
						(listener) => Effect.sync(() => listener.stop(true))
					);
				});

			const take = Effect.fn("Places.take")(function* (options: {
				readonly session: SessionId;
				readonly run: RunId;
				readonly owner: string;
				readonly waitForPlace: Duration.Duration;
			}) {
				const gameFile = yield* Effect.mapError(Effect.fromOption(game), () =>
					NoGameHere.make({})
				);
				const gameHere = yield* Effect.orDie(fs.exists(gameFile));
				if (!gameHere) {
					return yield* NoGameHere.make({ path: gameFile });
				}
				const flags = yield* cardFlags;
				const tag = yield* image;
				const now = yield* Clock.currentTimeMillis;
				const since = Temporal.Instant.fromEpochMilliseconds(now).toString();

				const make = (id: PlaceId, folder: string) =>
					run([
						"create",
						"--name",
						`game-testing-place-${id}`,
						...Arr.flatMap(
							[
								`game-testing.place=${id}`,
								`game-testing.holder=${holder}`,
								`game-testing.session=${options.session}`,
								`game-testing.run=${options.run}`,
								`game-testing.owner=${options.owner}`,
								`game-testing.since=${since}`,
							],
							(label) => ["--label", label]
						),
						"--network",
						"none",
						"--timeout",
						String(Duration.toSeconds(gameLimit)),
						"--memory",
						memory,
						...flags,
						"--volume",
						`${folder}:/place`,
						"--volume",
						`${lichii}:/opt/lichii:ro`,
						"--volume",
						`${gameFile}:/game/oot.nup:ro`,
						tag,
					]);
				const made = (id: PlaceId, folder: string, ran: Ran) => {
					if (ran.code === 0) {
						return Effect.succeedSome({
							id,
							folder,
							container: Str.trim(ran.stdout),
						});
					}
					return taken(ran)
						? Effect.succeedNone
						: Effect.fail(
								LaunchFailed.make({
									place: id,
									stage: "spawn",
									detail: Str.trim(ran.stderr),
									logTail: [],
								})
							);
				};

				const tryPlace = Effect.fnUntraced(function* (id: PlaceId) {
					const folder = `${root}/${id}`;
					yield* Effect.orDie(
						Effect.forEach(
							["config/lichii", "data", "cache", "work"],
							(part) =>
								fs.makeDirectory(`${folder}/${part}`, { recursive: true }),
							{ concurrency: "unbounded", discard: true }
						)
					);
					const first = yield* make(id, folder);
					if (!taken(first)) {
						return yield* made(id, folder, first);
					}
					const containers = yield* listed;
					const held = heldIn(containers, id);
					if (Option.isNone(held)) {
						return Option.none();
					}
					const left = yield* gone(held.value.Labels["game-testing.holder"]);
					if (!left) {
						return Option.none();
					}
					const refused = yield* removal(held.value.Id);
					if (Option.isSome(refused)) {
						return yield* LaunchFailed.make({
							place: id,
							stage: "spawn",
							detail: refused.value,
							logTail: [],
						});
					}
					const second = yield* make(id, folder);
					return yield* made(id, folder, second);
				});

				const noneFree = NoFreePlace.make({
					places: count,
					waited: options.waitForPlace,
				});
				const firstFree = Effect.gen(function* () {
					for (const id of ids) {
						const place = yield* tryPlace(id);
						if (Option.isSome(place)) {
							return place.value;
						}
					}
					return yield* noneFree;
				});
				const place = yield* Effect.acquireRelease(firstFree, release).pipe(
					Effect.retry({
						schedule: Schedule.max([
							Schedule.spaced(lookAgain),
							Schedule.during(options.waitForPlace),
						]),
						while: (error) => error._tag === "NoFreePlace",
					})
				);
				yield* holdOn(place);
				return place;
			});

			const list = Effect.gen(function* () {
				const held = yield* listed;
				return Arr.map(ids, (id) =>
					Option.match(heldIn(held, id), {
						onNone: () => ({ place: id }),
						onSome: ({ Labels: labels }) => ({
							place: id,
							holder: {
								session: labels["game-testing.session"],
								run: labels["game-testing.run"],
								owner: labels["game-testing.owner"],
								since: labels["game-testing.since"],
							},
						}),
					})
				);
			}).pipe(Effect.withSpan("Places.list"));

			return Places.of({ take, list });
		})
	);
}
