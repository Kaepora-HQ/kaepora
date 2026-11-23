/**
 * The spots of both tables: the tracked `tables/spots.tsv`, which git decides, and the recorded spots of this machine,
 * which hold only what git does not have yet. A recorded spot wins over a tracked one of the same name that serves an
 * age it serves, and every plan reads them again, so a spot kept works at once. The recorded spots are a SQLite file
 * in this machine's data folder, `game-testing/recorded.sqlite`, so writers on one machine take turns.
 */
import { Database } from "bun:sqlite";
import {
	Array as Arr,
	Context,
	Effect,
	FileSystem,
	Layer,
	Option,
	Schema,
	String as Str,
} from "effect";
import { UserDirectories } from "effect/persistence";
import type { ChestSpots, Spot, SpotAge } from "../contract/spots.ts";
import { Found, kindOf, SpotName } from "../contract/spots.ts";
import type { Age } from "../contract/state.ts";
import type { GameData } from "../data/game-data.ts";
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

			const recorded = Effect.sync((): unknown =>
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
			const all = Effect.map(recorded, (kept) => merged(own.spots, kept));

			return Spots.of({
				all,
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

export const workedOutAbout = "worked out one unit before its actor";

/** How a spot was found, and the look that started there, in words. */
export const aboutSpot = (spot: Spot) =>
	Arr.join(
		[
			spot.found === "worked out" ? workedOutAbout : `from ${spot.evidence}`,
			...(spot.look === undefined
				? []
				: [`run ${spot.look.run} ${spot.look.outcome} from it`]),
			...(spot.note === "" ? [] : [spot.note]),
		],
		"; "
	);

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
					return {
						age,
						kind: kindOf(spot.value),
						about: aboutSpot(spot.value),
					};
				}
				const seen = Arr.filter(placed, (placement) =>
					standsIn(placement.scene, standing)
				);
				return seen.length === 1
					? { age, kind: "worked out" as const, about: workedOutAbout }
					: undefined;
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
