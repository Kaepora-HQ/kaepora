/**
 * A saved value is named by every string in the game's files whose murmur3 is the value's hash, and by its struct:
 * the member of struct S named M is the value `S.M`, checked against both hashes the list gives. A name only a string
 * gives can be chance; a struct member's cannot.
 */
import { Database } from "bun:sqlite";
import {
	Array as Arr,
	Config,
	Context,
	Effect,
	FileSystem,
	Formatter,
	Layer,
	Option,
	Order,
	Schema,
	String as Str,
} from "effect";
import { UserDirectories } from "effect/persistence";
import { ChildProcessSpawner } from "effect/process";
import type { ValueType as ValueTypeSchema } from "../contract/plan.ts";
import type { UnmeasuredFile } from "../contract/record.ts";
import type { NameMatch } from "../contract/rpc.ts";
import type { Mod } from "../contract/state.ts";
import type {
	NameKind,
	Position,
	Saved,
	TableKind,
} from "../contract/values.ts";
import type { DumpFailed } from "../errors.ts";
import {
	ModUnreadable,
	NoGameHere,
	StateRefused,
	UnknownName,
} from "../errors.ts";
import type { Dump } from "./build.ts";
import {
	ChoicesJson,
	fill,
	listed,
	makeDump,
	PouchRowJson,
	readGame,
	SavedJson,
	saveFiles,
	scalarGroups,
} from "./build.ts";
import type { PouchRow } from "./dump.ts";
import { FileLine, GameDataList, Key, ModLine, modTooLarge } from "./dump.ts";

/** The layout of the cache file; a change to its tables raises it, which gives every game file a new cache. */
const cacheLayout = 2;

export type ValueType = typeof ValueTypeSchema.Type;

export type SaveFileName = (typeof saveFiles)[number] | "none";

export interface GameValue {
	readonly choices: readonly string[];
	/** The game data list's `DefaultValue`. */
	readonly default: Saved;
	readonly file: SaveFileName;
	readonly hash: number;
	readonly name: string;
	/** Whether the game's files give the value this name, so a record built from a placement is that record. */
	readonly named: boolean;
	readonly type: ValueType;
}

/**
 * A value a mod's new game starts with in place of the game's own start, `value.default`; or, `added`, a value the mod
 * adds to the game's list, which no base holds, named by its hash, with the mod's entry as its `value`.
 */
export interface StartValue {
	readonly added: boolean;
	readonly to: Saved;
	readonly value: GameValue;
}

export interface Item {
	readonly name: string;
	readonly row: PouchRow;
	/** The actor the save records it under, `ActorInfo.ActorName`. */
	readonly savedActor: string;
}

export interface Scene {
	readonly dungeon: string;
	readonly folder: string;
	readonly name: string;
	readonly outdoors: boolean;
	readonly rooms: number;
}

export type Point = typeof Position.Type;

export interface Landing {
	readonly name: string;
	readonly position: Point;
	readonly scene: string;
	/** Radians about the vertical axis: 0 faces +Z, a quarter turn faces +X. */
	readonly yaw: number;
}

export interface Placement {
	readonly actor: string;
	readonly hash: string;
	/** An anchor's or a named actor's own name. */
	readonly name: string | undefined;
	readonly position: Point;
	readonly room: number | undefined;
	readonly scene: string;
	readonly yaw: number;
}

export type PlacementQuery =
	| { readonly scene: string; readonly room?: number }
	| { readonly hash: string }
	| { readonly actor: string };

export class GameData extends Context.Service<
	GameData,
	{
		readonly key: string;
		readonly value: (name: string) => Effect.Effect<GameValue, UnknownName>;
		readonly find: (name: string) => Effect.Effect<Option.Option<GameValue>>;
		readonly item: (name: string) => Effect.Effect<Item, UnknownName>;
		readonly items: readonly Item[];
		readonly scene: (name: string) => Option.Option<Scene>;
		readonly landing: (name: string) => Option.Option<Landing>;
		readonly placements: (
			query: PlacementQuery
		) => Effect.Effect<readonly Placement[]>;
		readonly isKey: (key: bigint) => Effect.Effect<boolean>;
		readonly named: (
			placements: readonly Placement[],
			flows: readonly string[]
		) => Effect.Effect<readonly string[]>;
		readonly names: (kind: TableKind) => Effect.Effect<readonly string[]>;
		readonly checkMod: (
			folder: string
		) => Effect.Effect<readonly UnmeasuredFile[], StateRefused | DumpFailed>;
		/** Each value the mod's game data list starts a new game with that the game's own list starts otherwise. */
		readonly startValues: (
			mod: Mod
		) => Effect.Effect<readonly StartValue[], ModUnreadable>;
		readonly search: (
			text: string,
			options: { readonly kind?: NameKind; readonly limit: number }
		) => Effect.Effect<readonly NameMatch[]>;
	}
>()("game-testing/data/game-data/GameData") {
	static readonly layer = Layer.effect(
		GameData,
		Effect.gen(function* () {
			const fs = yield* FileSystem.FileSystem;
			const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
			const game = yield* Config.String("GAME_TESTING_GAME_FILE").pipe(
				Effect.catchTag("ConfigError", () => NoGameHere.make({}))
			);
			const program = yield* Config.String("GAME_TESTING_PATCHER").pipe(
				Effect.orDie
			);
			if (!(yield* fs.exists(game).pipe(Effect.orDie))) {
				return yield* NoGameHere.make({ path: game });
			}
			const dump = makeDump(spawner, program, game);
			const printed = yield* dump.run("key");
			const key = yield* dump.decode(Key, printed);
			const name = `${key.key}-${key.build}-${key.format}-${cacheLayout}`;
			const directories = yield* UserDirectories.UserDirectories;
			const cache = yield* directories.cache("game-testing").pipe(Effect.orDie);
			const folder = `${cache}/game-data`;
			const path = `${folder}/${name}.sqlite`;
			if (!(yield* fs.exists(path).pipe(Effect.orDie))) {
				yield* fs.makeDirectory(folder, { recursive: true }).pipe(Effect.orDie);
				const read = yield* readGame(dump);
				yield* Effect.scoped(
					Effect.gen(function* () {
						const building = yield* fs
							.makeTempDirectoryScoped({ directory: folder })
							.pipe(Effect.orDie);
						yield* fill(`${building}/${name}.sqlite`, read);
						yield* fs
							.rename(`${building}/${name}.sqlite`, path)
							.pipe(Effect.orDie);
					})
				);
			}
			const db = yield* Effect.acquireRelease(
				Effect.sync(() => new Database(path, { readonly: true })),
				(open) => Effect.sync(() => open.close())
			);
			return yield* serve(db, name, dump, fs);
		})
	);
}

const ValueRow = Schema.Struct({
	name: Schema.NullOr(Schema.String),
	type: Schema.Literals(scalarGroups),
	default: SavedJson,
	file: Schema.Literals([...saveFiles, "none"]),
	choices: ChoicesJson,
});
const ItemRow = Schema.Struct({
	name: Schema.String,
	savedActor: Schema.String,
	row: PouchRowJson,
});
const SceneRow = Schema.Struct({
	name: Schema.String,
	folder: Schema.String,
	dungeon: Schema.String,
	rooms: Schema.Int,
	outdoors: Schema.Literals([0, 1]),
});
const PlaceRow = Schema.Struct({
	name: Schema.String,
	scene: Schema.String,
	x: Schema.Finite,
	y: Schema.Finite,
	z: Schema.Finite,
	yaw: Schema.Finite,
});
const PlacementRow = Schema.Struct({
	hash: Schema.String,
	scene: Schema.String,
	actor: Schema.String,
	name: Schema.NullOr(Schema.String),
	room: Schema.NullOr(Schema.Int),
	x: Schema.Finite,
	y: Schema.Finite,
	z: Schema.Finite,
	yaw: Schema.Finite,
});
const Text = Schema.Struct({ text: Schema.String });
const NameRow = Schema.Struct({
	kind: Schema.String,
	name: Schema.String,
	about: Schema.String,
});

/** Where a mod carries its game data list, whose defaults are what its new game starts with. */
const startList = "romfs/GameData/GameDataList.Product.110.byml.zs";

const DefaultRow = Schema.Struct({ hash: Schema.Int, text: Schema.String });

const serve = Effect.fn("GameData.open")(function* (
	db: Database,
	key: string,
	dump: Dump,
	fs: FileSystem.FileSystem
) {
	const rows = <S extends Schema.Top & { readonly DecodingServices: never }>(
		row: S,
		sql: string,
		...params: readonly (string | number | null)[]
	) => {
		const found = Effect.sync((): unknown => db.query(sql).all(...params));
		return Effect.flatMap(found, (all) =>
			Schema.decodeUnknownEffect(Schema.Array(row))(all)
		).pipe(Effect.orDie);
	};
	const texts = (sql: string, ...params: readonly (string | number | null)[]) =>
		Effect.map(
			rows(Text, sql, ...params),
			Arr.map((row) => row.text)
		);

	const items = yield* rows(
		ItemRow,
		"SELECT name, saved AS savedActor, row FROM item"
	);
	const scenes = yield* rows(SceneRow, "SELECT * FROM scene");
	const landings = yield* rows(PlaceRow, "SELECT * FROM landing");
	const itemsByName = new Map(Arr.map(items, (item) => [item.name, item]));
	const scenesByName = new Map(
		Arr.map(scenes, (scene): [string, Scene] => [
			scene.name,
			{ ...scene, outdoors: scene.outdoors === 1 },
		])
	);
	const landingsByName = new Map(
		Arr.map(landings, (landing): [string, Landing] => [
			landing.name,
			{
				name: landing.name,
				scene: landing.scene,
				position: [landing.x, landing.y, landing.z],
				yaw: landing.yaw,
			},
		])
	);
	const names = (kind: TableKind) =>
		texts("SELECT name AS text FROM name WHERE kind = ?", kind);
	const unknown = (kind: TableKind, asked: string) =>
		Effect.flatMap(names(kind), (known) =>
			UnknownName.make({ kind, asked, suggestions: nearest(asked, known) })
		);
	const placementsOf = (
		sql: string,
		...params: readonly (string | number | null)[]
	) =>
		Effect.map(
			rows(PlacementRow, `SELECT * FROM placement WHERE ${sql}`, ...params),
			Arr.map(
				(row): Placement => ({
					hash: row.hash,
					scene: row.scene,
					actor: row.actor,
					name: row.name ?? undefined,
					room: row.room ?? undefined,
					position: [row.x, row.y, row.z],
					yaw: row.yaw,
				})
			)
		);
	const json = (values: readonly string[]) => Formatter.formatJson(values);

	const gameValue = (
		name: string,
		hash: number,
		found: typeof ValueRow.Type
	): GameValue => ({
		name,
		named: found.name === name,
		hash,
		type: found.type,
		default: found.default,
		file: found.file,
		choices: found.choices,
	});
	const valueRows = (hash: number) =>
		rows(
			ValueRow,
			'SELECT name, type, default_value AS "default", file, choices FROM value WHERE hash = ?',
			hash
		);

	const find = Effect.fn("GameData.find")(function* (name: string) {
		const hash = valueHash(name);
		const [row] = yield* valueRows(hash);
		const raw = Str.startsWith("0x")(name);
		// A name whose hash the game's files give to another name is a chance match, not this value.
		return Option.map(
			Option.filter(
				Option.fromNullishOr(row),
				(found) => raw || found.name === null || found.name === name
			),
			(found) => gameValue(name, hash, found)
		);
	});

	/** The game's own start of every value, as the cache writes it, so two starts compare as text. */
	const ownStarts = yield* Effect.cached(
		Effect.map(
			rows(DefaultRow, "SELECT hash, default_value AS text FROM value"),
			(all) => new Map(Arr.map(all, (row) => [row.hash, row.text]))
		)
	);
	const startValues = Effect.fn("GameData.startValues")(function* (mod: Mod) {
		const carried = Arr.findFirst(mod.files, (file) => file.path === startList);
		if (Option.isNone(carried)) {
			return [];
		}
		const where = `${mod.name}/${startList}`;
		const unreadable = (detail: string) =>
			ModUnreadable.make({ folder: where, reason: "list", detail });
		const printed = yield* Effect.scoped(
			Effect.gen(function* () {
				const folder = yield* Effect.orDie(fs.makeTempDirectoryScoped());
				// The dump opens a file by its name's ending, as the game does.
				const file = `${folder}/${Arr.lastNonEmpty(Str.split(startList, "/"))}`;
				yield* Effect.orDie(fs.writeFile(file, carried.value.bytes));
				return yield* dump
					.run("open", file)
					.pipe(
						Effect.catchTag("DumpFailed", (failed) =>
							unreadable(Str.trim(Str.replaceAll(file, where)(failed.stderr)))
						)
					);
			})
		);
		const { root } = yield* dump.decode(FileLine, printed);
		const list = yield* Schema.decodeUnknownEffect(GameDataList)(root).pipe(
			Effect.mapError((issue) => unreadable(issue.message))
		);
		const own = yield* ownStarts;
		const changed = yield* Effect.filter(
			listed(list),
			(entry) =>
				Effect.map(
					Effect.orDie(Schema.encodeEffect(SavedJson)(entry.default)),
					(text) => own.get(entry.hash) !== text
				),
			{ concurrency: 1 }
		);
		return yield* Effect.forEach(
			changed,
			Effect.fnUntraced(function* (entry): Effect.fn.Return<
				StartValue,
				ModUnreadable
			> {
				const [found] = yield* valueRows(entry.hash);
				const hex = `0x${entry.hash.toString(16)}`;
				if (found === undefined) {
					return {
						added: true,
						value: { ...entry, name: hex, named: false },
						to: entry.default,
					};
				}
				return {
					added: false,
					value: gameValue(found.name ?? hex, entry.hash, found),
					to: entry.default,
				};
			}),
			{ concurrency: 1 }
		);
	});

	return GameData.of({
		key,
		find,
		value: (name) =>
			Effect.flatMap(find(name), (found) =>
				Option.isSome(found)
					? Effect.succeed(found.value)
					: unknown("value", name)
			),
		item: (name) => {
			const item = itemsByName.get(name);
			return item === undefined ? unknown("item", name) : Effect.succeed(item);
		},
		items,
		scene: (name) => Option.fromNullishOr(scenesByName.get(name)),
		landing: (name) => Option.fromNullishOr(landingsByName.get(name)),
		placements: (query) => {
			if ("hash" in query) {
				return placementsOf("hash = ?", query.hash);
			}
			if ("actor" in query) {
				return placementsOf("actor = ?", query.actor);
			}
			return placementsOf(
				"scene = ? AND (?2 IS NULL OR room = ?2)",
				query.scene,
				query.room ?? null
			);
		},
		isKey: (actorKey) =>
			Effect.map(
				texts(
					"SELECT key AS text FROM actor_key WHERE key = ?",
					actorKey.toString()
				),
				Arr.isReadonlyArrayNonEmpty
			),
		named: Effect.fn("GameData.named")(function* (placements, flows) {
			const bound = yield* texts(
				"SELECT DISTINCT module AS text FROM binding WHERE actor IN (SELECT value FROM json_each(?))",
				json(Arr.map(placements, (placement) => placement.hash))
			);
			const modules = new Set(bound);
			const pending = [...bound];
			for (
				let module = pending.pop();
				module !== undefined;
				module = pending.pop()
			) {
				for (const child of yield* texts(
					"SELECT child AS text FROM module_child WHERE module = ?",
					module
				)) {
					if (!modules.has(child)) {
						modules.add(child);
						pending.push(child);
					}
				}
			}
			return yield* texts(
				"SELECT DISTINCT name AS text FROM named WHERE (kind = 'module' AND source IN (SELECT value FROM json_each(?1))) OR (kind = 'flow' AND source IN (SELECT value FROM json_each(?2))) ORDER BY name",
				json([...modules]),
				json(
					Arr.dedupe([
						...Arr.map(placements, (placement) => placement.actor),
						...flows,
					])
				)
			);
		}),
		names,
		checkMod: Effect.fn("GameData.checkMod")(function* (folder) {
			const done = yield* dump.exec("check-mod", [folder], [0, modTooLarge]);
			const lines = yield* Effect.forEach(
				dump.lines(done.stdout),
				(line) => dump.decode(ModLine, line),
				{ concurrency: 1 }
			);
			if (done.exitCode === modTooLarge) {
				return yield* StateRefused.make({
					reason: "mod-too-large",
					detail: Arr.join(
						Arr.flatMapNullishOr(lines, (line) =>
							"needs" in line
								? `${line.path} needs ${line.needs} bytes in the size table, which gives it ${line.entry}.`
								: undefined
						),
						" "
					),
				});
			}
			return Arr.flatMapNullishOr(lines, (line) =>
				"unchecked" in line
					? { path: line.path, reason: line.unchecked }
					: undefined
			);
		}),
		startValues,
		search: (text, options) =>
			rows(
				NameRow,
				// An event setting is named by the saved value that says it was played, HavePlayedEvent.<setting>.
				"SELECT kind, name, about FROM (SELECT kind, name, about FROM name WHERE (?1 IS NULL OR kind = ?1) AND (name LIKE ?2 OR about LIKE ?2) UNION ALL SELECT 'event' AS kind, substr(name, 17) AS name, about FROM name WHERE (?1 IS NULL OR ?1 = 'event') AND kind = 'value' AND name LIKE 'HavePlayedEvent.%' AND substr(name, 17) LIKE ?2) ORDER BY length(name), name LIMIT ?3",
				options.kind ?? null,
				`%${text}%`,
				options.limit
			),
	});
});

/** A saved value's hash: its name's murmur3, or `0x` and the hash itself. */
export const valueHash = (name: string) =>
	Str.startsWith("0x")(name)
		? Number.parseInt(name, 16)
		: Bun.hash.murmur32v3(name);

const editDistance = (a: string, b: string) => {
	let previous = Arr.range(0, b.length);
	for (const [i, left] of [...a].entries()) {
		const current = [i + 1];
		for (const [j, right] of [...b].entries()) {
			current.push(
				Math.min(
					(previous[j + 1] as number) + 1,
					(current[j] as number) + 1,
					(previous[j] as number) + (left === right ? 0 : 1)
				)
			);
		}
		previous = current as Arr.NonEmptyArray<number>;
	}
	return previous[b.length] as number;
};

export const nearest = (asked: string, names: readonly string[]) =>
	Arr.map(
		Arr.take(
			Arr.sort(
				Arr.map(names, (name) => ({ name, far: editDistance(asked, name) })),
				Order.mapInput(
					Order.Number,
					(near: { readonly far: number }) => near.far
				)
			),
			5
		),
		(near) => near.name
	);
