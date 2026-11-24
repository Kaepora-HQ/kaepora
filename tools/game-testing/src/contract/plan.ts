/**
 * What a start state writes into its base save: each value, what the base held, and where it came from. `plan` and
 * `plan_state` answer it, and a run's record keeps it.
 */
import { Schema } from "effect";
import { KeptFrom, keptText } from "./bases.ts";
import { StartSpot } from "./spots.ts";
import { Saved } from "./values.ts";

/** The types of a saved value the library reads and writes. */
export const ValueType = Schema.Literals([
	"Bool",
	"Int",
	"UInt",
	"Float",
	"Enum",
	"String16",
	"String32",
	"String64",
	"Vector3",
]);

/** The words of a start state that write values. */
export const Word = Schema.Literals([
	"location",
	"age",
	"withItems",
	"withoutItems",
	"equipped",
	"reset",
	"story",
	"set",
]);

/**
 * Where a planned value came from: a word of the state and what it named, such as `withItems` and `Item_Range_Bow`; a
 * location by name and its kind of spot; the mod, whose new game starts the value so; or a rule of the library's own.
 */
export const Origin = Schema.Union([
	Schema.TaggedStruct("word", { word: Word, named: Schema.String }),
	/** A location by name, and the kind of spot it stands Link on, such as a row of `tables/spots.tsv` found in a save. */
	Schema.TaggedStruct("spot", {
		name: Schema.String,
		kind: StartSpot.fields.kind,
	}),
	Schema.TaggedStruct("mod", { mod: Schema.String }),
	Schema.TaggedStruct("rule", { rule: Schema.String }),
]);
export type Origin = typeof Origin.Type;

/** An origin as a sentence says it: `set PlayerStatus.CurrentRupee`, `the mod skips`, or the rule. */
export const originText = (origin: Origin) => {
	switch (origin._tag) {
		case "word":
			return `${origin.word} ${origin.named}`;
		case "spot":
			return `location ${origin.name}, a ${origin.kind} spot`;
		case "mod":
			return `the mod ${origin.mod}`;
		default:
			return origin.rule;
	}
};

/**
 * One record a state writes, with what the base held and where it came from: a saved value by name, in
 * `progress.sav` or `caption.sav`, or a per-actor key by its placement hash in decimal.
 */
export const PlannedEdit = Schema.Union([
	Schema.TaggedStruct("value", {
		name: Schema.String,
		file: Schema.Literals(["progress", "caption"]),
		to: Saved,
		base: Saved,
		from: Origin,
	}),
	Schema.TaggedStruct("key", {
		key: Schema.String,
		to: Schema.Boolean,
		base: Schema.Boolean,
		from: Origin,
	}),
	/**
	 * A value the mod adds to the game's list, such as a seed's `Kaepora_Once_` flags, by its hash. A base has no record
	 * for it and nothing is written: the game loads a save with no record for a value at the list's default, so the look
	 * starts with it at `to`, as a new game of the mod does.
	 */
	Schema.TaggedStruct("added", {
		name: Schema.String,
		file: Schema.Literals(["progress", "caption"]),
		type: ValueType,
		choices: Schema.optionalKey(Schema.Array(Schema.String)),
		to: Saved,
		from: Origin,
	}),
]);
export type PlannedEdit = typeof PlannedEdit.Type;

/**
 * The save a start was built on: a base by its name, with the run's save it was named from if it was; or a save a run
 * kept, which the state named itself.
 */
export const PlannedBase = Schema.Union([
	Schema.Struct({
		name: Schema.String,
		about: Schema.String,
		from: Schema.optionalKey(KeptFrom),
	}),
	Schema.Struct({ about: Schema.String, from: KeptFrom }),
]);
export type PlannedBase = typeof PlannedBase.Type;

/** A base as a sentence names it: its name, or the run's save it is. */
export const baseText = (base: PlannedBase) =>
	"name" in base ? base.name : keptText(base.from);

/**
 * What a state writes into its base, and what it leaves as the base holds it and nothing writes: a value named by the
 * files of a place it resets that is not the place's own, so a test that needs one adds it with `set`, or a value the
 * mod's new game starts otherwise that play moved in the base.
 */
export const StatePlan = Schema.Struct({
	base: PlannedBase,
	/** Where the state's location stands Link, and what kind of spot that is. */
	location: Schema.optionalKey(StartSpot),
	edits: Schema.Array(PlannedEdit),
	leftAlone: Schema.Array(Schema.Struct({ name: Schema.String, from: Origin })),
});
export type StatePlan = typeof StatePlan.Type;
