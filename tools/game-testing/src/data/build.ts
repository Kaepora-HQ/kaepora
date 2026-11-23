import { Database } from "bun:sqlite";
import {
	Array as Arr,
	Effect,
	Option,
	Order,
	Record as Rec,
	Schema,
	String as Str,
	Stream,
} from "effect";
import type { ChildProcessSpawner } from "effect/process";
import { ChildProcess } from "effect/process";
import { Saved } from "../contract/values.ts";
import { DumpFailed } from "../errors.ts";
import type { GameDataList } from "./dump.ts";
import {
	ActorInfo,
	Challenge,
	DungeonParam,
	FileLine,
	GameBancInfo,
	GameDataList as GameDataListSchema,
	Messages,
	Placements,
	PouchActorInfo,
	PouchRow as PouchRowSchema,
	StartPos,
	StringsLine,
	stem,
} from "./dump.ts";
import type { Point, SaveFileName, ValueType } from "./game-data.ts";

/** Which save file keeps a value, by the list's `SaveFileIndex`. */
export const saveFiles = ["progress", "option", "caption", "account"] as const;

export const makeDump = (
	spawner: ChildProcessSpawner.ChildProcessSpawner["Service"],
	program: string,
	game: string
) => {
	const exec = Effect.fn("GameData.dump")(function* (
		command: string,
		args: readonly string[],
		ok: readonly number[]
	) {
		const handle = yield* spawner
			.spawn(ChildProcess.make(program, [command, game, ...args]))
			.pipe(Effect.orDie);
		const [stdout, stderr, exitCode] = yield* Effect.all(
			[
				Stream.mkString(Stream.decodeText(handle.stdout)),
				Stream.mkString(Stream.decodeText(handle.stderr)),
				handle.exitCode,
			],
			{ concurrency: "unbounded" }
		).pipe(Effect.orDie);
		if (!Arr.contains(ok, exitCode)) {
			return yield* DumpFailed.make({ exitCode, stderr });
		}
		return { stdout, exitCode };
	}, Effect.scoped);
	const run = (command: string, ...args: readonly string[]) =>
		Effect.map(exec(command, args, [0]), (done) => done.stdout);
	const decode = <S extends Schema.Top & { readonly DecodingServices: never }>(
		schema: S,
		text: string
	) =>
		Schema.decodeEffect(Schema.fromJsonString(schema))(text).pipe(Effect.orDie);
	const lines = (text: string) =>
		Arr.filter(Str.split(text, "\n"), Str.isNonEmpty);
	/** No paths asks for nothing: `strings` with none answers for the whole game, and `files` refuses its command line. */
	const perPath = <S extends Schema.Top & { readonly DecodingServices: never }>(
		command: string,
		line: S,
		paths: readonly string[]
	) =>
		paths.length === 0
			? Effect.succeed([])
			: Effect.flatMap(run(command, ...paths), (text) =>
					Effect.forEach(lines(text), (each) => decode(line, each), {
						concurrency: 1,
					})
				);
	return {
		exec,
		run,
		decode,
		lines,
		/** In the order asked. */
		files: <S extends Schema.Top & { readonly DecodingServices: never }>(
			root: S,
			paths: readonly string[]
		) =>
			Effect.flatMap(perPath("files", FileLine, paths), (files) =>
				Effect.forEach(
					files,
					(file) =>
						Schema.decodeUnknownEffect(root)(file.root).pipe(Effect.orDie),
					{ concurrency: 1 }
				)
			),
		strings: (paths: readonly string[]) =>
			perPath("strings", StringsLine, paths),
	};
};
export type Dump = ReturnType<typeof makeDump>;

interface Read {
	readonly actorInfo: typeof ActorInfo.Type;
	readonly dungeons: typeof DungeonParam.Type;
	readonly list: GameDataList;
	readonly placements: readonly {
		readonly path: string;
		readonly file: typeof Placements.Type;
	}[];
	readonly pouch: typeof PouchActorInfo.Type;
	readonly quests: readonly string[];
	readonly scenes: typeof GameBancInfo.Type;
	readonly sources: readonly (typeof StringsLine.Type)[];
	readonly startPos: typeof StartPos.Type;
	readonly strings: readonly string[];
	readonly text: typeof Messages.Type;
}

/** A table of the game file, `<table>.Product.<version>.<kind>.zs`, not a member of a pack or a note beside it. */
const product = /^(?<table>.*)\.Product\.(?<version>\d+)\.[^/]*\.zs$/u;
const placementPath =
	/^Banc\/[^/]+\/[^/]+_(?:Static|Dynamic)\.bcett\.byml\.zs$/u;

export const readGame = Effect.fn("GameData.read")(function* (dump: Dump) {
	const pathList = yield* dump.run("list");
	const paths = dump.lines(pathList);
	/** The update's table over the base game's. */
	const newest = (name: string) =>
		Arr.last(
			Arr.sort(
				Arr.filter(paths, (path) => product.exec(path)?.groups?.table === name),
				Order.mapInput(Order.Number, (path: string) =>
					Number(product.exec(path)?.groups?.version)
				)
			)
		).pipe(
			Effect.fromOption,
			Effect.catch(() => Effect.die(new Error(`The game file has no ${name}.`)))
		);
	const file = <S extends Schema.Top & { readonly DecodingServices: never }>(
		root: S,
		path: string
	) => Effect.map(dump.files(root, [path]), (trees) => trees[0] as S["Type"]);
	const table = <S extends Schema.Top & { readonly DecodingServices: never }>(
		root: S,
		name: string
	) => Effect.flatMap(newest(name), (path) => file(root, path));
	const placementPaths = Arr.filter(paths, (path) => placementPath.test(path));
	const sourcePaths = Arr.filter(
		paths,
		(path) =>
			(Str.startsWith("Logic/")(path) && Str.endsWith(".ainb")(path)) ||
			(Str.startsWith("Event/EventFlow/")(path) &&
				Str.endsWith(".bfevfl.zs")(path))
	);
	const challenge = yield* table(Challenge, "RSDB/Challenge");
	const textPack = yield* newest("Mals/USen");
	const placements = yield* dump.files(Placements, placementPaths);
	const strings = yield* dump.run("strings");
	const read: Read = {
		actorInfo: yield* table(ActorInfo, "RSDB/ActorInfo"),
		pouch: yield* table(PouchActorInfo, "RSDB/PouchActorInfo"),
		quests: Arr.map(challenge, (row) => stem(row.__RowId)),
		dungeons: yield* table(DungeonParam, "RSDB/DungeonParam"),
		scenes: yield* table(GameBancInfo, "RSDB/GameBancInfo"),
		list: yield* table(GameDataListSchema, "GameData/GameDataList"),
		startPos: yield* file(StartPos, "Banc/BancBasedResource/StartPos.byml.zs"),
		text: yield* file(Messages, `${textPack}/ActorMsg/PouchContent.msbt`),
		placements: Arr.zipWith(placementPaths, placements, (path, tree) => ({
			path,
			file: tree,
		})),
		sources: yield* dump.strings(sourcePaths),
		strings: dump.lines(strings),
	};
	return read;
});

/** Radians about the vertical axis of Euler angles; angles of ±π about X and Z turn it to π − y. */
const yawOf = ([x, y, z]: Point) =>
	Math.abs(x) > Math.PI / 2 && Math.abs(z) > Math.PI / 2 ? Math.PI - y : y;

export const scalarGroups = [
	"Bool",
	"Int",
	"UInt",
	"Float",
	"Enum",
	"String16",
	"String32",
	"String64",
	"Vector3",
] as const satisfies readonly ValueType[];

interface Listed {
	readonly choices: readonly string[];
	readonly default: Saved;
	readonly file: SaveFileName;
	readonly hash: number;
	readonly type: ValueType;
}

/** Each value of a game data list with its default, as the cache keeps it. */
export const listed = (list: GameDataList): readonly Listed[] =>
	Arr.flatMap(scalarGroups, (type) =>
		Arr.map(list.Data[type], (entry): Listed => {
			const file = saveFiles[entry.SaveFileIndex] ?? "none";
			if ("RawValues" in entry) {
				const choice = Arr.findFirst(
					entry.RawValues,
					(raw) => Bun.hash.murmur32v3(raw) === entry.DefaultValue
				);
				return {
					type,
					hash: entry.Hash,
					file,
					choices: entry.RawValues,
					default: Option.getOrElse(choice, () => String(entry.DefaultValue)),
				};
			}
			const value = entry.DefaultValue;
			return {
				type,
				hash: entry.Hash,
				file,
				choices: [],
				default:
					typeof value === "object" ? [value.x, value.y, value.z] : value,
			};
		})
	);

const valueNames = (read: Read, values: readonly Listed[]) => {
	const kinds = Arr.dedupe(
		Arr.flatMapNullishOr(read.pouch, (row) => row.MaxNumGameData)
	);
	const candidates = [
		...read.strings,
		...Arr.flatMap(read.placements, ({ file }) =>
			Arr.map(file.Actors ?? [], (actor) => actor.Hash)
		),
		...Arr.map(read.quests, (quest) => `Step_${quest}`),
		"PouchContentMaxNum",
		...Arr.map(kinds, (kind) => `CurrentExpansionActor_${kind}`),
	];
	const byHash = new Map<number, string>();
	for (const candidate of candidates) {
		const hash = Bun.hash.murmur32v3(candidate);
		if (!byHash.has(hash)) {
			byHash.set(hash, candidate);
		}
	}
	const structs = new Map(
		Arr.map(read.list.Data.Struct, (struct) => [
			struct.Hash,
			struct.DefaultValue,
		])
	);
	const scalars = new Set(Arr.map(values, (value) => value.hash));
	const members = memberNames(structs, byHash);
	const names = new Map<number, string>();
	for (const hash of [...scalars, ...structs.keys()]) {
		const name = members.get(hash) ?? byHash.get(hash);
		if (name !== undefined) {
			names.set(hash, name);
		}
	}
	return names;
};

/** Names both hashes of a struct member check; a lone string's name of a value can be chance, so these win. */
const memberNames = (
	structs: ReadonlyMap<
		number,
		readonly { readonly Hash: number; readonly Value: number }[]
	>,
	byHash: ReadonlyMap<number, string>
) => {
	const members = new Map<number, string>();
	const roots = new Map<number, string>();
	for (const hash of structs.keys()) {
		const name = byHash.get(hash);
		if (name !== undefined) {
			roots.set(hash, name);
		}
	}
	const pending = [...roots.keys()];
	for (
		let struct = pending.pop();
		struct !== undefined;
		struct = pending.pop()
	) {
		const parent = (members.get(struct) ?? roots.get(struct)) as string;
		for (const member of structs.get(struct) ?? []) {
			const field = byHash.get(member.Hash);
			const full = `${parent}.${field}`;
			if (
				field !== undefined &&
				!members.has(member.Value) &&
				Bun.hash.murmur32v3(full) === member.Value
			) {
				members.set(member.Value, full);
				if (structs.has(member.Value)) {
					pending.push(member.Value);
				}
			}
		}
	}
	return members;
};

const createStatements = [
	"CREATE TABLE value (hash INTEGER PRIMARY KEY, name TEXT, type TEXT NOT NULL, default_value TEXT NOT NULL, file TEXT NOT NULL, choices TEXT NOT NULL)",
	"CREATE TABLE actor_key (key TEXT PRIMARY KEY)",
	"CREATE TABLE item (name TEXT PRIMARY KEY, saved TEXT NOT NULL, row TEXT NOT NULL)",
	"CREATE TABLE scene (name TEXT PRIMARY KEY, folder TEXT NOT NULL, dungeon TEXT NOT NULL, rooms INTEGER NOT NULL, outdoors INTEGER NOT NULL)",
	"CREATE TABLE landing (name TEXT PRIMARY KEY, scene TEXT NOT NULL, x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL, yaw REAL NOT NULL)",
	"CREATE TABLE placement (hash TEXT NOT NULL, scene TEXT NOT NULL, actor TEXT NOT NULL, name TEXT, room INTEGER, x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL, yaw REAL NOT NULL)",
	"CREATE INDEX placement_scene ON placement (scene, room)",
	"CREATE INDEX placement_hash ON placement (hash)",
	"CREATE INDEX placement_actor ON placement (actor)",
	"CREATE TABLE binding (module TEXT NOT NULL, actor TEXT NOT NULL)",
	"CREATE INDEX binding_actor ON binding (actor)",
	"CREATE TABLE module_child (module TEXT NOT NULL, child TEXT NOT NULL)",
	"CREATE INDEX module_child_module ON module_child (module)",
	"CREATE TABLE named (kind TEXT NOT NULL, source TEXT NOT NULL, name TEXT NOT NULL)",
	"CREATE INDEX named_source ON named (kind, source)",
	"CREATE TABLE name (kind TEXT NOT NULL, name TEXT NOT NULL, about TEXT NOT NULL, PRIMARY KEY (kind, name))",
];

export const SavedJson = Schema.fromJsonString(Saved);
export const ChoicesJson = Schema.fromJsonString(Schema.Array(Schema.String));
export const PouchRowJson = Schema.fromJsonString(PouchRowSchema);

type Column = string | number | null;
type Rows = readonly (readonly Column[])[];

const placementFile = /\/(?<scene>[^/]+)_(?:Static|Dynamic)\.bcett\.byml\.zs$/u;
const textCode = /\{[^}]*\}/gu;

const sceneRows = (read: Read): Rows => {
	const dungeons = new Map(Arr.map(read.dungeons, (row) => [row.__RowId, row]));
	return Arr.map(read.scenes, (row) => {
		const folder = Str.split(row.BancMain, "/")[3] as string;
		const dungeon = dungeons.get(row.__RowId) ?? dungeons.get(folder);
		// A boss stage has only its folder's row; the map marker belongs to a field of its own or to no row at all.
		const own = dungeons.get(row.__RowId);
		const outdoors =
			dungeon === undefined || (own !== undefined && own.IsFieldData === true);
		return [
			row.__RowId,
			folder,
			dungeon?.__RowId ?? "",
			dungeon?.RoomParam.length ?? 0,
			outdoors ? 1 : 0,
		];
	});
};

const landingRows = (read: Read): Rows =>
	Arr.flatMap(Rec.toEntries(read.startPos), ([scene, spots]) =>
		Arr.map(Rec.toEntries(spots), ([landing, spot]) => [
			landing === "" ? scene : `${scene}/${landing}`,
			scene,
			...spot.Trans,
			yawOf(spot.Rot),
		])
	);

const placementRows = (read: Read): Rows =>
	Arr.flatMap(read.placements, ({ path, file }) =>
		Arr.map(file.Actors ?? [], (actor) => [
			actor.Hash,
			placementFile.exec(path)?.groups?.scene as string,
			actor.Gyaml,
			actor.Name ?? null,
			actor.Dynamic?.DefaultDungeonRoomIndex ?? null,
			...(actor.Translate ?? [0, 0, 0]),
			yawOf(actor.Rotate ?? [0, 0, 0]),
		])
	);

const bindingRows = (read: Read): Rows =>
	Arr.flatMap(read.placements, ({ file }) =>
		Arr.flatMap(file.AiGroups ?? [], (group) => {
			const modules = Arr.dedupe([
				stem(group.Logic),
				...Arr.flatMapNullishOr(group.References, (reference) =>
					reference.Logic === undefined ? undefined : stem(reference.Logic)
				),
			]);
			return Arr.flatMap(modules, (module) =>
				Arr.map(group.References, (reference) => [module, reference.Reference])
			);
		})
	);

const kindOf = (path: string) =>
	Str.startsWith("Logic/")(path) ? "module" : "flow";

const childRows = (read: Read): Rows => {
	const modules = new Set(
		Arr.flatMapNullishOr(read.sources, (source) =>
			kindOf(source.path) === "module" ? stem(source.path) : undefined
		)
	);
	return Arr.flatMap(read.sources, (source) =>
		kindOf(source.path) === "module"
			? Arr.flatMapNullishOr(source.strings, (text) =>
					modules.has(text) && text !== stem(source.path)
						? [stem(source.path), text]
						: undefined
				)
			: []
	);
};

const namedRows = (read: Read, scalars: ReadonlySet<number>): Rows =>
	Arr.flatMap(read.sources, (source) =>
		Arr.flatMapNullishOr(source.strings, (text) =>
			scalars.has(Bun.hash.murmur32v3(text))
				? [kindOf(source.path), stem(source.path), text]
				: undefined
		)
	);

export const fill = Effect.fn("GameData.fill")(function* (
	path: string,
	read: Read
) {
	const values = listed(read.list);
	const valueNamesByHash = valueNames(read, values);
	const scalars = new Set(Arr.map(values, (value) => value.hash));
	const actorName = new Map(
		Arr.map(read.actorInfo, (row) => [
			row.__RowId,
			row.ActorName ?? row.__RowId,
		])
	);
	const englishName = new Map(
		Arr.map(read.text, (message) => [
			message.label,
			Str.trim(Str.replaceAll(textCode, "")(message.text)),
		])
	);
	const valueTable = yield* Effect.forEach(
		values,
		(entry) =>
			Effect.all(
				[
					Schema.encodeEffect(SavedJson)(entry.default),
					Schema.encodeEffect(ChoicesJson)(entry.choices),
				],
				{ concurrency: 1 }
			).pipe(
				Effect.orDie,
				Effect.map(([encoded, choices]): readonly Column[] => [
					entry.hash,
					valueNamesByHash.get(entry.hash) ?? null,
					entry.type,
					encoded,
					entry.file,
					choices,
				])
			),
		{ concurrency: 1 }
	);
	const item = yield* Effect.forEach(
		read.pouch,
		(row) =>
			Effect.map(
				Schema.encodeEffect(PouchRowJson)(row).pipe(Effect.orDie),
				(encoded): readonly Column[] => [
					row.__RowId,
					actorName.get(row.__RowId) ?? row.__RowId,
					encoded,
				]
			),
		{ concurrency: 1 }
	);
	const scene = sceneRows(read);
	const landing = landingRows(read);
	const tables: Readonly<Record<string, Rows>> = {
		value: valueTable,
		actor_key: Arr.map(read.list.Data.Bool64bitKey, (entry) => [entry.Hash]),
		item,
		scene,
		landing,
		placement: placementRows(read),
		binding: bindingRows(read),
		module_child: childRows(read),
		named: namedRows(read, scalars),
		name: [
			...Arr.flatMapNullishOr([...valueNamesByHash], ([hash, name]) =>
				scalars.has(hash) ? ["value", name, ""] : undefined
			),
			...Arr.map(item, ([name, saved]) => [
				"item",
				name as string,
				englishName.get(`${saved as string}_Name`) ?? "",
			]),
			...Arr.map(read.quests, (quest) => ["quest", quest, ""]),
			...Arr.map(scene, ([name, folder]) => [
				"scene",
				name as string,
				folder as string,
			]),
			...Arr.map(landing, ([name, landingScene]) => [
				"landing",
				name as string,
				landingScene as string,
			]),
		],
	};
	yield* Effect.sync(() => {
		const db = new Database(path, { create: true, strict: true });
		db.run("PRAGMA journal_mode = OFF");
		for (const statement of createStatements) {
			db.run(statement);
		}
		db.transaction(() => {
			for (const [table, rows] of Rec.toEntries(tables)) {
				const [first] = rows;
				if (first === undefined) {
					continue;
				}
				const insert = db.prepare(
					`INSERT INTO ${table} VALUES (${Arr.join(Arr.replicate("?", first.length), ", ")})`
				);
				for (const row of rows) {
					insert.run(...row);
				}
				insert.finalize();
			}
		})();
		db.close();
	});
});
