/**
 * Records spots from a running game, through its debug server. It only reads: it sends `search_memory` once a
 * connection, to find the game's program, and `read_memory` for every value, and neither changes the game.
 *
 * Measured on the game: `PlayerStatus.CurrentPos` follows Link as he walks, and `PlayerStatus.CurrentDir` is the way he
 * faces, a unit vector across the floor. A chest's `IsTreasureBoxOpen` turns true once the last page of its item's
 * text closes.
 */
import { Array as Arr, Effect, Ref, Schedule } from "effect";
import type { PlayedSpot, Spot } from "../contract/spots.ts";
import { failureOf, GameStarting } from "../errors.ts";
import type { DebugServer } from "./emulator.ts";
import { connect } from "./emulator.ts";
import type { ValueEntry, Values } from "./values.ts";
import { attach } from "./values.ts";

/** A chest check and the actors that stand for it, each with its own `IsTreasureBoxOpen`. */
export interface Chest {
	readonly hashes: readonly string[];
	readonly name: string;
}

/** Where a spot goes: the recorded spots of the machine that plans looks, here or far. */
export type Keep<E> = (played: PlayedSpot) => Effect.Effect<Spot, E>;

const every = "500 millis";
const waitForGame = "2 seconds";

const entry = (name: string, type: "Int" | "String64"): ValueEntry => ({
	name,
	hash: Bun.hash.murmur32v3(name),
	type,
});

const pointOf = (name: string) => ({ name, hash: Bun.hash.murmur32v3(name) });

const degrees = (radians: number) => (radians * 180) / Math.PI;

/** The game's values for one connection, which closes with the scope. */
const reading = (server: DebugServer) =>
	Effect.flatMap(
		connect({
			...server,
			wire: { line: () => Effect.void },
			logFileTail: Effect.succeed([]),
		}),
		attach
	);

/** Where Link stands now: his scene, the game's room, his place and the way he faces. */
const standing = Effect.fn("Recorder.standing")(function* (values: Values) {
	const scene = yield* values.read(entry("Sequence_CurrentBanc", "String64"));
	const room = yield* values.read(entry("DungeonRoom", "Int"));
	const position = yield* values.readPoint(pointOf("PlayerStatus.CurrentPos"));
	const [x, , z] = yield* values.readPoint(pointOf("PlayerStatus.CurrentDir"));
	return {
		scene: String(scene),
		room: Number(room),
		position,
		facing: degrees(Math.atan2(x, z)),
	};
});

/** Keeps where Link stands now under `name`. */
export const recordHere = Effect.fn("Recorder.here")(function* <E>(
	server: DebugServer,
	name: string,
	keep: Keep<E>,
	isNew = false
) {
	const values = yield* reading(server);
	const now = yield* standing(values);
	return yield* keep({ name, ...now, ...(isNew ? { new: true } : {}) });
}, Effect.scoped);

/**
 * The chests whose actors keep an `IsTreasureBoxOpen` value in the running game, with only those actors. A trap chest
 * keeps a per-actor key instead, which this does not read. A game whose tables are not loaded yet has none.
 */
const recordedIn = Effect.fnUntraced(function* (
	values: Values,
	chests: readonly Chest[]
) {
	const held = yield* values.readFlags(
		Arr.flatMap(chests, (chest) => Arr.map(chest.hashes, flagOf))
	);
	const watched = Arr.filter(
		Arr.map(chests, (chest) => ({
			name: chest.name,
			hashes: Arr.filter(chest.hashes, (hash) => held.has(flagOf(hash))),
		})),
		(chest) => chest.hashes.length > 0
	);
	if (watched.length === 0) {
		return yield* GameStarting.make({});
	}
	return watched;
});

const flagOf = (hash: string) => `IsTreasureBoxOpen.${hash}`;

/** The chests' actors open now, by hash, and Link's scene. */
const openNow = (values: Values, chests: readonly Chest[]) =>
	Effect.gen(function* () {
		const scene = yield* values.read(entry("Sequence_CurrentBanc", "String64"));
		const hashes = Arr.flatMap(chests, (chest) => chest.hashes);
		const held = yield* values.readFlags(Arr.map(hashes, flagOf));
		const open = new Set(
			Arr.filter(hashes, (hash) => held.get(flagOf(hash)) === true)
		);
		return { scene: String(scene), open };
	});

/** How long a spot the game machine could not take waits before it is sent again. */
const sendAgain = "5 seconds";

/**
 * Watches a running game and keeps a spot each time Link opens one chest, until it is stopped. A read that shows
 * several chests opened at once, or a new scene, is a load, and keeps nothing. A spot the machine that keeps spots
 * cannot take, as while its server restarts, is held and sent again until it is kept or refused. While no game
 * answers, or one is still starting, it waits for one. It says what happens in `say`.
 */
export const watchChests = <
	E extends { readonly message: string; readonly _tag: string },
>(
	server: DebugServer,
	chests: readonly Chest[],
	keep: Keep<E>,
	say: (line: string) => Effect.Effect<void>
) =>
	Effect.scoped(
		Effect.gen(function* () {
			const sending = yield* Effect.scope;
			const send = (played: PlayedSpot) => {
				const attempt = keep(played);
				return attempt.pipe(
					Effect.catchIf(
						(failed) => failed._tag === "RemoteUnavailable",
						(failed) =>
							say(
								`held ${played.name}: ${failed.message} It is sent again until it is kept.`
							).pipe(
								Effect.andThen(
									attempt.pipe(
										Effect.retry({
											while: (again) => again._tag === "RemoteUnavailable",
											schedule: Schedule.spaced(sendAgain),
										})
									)
								)
							)
					),
					Effect.flatMap((spot) =>
						say(
							`kept ${spot.name}: ${spot.scene} at (${Arr.join(Arr.map(spot.position, String), ", ")})`
						)
					),
					Effect.catch((refused) => say(refused.message))
				);
			};
			const nameOf = new Map(
				Arr.flatMap(chests, (chest) =>
					Arr.map(chest.hashes, (hash) => [hash, chest.name] as const)
				)
			);
			const waiting = yield* Ref.make("");
			const watch = Effect.gen(function* () {
				const values = yield* reading(server);
				const watched = yield* recordedIn(values, chests);
				// What is open before it says it watches is the game's already, so each chest opened after is new.
				const first = yield* openNow(values, watched);
				const last = yield* Ref.make(first);
				yield* Ref.set(waiting, "");
				const unseen = Arr.filter(
					chests,
					(chest) => !Arr.some(watched, (each) => each.name === chest.name)
				);
				yield* say(
					`watching ${watched.length} chests${
						unseen.length === 0
							? ""
							: `; ${Arr.join(
									Arr.map(unseen, (chest) => chest.name),
									", "
								)} keep no value that says they are open, so record them by name`
					}`
				);
				yield* Effect.gen(function* () {
					const now = yield* openNow(values, watched);
					const before = yield* Ref.getAndSet(last, now);
					if (before.scene !== now.scene) {
						return;
					}
					const opened = Arr.filter(
						[...now.open],
						(hash) => !before.open.has(hash)
					);
					const [chest, ...more] = opened;
					if (more.length > 0) {
						return yield* say(
							`${opened.length} chests opened at once, as a save loads them, so no spot is kept for them`
						);
					}
					const name = chest === undefined ? undefined : nameOf.get(chest);
					if (chest === undefined || name === undefined) {
						return;
					}
					const here = yield* standing(values);
					yield* Effect.forkIn(send({ name, ...here, chest }), sending);
				}).pipe(Effect.repeat(Schedule.spaced(every)));
			});
			return yield* Effect.scoped(watch).pipe(
				Effect.catchCause((cause) =>
					Effect.gen(function* () {
						const { message: why } = failureOf(cause);
						if ((yield* Ref.getAndSet(waiting, why)) !== why) {
							yield* say(`waiting for a game: ${why}`);
						}
						yield* Effect.sleep(waitForGame);
					})
				),
				Effect.forever
			);
		})
	);
