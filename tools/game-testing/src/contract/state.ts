/**
 * The start state: a base save plus words, all in the game's own names. The game's machine resolves every name
 * through the game's tables and refuses the whole state before the emulator starts.
 */
import { Array as Arr, Schema, String as Str } from "effect";
import { Value } from "./values.ts";

/** Swapping age also swaps the sword and shield, as the game does. */
export const Age = Schema.Literals(["child", "adult"]);
export type Age = typeof Age.Type;

/** An item, held once or with a count, such as `Item_Bomb` with 20. */
export const Holding = Schema.Union([
	Schema.String,
	Schema.Struct({
		item: Schema.String,
		count: Schema.Int.check(Schema.isGreaterThanOrEqualTo(0)),
	}),
]);

/**
 * Where Link stands. A name is looked up in this order: a row of `tables/spots.tsv` (a spot play showed to be floor),
 * a check (Link stands before its subject), a landing of `StartPos`. A name two of them hold is refused, with both.
 * A scene, room, position and facing place him by hand.
 */
export const Location = Schema.Union([
	Schema.String,
	Schema.Struct({
		scene: Schema.String,
		room: Schema.optionalKey(Schema.Int),
		position: Schema.Tuple([Schema.Finite, Schema.Finite, Schema.Finite]),
		/** Degrees, 0 facing the scene's +Z. */
		facing: Schema.Finite,
	}),
]);

/**
 * A place or person as on a first visit: a check, a scene, a scene and room (`ForestTemple/17`), or a person by actor
 * (`Npc_SkullDad`). Every value its placements, bound logic modules and flows name goes back to the list's default.
 */
export const Subject = Schema.String;

/** Quest steps and played events, by the game's names. */
export const Story = Schema.Struct({
	/** Quest to step, such as `GatheringTokenGold` to `CollectToken`; written to `Step_<quest>`. */
	quests: Schema.optionalKey(Schema.Record(Schema.String, Schema.String)),
	/** Event settings to mark played, written to `HavePlayedEvent.<setting>`. */
	events: Schema.optionalKey(Schema.Array(Schema.String)),
});

export class StartState extends Schema.Class<
	StartState,
	{ readonly brand: unique symbol }
>("game-testing/contract/state/StartState")({
	/** A row of `tables/bases.tsv`, such as `new-game`; the base's hour is the run's hour. */
	base: Schema.String,
	location: Schema.optionalKey(Location),
	age: Schema.optionalKey(Age),
	/** Items held, with every record, container, tier and limit the pouch table joins to them. */
	has: Schema.optionalKey(Schema.Array(Holding)),
	/** Items not held, with the same expansion; tiers above a lacked tier go too. */
	lacks: Schema.optionalKey(Schema.Array(Schema.String)),
	/** Items worn or assigned, in the slot their pouch row names. */
	equip: Schema.optionalKey(Schema.Array(Schema.String)),
	fresh: Schema.optionalKey(Schema.Array(Subject)),
	story: Schema.optionalKey(Story),
	/** Saved values by name, written last. */
	set: Schema.optionalKey(Schema.Record(Schema.String, Value)),
}) {}

/**
 * What the game held once Link could move, of every value the state wrote: each one read from its memory, and the ones
 * it could not be read for, a per-actor key by `key <hash>`.
 */
export const ReadBack = Schema.Struct({
	values: Schema.Record(Schema.String, Value),
	notRead: Schema.Array(Schema.String),
});

/** A mod's folder on the machine that reads it, such as one the patcher wrote for a seed. */
export const ModFolder = Schema.Struct({ folder: Schema.String });
export type ModFolder = typeof ModFolder.Type;

/** One part of a path, which cannot name its folder or the one above it. */
const isPart = (part: string) => part !== "" && part !== "." && part !== "..";

/** A mod's name, its folder's own name: one part of a path. */
export const ModName = Schema.String.check(
	Schema.makeFilter((name) =>
		isPart(name) && !Str.includes("/")(name)
			? undefined
			: "a folder's own name, with no /, and not . or .."
	)
);

/** A file's path inside its mod's folder, in parts joined by `/`, so it can name nothing outside the folder. */
export const ModPath = Schema.String.check(
	Schema.makeFilter((path) =>
		Arr.every(Str.split(path, "/"), isPart)
			? undefined
			: "a path inside the mod's folder, in parts joined by /, none of them empty, . or .."
	)
);

/**
 * The most a start's message carries to a far machine's `serve`, whose WebSocket refuses a larger one, and the room the
 * rest of a start takes beside its mod. A mod's bytes travel as base64, a third larger.
 */
export const startLimit = 64 * 1024 * 1024;
export const modLimit = startLimit - 1024 * 1024;

/** A mod as it travels to the game's machine: its name, and each of its files with its bytes. */
export const Mod = Schema.Struct({
	name: ModName,
	files: Schema.NonEmptyArray(
		Schema.Struct({ path: ModPath, bytes: Schema.Uint8Array })
	),
});
export type Mod = typeof Mod.Type;

/** How the game writes its text: at once, through Lichii's instant text add-on as a player has it, or at its own speed. */
export const TextSpeed = Schema.Literals(["instant", "game"]);
export type TextSpeed = typeof TextSpeed.Type;
