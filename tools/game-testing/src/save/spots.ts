/**
 * The spots of both tables: the tracked `tables/spots.tsv`, which git decides, and the recorded spots of this machine,
 * which hold only what git does not have yet. A recorded spot wins over a tracked one of the same name that serves an
 * age it serves, and every plan reads them again, so a spot kept works at once. The recorded spots are a SQLite file
 * in this machine's data folder, `game-testing/recorded.sqlite`, so writers on one machine take turns.
 */
import { Database } from "bun:sqlite";
import {
	Array as Arr,
	Clock,
	Context,
	Effect,
	FileSystem,
	Layer,
	Option,
	Schema,
	String as Str,
} from "effect";
import { UserDirectories } from "effect/persistence";
import type {
	ChestSpots,
	Mark,
	Place,
	PlayedSpot,
	Recordings,
	Spot,
	SpotAge,
} from "../contract/spots.ts";
import { Found, kindOf, Look, SpotName, samePlace } from "../contract/spots.ts";
import type { Age } from "../contract/state.ts";
import { RunId } from "../contract/values.ts";
import type { GameData, Placement } from "../data/game-data.ts";
import { nearest } from "../data/game-data.ts";
import { SpotRefused } from "../errors.ts";
import type { Check } from "./tables.ts";
import { OwnTables } from "./tables.ts";

/** The ages a spot serves, by its scene's name. */
export const spotAge = (scene: string): SpotAge => {
	if (Str.includes("_Child")(scene)) {
		return "child";
	}
	return Str.includes("_Adult")(scene) ? "adult" : "either";
};

/** Whether a scene is one an age stands in. */
const standsIn = (scene: string, age: Age) => {
	const only = spotAge(scene);
	return only === "either" || only === age;
};

/** Whether two spots of one name serve an age in common, so one stands in the other's place. */
const overlap = (
	a: { readonly name: string; readonly scene: string },
	b: { readonly name: string; readonly scene: string }
) =>
	a.name === b.name &&
	(spotAge(a.scene) === "either" ||
		spotAge(b.scene) === "either" ||
		spotAge(a.scene) === spotAge(b.scene));

/** The spots of `tracked` that no spot of `recorded` stands in place of, then the recorded ones. */
const merged = (tracked: readonly Spot[], recorded: readonly Spot[]) => [
	...Arr.filter(
		tracked,
		(spot) => !Arr.some(recorded, (over) => overlap(over, spot))
	),
	...recorded,
];

/** The spots with each mark that stands on one of them as its look. */
const marked = (spots: readonly Spot[], marks: readonly Mark[]) =>
	Arr.map(spots, (spot) =>
		Option.match(
			Arr.findFirst(
				marks,
				(mark) => mark.name === spot.name && samePlace(mark, spot)
			),
			{ onNone: () => spot, onSome: (mark) => ({ ...spot, look: mark.look }) }
		)
	);

/**
 * The tracked spots with recorded ones moved in, each in place of a tracked spot it stands for, else added, and every
 * mark as its spot's look. A mark that stands on no spot marked a worked-out spot, which it becomes.
 */
export const intoTracked = (
	tracked: readonly Spot[],
	recorded: { readonly spots: readonly Spot[]; readonly marks: readonly Mark[] }
) => {
	const spots = [
		...Arr.map(tracked, (spot) =>
			Option.getOrElse(
				Arr.findFirst(recorded.spots, (over) => overlap(over, spot)),
				() => spot
			)
		),
		...Arr.filter(
			recorded.spots,
			(over) => !Arr.some(tracked, (spot) => overlap(over, spot))
		),
	];
	const workedOut = Arr.flatMapNullishOr(recorded.marks, (mark) =>
		Arr.some(spots, (spot) => spot.name === mark.name && samePlace(mark, spot))
			? undefined
			: { ...mark, found: "worked out" as const, evidence: "", note: "" }
	);
	return marked([...spots, ...workedOut], recorded.marks);
};

/** A mark as its table holds it. */
const MarkRow = Schema.Struct({
	name: SpotName,
	scene: Schema.String,
	room: Schema.NullOr(Schema.Int),
	x: Schema.Finite,
	y: Schema.Finite,
	z: Schema.Finite,
	facing: Schema.Finite,
	outcome: Look.fields.outcome,
	run: RunId,
});

/** A recorded spot as its table holds it. */
const RecordedRow = Schema.Struct({
	name: SpotName,
	scene: Schema.String,
	room: Schema.NullOr(Schema.Int),
	x: Schema.Finite,
	y: Schema.Finite,
	z: Schema.Finite,
	facing: Schema.Finite,
	found: Found,
	evidence: Schema.String,
	note: Schema.String,
});

/** A spot as a writer gives it to be recorded: a look belongs to git or to a mark, never to a recorded spot. */
export type Recorded = Omit<Spot, "look">;

/** How long a writer waits for another on this machine to finish before it fails. */
const busyMillis = 10_000;

export class Spots extends Context.Service<
	Spots,
	{
		/** Every spot, the recorded ones in place of the tracked ones they stand for. */
		readonly all: Effect.Effect<readonly Spot[]>;
		/** The spot Link starts on for `name` at `age`. */
		readonly find: (
			name: string,
			age: Age
		) => Effect.Effect<Option.Option<Spot>>;
		/** Records `spot`, in place of a recorded spot it stands for. */
		readonly keep: (spot: Recorded) => Effect.Effect<void>;
		/** Keeps a look's mark on the spot it started on, in place of an earlier mark for that name and age. */
		readonly mark: (mark: Mark) => Effect.Effect<void>;
		/** The look marked on the spot `name` has at `place`. */
		readonly lookAt: (
			name: string,
			place: Place
		) => Effect.Effect<Option.Option<Look>>;
		/** The recorded spots and marks alone, which git does not have yet. */
		readonly recorded: Effect.Effect<Recordings>;
		/** Takes these out, as they are now in git; a spot recorded or marked again since stays. */
		readonly forget: (recordings: Recordings) => Effect.Effect<void>;
	}
>()("game-testing/save/spots") {
	static readonly layer = Layer.effect(
		Spots,
		Effect.gen(function* () {
			const own = yield* OwnTables;
			const fs = yield* FileSystem.FileSystem;
			const directories = yield* UserDirectories.UserDirectories;
			const folder = yield* directories.data("game-testing").pipe(Effect.orDie);
			yield* fs.makeDirectory(folder, { recursive: true }).pipe(Effect.orDie);
			const db = yield* Effect.acquireRelease(
				Effect.sync(() => {
					const open = new Database(`${folder}/recorded.sqlite`, {
						create: true,
						strict: true,
					});
					open.run(`PRAGMA busy_timeout = ${busyMillis}`);
					open.run("PRAGMA journal_mode = WAL");
					open.run(
						"CREATE TABLE IF NOT EXISTS mark (name TEXT NOT NULL, age TEXT NOT NULL, scene TEXT NOT NULL, room INTEGER, x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL, facing REAL NOT NULL, outcome TEXT NOT NULL, run TEXT NOT NULL)"
					);
					open.run(
						"CREATE TABLE IF NOT EXISTS spot (name TEXT NOT NULL, age TEXT NOT NULL, scene TEXT NOT NULL, room INTEGER, x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL, facing REAL NOT NULL, found TEXT NOT NULL, evidence TEXT NOT NULL, note TEXT NOT NULL)"
					);
					return open;
				}),
				(open) => Effect.sync(() => open.close())
			);
			const twice = Arr.findFirst(own.spots, (spot, index) =>
				Arr.some(Arr.drop(own.spots, index + 1), (other) =>
					overlap(spot, other)
				)
			);
			if (Option.isSome(twice)) {
				return yield* Effect.die(
					new Error(
						`tables/spots.tsv has two spots for ${twice.value.name} that serve one age.`
					)
				);
			}

			const spotsRecorded = Effect.sync((): unknown =>
				db.query("SELECT * FROM spot").all()
			).pipe(
				Effect.flatMap(Schema.decodeUnknownEffect(Schema.Array(RecordedRow))),
				Effect.map(
					Arr.map(
						(row): Spot => ({
							name: row.name,
							scene: row.scene,
							...(row.room === null ? {} : { room: row.room }),
							position: [row.x, row.y, row.z],
							facing: row.facing,
							found: row.found,
							evidence: row.evidence,
							note: row.note,
						})
					)
				),
				Effect.orDie
			);
			const marks = Effect.sync((): unknown =>
				db.query("SELECT * FROM mark").all()
			).pipe(
				Effect.flatMap(Schema.decodeUnknownEffect(Schema.Array(MarkRow))),
				Effect.map(
					Arr.map(
						(row): Mark => ({
							name: row.name,
							scene: row.scene,
							...(row.room === null ? {} : { room: row.room }),
							position: [row.x, row.y, row.z],
							facing: row.facing,
							look: { outcome: row.outcome, run: row.run },
						})
					)
				),
				Effect.orDie
			);
			const all = Effect.map(
				Effect.all([spotsRecorded, marks], { concurrency: 1 }),
				([kept, onSpots]) => marked(merged(own.spots, kept), onSpots)
			);
			const place = (at: Place) => [
				at.scene,
				at.room ?? null,
				...at.position,
				at.facing,
			];

			return Spots.of({
				all,
				recorded: Effect.map(
					Effect.all([spotsRecorded, marks], { concurrency: 1 }),
					([spots, onSpots]) => ({
						spots,
						marks: onSpots,
					})
				),
				mark: (mark) =>
					Effect.sync(() =>
						db
							.transaction(() => {
								const age = spotAge(mark.scene);
								db.run(
									"DELETE FROM mark WHERE name = ?1 AND (age = 'either' OR ?2 = 'either' OR age = ?2)",
									[mark.name, age]
								);
								db.run(
									"INSERT INTO mark (name, age, scene, room, x, y, z, facing, outcome, run) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
									[
										mark.name,
										age,
										...place(mark),
										mark.look.outcome,
										mark.look.run,
									]
								);
							})
							.immediate()
					),
				lookAt: (name, at) =>
					Effect.map(marks, (onSpots) =>
						Option.map(
							Arr.findFirst(
								onSpots,
								(mark) => mark.name === name && samePlace(mark, at)
							),
							(mark) => mark.look
						)
					),
				forget: ({ spots, marks: moved }) =>
					Effect.sync(() =>
						db
							.transaction(() => {
								for (const mark of moved) {
									db.run(
										"DELETE FROM mark WHERE name = ? AND scene = ? AND room IS ? AND x = ? AND y = ? AND z = ? AND facing = ? AND outcome = ? AND run = ?",
										[
											mark.name,
											...place(mark),
											mark.look.outcome,
											mark.look.run,
										]
									);
								}
								for (const spot of spots) {
									db.run(
										"DELETE FROM spot WHERE name = ? AND scene = ? AND room IS ? AND x = ? AND y = ? AND z = ? AND facing = ? AND found = ? AND evidence = ? AND note = ?",
										[
											spot.name,
											spot.scene,
											spot.room ?? null,
											...spot.position,
											spot.facing,
											spot.found,
											spot.evidence,
											spot.note,
										]
									);
								}
							})
							.immediate()
					),
				find: (name, age) =>
					Effect.map(all, (spots) =>
						Arr.findFirst(
							spots,
							(spot) => spot.name === name && standsIn(spot.scene, age)
						)
					),
				// One transaction takes the place of what the spot stands for, so two writers take turns.
				keep: (spot) =>
					Effect.sync(() =>
						db
							.transaction(() => {
								const age = spotAge(spot.scene);
								db.run(
									"DELETE FROM spot WHERE name = ?1 AND (age = 'either' OR ?2 = 'either' OR age = ?2)",
									[spot.name, age]
								);
								db.run(
									"INSERT INTO spot (name, age, scene, room, x, y, z, facing, found, evidence, note) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
									[
										spot.name,
										age,
										spot.scene,
										spot.room ?? null,
										...spot.position,
										spot.facing,
										spot.found,
										spot.evidence,
										spot.note,
									]
								);
							})
							.immediate()
					),
			});
		})
	);
}

/** Every placement of a check's actors. */
const placementsOf = Effect.fnUntraced(function* (
	data: GameData["Service"],
	check: Check | undefined
) {
	const byHash = yield* Effect.forEach(
		check?.placements ?? [],
		(hash) => data.placements({ hash }),
		{ concurrency: 1 }
	);
	return Arr.flatten(byHash);
});

/** The placements of a check for an age: in the scenes of that age, or of both. */
export const placedFor = Effect.fnUntraced(function* (
	data: GameData["Service"],
	check: Check,
	age: Age
) {
	const placed = yield* placementsOf(data, check);
	return Arr.filter(placed, (placement) => standsIn(placement.scene, age));
});

/** How far before a check's actor Link stands: near enough that A reaches a small chest. */
const before = 1;
const lift = 0.1;

/** The spot worked out one unit before a placement's actor, on the side it faces, facing it. */
export const workedOut = (name: string, subject: Placement): Spot => ({
	name,
	scene: subject.scene,
	...(subject.room === undefined ? {} : { room: subject.room }),
	position: [
		subject.position[0] + before * Math.sin(subject.yaw),
		subject.position[1] + lift,
		subject.position[2] + before * Math.cos(subject.yaw),
	],
	facing: ((subject.yaw * 180) / Math.PI + 180) % 360,
	found: "worked out",
	evidence: "",
	note: "",
});

/**
 * How a spot was found, and the look that started there, in words. A failed look says the look failed, which can be
 * the start's state as much as the spot: a chest that is not there yet, or rocks before it.
 */
export const aboutSpot = (spot: Spot) =>
	Arr.join(
		[
			spot.found === "worked out"
				? "worked out one unit before its actor"
				: `from ${spot.evidence}`,
			...(spot.look === undefined
				? []
				: [`a look from it ${spot.look.outcome}, run ${spot.look.run}`]),
			...(spot.note === "" ? [] : [spot.note]),
		],
		"; "
	);

/** A spot as the chest list gives it for an age. */
const listed = (age: SpotAge, spot: Spot) => ({
	age,
	kind: kindOf(spot),
	about: aboutSpot(spot),
	...(spot.look === undefined ? {} : { look: spot.look }),
});

/**
 * Every chest of `remake/chests.tsv` in its order, with a spot for each age that stands in its scenes: the spot kept
 * for it, or one worked out before its actor when that age sees the actor once. A chest the remake does not have has
 * none.
 */
export const chestSpots = Effect.fn("chestSpots")(function* (
	data: GameData["Service"],
	own: OwnTables["Service"],
	spots: Spots["Service"]
) {
	const kept = yield* spots.all;
	const { marks: onSpots } = yield* spots.recorded;
	return yield* Effect.forEach(
		own.chests,
		Effect.fnUntraced(function* (name): Effect.fn.Return<ChestSpots> {
			const placed = yield* placementsOf(data, own.checks.get(name));
			const ages = Arr.dedupe(
				Arr.map(placed, (placement) => spotAge(placement.scene))
			);
			const perAge = Arr.flatMapNullishOr(ages, (age) => {
				const standing = age === "either" ? "child" : age;
				const spot = Arr.findFirst(
					kept,
					(each) => each.name === name && standsIn(each.scene, standing)
				);
				if (Option.isSome(spot)) {
					return listed(age, spot.value);
				}
				const seen = Arr.filter(placed, (placement) =>
					standsIn(placement.scene, standing)
				);
				const [only, ...more] = seen;
				if (only === undefined || more.length > 0) {
					return;
				}
				const worked = workedOut(name, only);
				const look = Arr.findFirst(
					onSpots,
					(mark) => mark.name === name && samePlace(mark, worked)
				);
				const shown = Option.match(look, {
					onNone: () => worked,
					onSome: (mark) => ({ ...worked, look: mark.look }),
				});
				return listed(age, shown);
			});
			const [first] = placed;
			return {
				name,
				area:
					first === undefined
						? ""
						: Option.match(data.scene(first.scene), {
								onNone: () => first.scene,
								onSome: (scene) => scene.folder,
							}),
				spots: perAge,
			};
		}),
		{ concurrency: 1 }
	);
});

/** The farthest across the floor Link stands to open a chest: the saves show small chests opened from 0.6 to 1.4. */
const reach = 1.5;

/**
 * Keeps where Link stands in a running game as a spot found in play. A chest he has just opened must stand in his scene
 * within reach, and the spot faces it, since the item's pose turns him to the camera; it takes the chest's room. Any
 * other name keeps his facing, and his room inside a dungeon, and must be a check or a spot already, or new on
 * purpose. A landing's name is never a spot's.
 */
export const keepPlayed = Effect.fn("keepPlayed")(function* (
	data: GameData["Service"],
	own: OwnTables["Service"],
	spots: Spots["Service"],
	played: PlayedSpot
) {
	const refuse = (detail: string) =>
		SpotRefused.make({ spot: played.name, detail });
	const scene = data.scene(played.scene);
	if (Option.isNone(scene)) {
		return yield* refuse(
			played.scene === ""
				? "the game has loaded no scene yet."
				: `Link stands in ${played.scene}, which the game's tables do not have.`
		);
	}
	if (Option.isSome(data.landing(played.name))) {
		return yield* refuse(
			"it is a landing of the game, which a spot's name must not be."
		);
	}
	if (played.chest === undefined && played.new !== true) {
		const kept = yield* spots.all;
		const known = [...own.checks.keys(), ...Arr.map(kept, (each) => each.name)];
		if (!Arr.contains(known, played.name)) {
			return yield* refuse(
				`it is no check and no spot. Nearest: ${Arr.join(nearest(played.name, known), ", ")}. A new spot by that name is kept with --new.`
			);
		}
	}
	const millis = yield* Clock.currentTimeMillis;
	const day = Temporal.Instant.fromEpochMilliseconds(millis)
		.toZonedDateTimeISO("UTC")
		.toPlainDate()
		.toString();
	const place =
		played.chest === undefined
			? {
					...(scene.value.dungeon === "" ? {} : { room: played.room }),
					facing: played.facing,
				}
			: yield* facingChest(data, played, played.chest);
	const spot: Recorded = {
		name: played.name,
		scene: played.scene,
		position: played.position,
		found: "play",
		evidence: `a running game on ${day}`,
		note: "",
		...place,
	};
	yield* spots.keep(spot);
	return spot;
});

/** The room and facing of a spot before the chest Link has just opened, which stands in his scene within reach. */
const facingChest = Effect.fnUntraced(function* (
	data: GameData["Service"],
	played: PlayedSpot,
	hash: string
) {
	const placed = yield* data.placements({ hash });
	const chest = Arr.findFirst(
		placed,
		(placement) => placement.scene === played.scene
	);
	if (Option.isNone(chest)) {
		return yield* SpotRefused.make({
			spot: played.name,
			detail: `its chest ${hash} has no actor in ${played.scene}.`,
		});
	}
	const [x, , z] = played.position;
	const [chestX, , chestZ] = chest.value.position;
	const across = Math.hypot(chestX - x, chestZ - z);
	if (across > reach) {
		return yield* SpotRefused.make({
			spot: played.name,
			detail: `Link stood ${Number(across.toFixed(1))} units from it, and opens a chest from at most ${reach}.`,
		});
	}
	return {
		...(chest.value.room === undefined ? {} : { room: chest.value.room }),
		facing: (Math.atan2(chestX - x, chestZ - z) * 180) / Math.PI,
	};
});
