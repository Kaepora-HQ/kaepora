/**
 * The start state: a base save plus words, all in the game's own names. The game's machine resolves every name
 * through the game's tables and refuses the whole state before the emulator starts.
 */
import { Schema } from "effect";
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
 * `exact` places him by hand.
 */
export const Spot = Schema.Union([
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
	at: Schema.optionalKey(Spot),
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

/** What the game holds after the start, read back from memory for every value the state wrote. */
export const ReadBack = Schema.Record(Schema.String, Value);

/** A mod to install alone in the game's place: a folder the patcher wrote. */
export const ModSource = Schema.Struct({ folder: Schema.String });
export type ModSource = typeof ModSource.Type;
