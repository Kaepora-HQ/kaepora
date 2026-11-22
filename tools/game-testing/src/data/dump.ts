/**
 * `kaepora-dump` prints one line per answer, JSON or a bare path or name. These schemas name only the fields the library
 * reads.
 *
 * - `key <game.nup>`: `{"build", "format", "key"}`, the program's build id, the format's version and the SHA-256 of
 *   the container's table of contents.
 * - `list <game.nup> [prefix]`: each path that starts with the prefix, and after each pack its members that do,
 *   as `<pack path>/<member>`.
 * - `files <game.nup> <path>...`: `{"path", "root", "sha256"}` per path. BYML prints as its tree, a 64-bit value as
 *   decimal text, a message file as `[{"label", "text"}]`.
 * - `check-mod <game.nup> <mod-dir>`: per mod file in path order, `{"entry", "needs", "path"}` for a file the game
 *   would drop, or `{"path", "unchecked"}` with the reason it could not be measured. It exits 3 when a file is too
 *   large, 0 otherwise, and 1 on an error.
 * - `strings <game.nup> [path...]`: with no path, each distinct run of `[A-Za-z0-9_]` of three or more characters in
 *   the files that name saved values, one per line; with paths, `{"path", "strings"}` per path.
 */
import { Array as Arr, Schema, SchemaGetter, String as Str } from "effect";

export const dumpFormat = 1;

export const Key = Schema.Struct({
	build: Schema.String,
	format: Schema.Literal(dumpFormat),
	key: Schema.String,
});

export const FileLine = Schema.Struct({
	path: Schema.String,
	root: Schema.Unknown,
});

export const ModLine = Schema.Union([
	Schema.Struct({ path: Schema.String, needs: Schema.Int, entry: Schema.Int }),
	Schema.Struct({ path: Schema.String, unchecked: Schema.String }),
]);

export const modTooLarge = 3;

export const StringsLine = Schema.Struct({
	path: Schema.String,
	strings: Schema.Array(Schema.String),
});

export const stem = (path: string) =>
	Str.split(Arr.lastNonEmpty(Str.split(path, "/")), ".")[0] as string;

const actorPath = (name: string) =>
	`Work/Actor/${name}.engine__actor__ActorParam.gyml`;

const ActorPath = Schema.String.pipe(
	Schema.decodeTo(Schema.String, {
		decode: SchemaGetter.transform(stem),
		encode: SchemaGetter.transform(actorPath),
	})
);

/** Actor paths, comma separated. */
const ActorPaths = Schema.String.pipe(
	Schema.decodeTo(Schema.Array(Schema.String), {
		decode: SchemaGetter.transform((text: string) =>
			Arr.map(Str.split(text, ","), stem)
		),
		encode: SchemaGetter.transform((names: readonly string[]) =>
			Arr.join(Arr.map(names, actorPath), ",")
		),
	})
);

/** `RSDB/ActorInfo`: which actor the save records an item under. */
export const ActorInfo = Schema.Array(
	Schema.Struct({
		__RowId: Schema.String,
		ActorName: Schema.optionalKey(Schema.String),
	})
);

export const PouchRow = Schema.Struct({
	__RowId: Schema.String,
	PouchCategory: Schema.optionalKey(Schema.String),
	PouchSpecialDeal: Schema.optionalKey(Schema.String),
	ShootableActor: Schema.optionalKey(ActorPath),
	SmallBagActor: Schema.optionalKey(ActorPath),
	MaxNumGameData: Schema.optionalKey(Schema.String),
	/** The actors a container fills. */
	ExpansionMaxNumActor: Schema.optionalKey(ActorPaths),
	AfterExpansionMaxNum: Schema.optionalKey(Schema.Int),
	/** A container's ammunition that it fills only once the ammunition's own IsGet is set. */
	ActorFullOnlyIfIsGetWhenGetBag: Schema.optionalKey(ActorPath),
	/** The item keeps a count. */
	PouchStockable: Schema.optionalKey(Schema.Boolean),
	AssignableItem: Schema.optionalKey(Schema.String),
	ExpansionAvility: Schema.optionalKey(Schema.String),
	LiftableGrade: Schema.optionalKey(Schema.String),
	DiveDepth: Schema.optionalKey(Schema.Finite),
	OcarinaTypeValue: Schema.optionalKey(Schema.Int),
	AdultEquipment: Schema.optionalKey(ActorPath),
	ChildEquipment: Schema.optionalKey(ActorPath),
});
export type PouchRow = typeof PouchRow.Type;

/** `RSDB/PouchActorInfo`: what an item fills, what holds it, its tiers, its limit and its button. */
export const PouchActorInfo = Schema.Array(PouchRow);

/** `RSDB/Challenge`: the quests, by a path whose name is the quest's. */
export const Challenge = Schema.Array(
	Schema.Struct({ __RowId: Schema.String })
);

/** `RSDB/DungeonParam`: the scenes and folders with rooms; a field row is outdoors. */
export const DungeonParam = Schema.Array(
	Schema.Struct({
		__RowId: Schema.String,
		RoomParam: Schema.Array(Schema.Unknown),
		IsFieldData: Schema.optionalKey(Schema.Boolean),
	})
);

/** `RSDB/GameBancInfo`: every scene, with its folder in `BancMain`, `Work/Banc/Scene/<folder>/<scene>.bcett.json`. */
export const GameBancInfo = Schema.Array(
	Schema.Struct({ __RowId: Schema.String, BancMain: Schema.String })
);

const Point = Schema.Tuple([Schema.Finite, Schema.Finite, Schema.Finite]);

const Entry = <Default extends Schema.Top>(value: Default) =>
	Schema.Struct({
		Hash: Schema.Int,
		DefaultValue: value,
		SaveFileIndex: Schema.Int,
	});

/** `GameData/GameDataList`: every saved value by hash, grouped by type, with its default and the file that saves it. */
export const GameDataList = Schema.Struct({
	Data: Schema.Struct({
		Bool: Schema.Array(Entry(Schema.Boolean)),
		Int: Schema.Array(Entry(Schema.Int)),
		UInt: Schema.Array(Entry(Schema.Int)),
		Float: Schema.Array(Entry(Schema.Finite)),
		Enum: Schema.Array(
			Schema.Struct({
				Hash: Schema.Int,
				DefaultValue: Schema.Int,
				SaveFileIndex: Schema.Int,
				RawValues: Schema.Array(Schema.String),
			})
		),
		String16: Schema.Array(Entry(Schema.String)),
		String32: Schema.Array(Entry(Schema.String)),
		String64: Schema.Array(Entry(Schema.String)),
		Vector3: Schema.Array(
			Entry(
				Schema.Struct({ x: Schema.Finite, y: Schema.Finite, z: Schema.Finite })
			)
		),
		/** Members as (the member's hash, the hash of the value `<struct>.<member>`). */
		Struct: Schema.Array(
			Schema.Struct({
				Hash: Schema.Int,
				DefaultValue: Schema.Array(
					Schema.Struct({ Hash: Schema.Int, Value: Schema.Int })
				),
			})
		),
		/** The per-actor keys, by 64-bit hash. */
		Bool64bitKey: Schema.Array(Schema.Struct({ Hash: Schema.String })),
	}),
});
export type GameDataList = typeof GameDataList.Type;

/** `Banc/BancBasedResource/StartPos`: each scene's landings by name, with Euler angles in radians. */
export const StartPos = Schema.Record(
	Schema.String,
	Schema.Record(Schema.String, Schema.Struct({ Rot: Point, Trans: Point }))
);

export const Messages = Schema.Array(
	Schema.Struct({ label: Schema.String, text: Schema.String })
);

export const Placements = Schema.Struct({
	Actors: Schema.optionalKey(
		Schema.Array(
			Schema.Struct({
				Gyaml: Schema.String,
				Hash: Schema.String,
				/** An anchor's or a named actor's own name, such as `HidingHole_FieldEntranceGo`. */
				Name: Schema.optionalKey(Schema.String),
				/** Absent for an actor at the origin. */
				Translate: Schema.optionalKey(Point),
				Rotate: Schema.optionalKey(Point),
				Dynamic: Schema.optionalKey(
					Schema.Struct({
						DefaultDungeonRoomIndex: Schema.optionalKey(Schema.Int),
					})
				),
			})
		)
	),
	/** Logic modules and the actors each is bound to. */
	AiGroups: Schema.optionalKey(
		Schema.Array(
			Schema.Struct({
				Logic: Schema.String,
				References: Schema.Array(
					Schema.Struct({
						Reference: Schema.String,
						Logic: Schema.optionalKey(Schema.String),
					})
				),
			})
		)
	),
});
