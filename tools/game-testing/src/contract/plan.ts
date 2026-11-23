/**
 * What a start state writes into its base save: each value, what the base held, and where it came from. `plan` and
 * `plan_state` answer it, and a run's record keeps it.
 */
import { Schema } from "effect";
import { Saved } from "./values.ts";

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
 * Where a planned value came from: a word of the state and what it named, such as `withItems` and `Item_Range_Bow`;
 * the mod, whose new game starts the value so; or a rule of the library's own.
 */
export const Origin = Schema.Union([
	Schema.TaggedStruct("word", { word: Word, named: Schema.String }),
	Schema.TaggedStruct("mod", { mod: Schema.String }),
	Schema.TaggedStruct("rule", { rule: Schema.String }),
]);
export type Origin = typeof Origin.Type;

/** An origin as a sentence says it: `set PlayerStatus.CurrentRupee`, `the mod skips`, or the rule. */
export const originText = (origin: Origin) => {
	switch (origin._tag) {
		case "word":
			return `${origin.word} ${origin.named}`;
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
]);
export type PlannedEdit = typeof PlannedEdit.Type;

/**
 * What a state writes into its base, and what it leaves as the base holds it and nothing writes: a value named by the
 * files of a place it resets that is not the place's own, so a test that needs one adds it with `set`, or a value the
 * mod's new game starts otherwise that play moved in the base.
 */
export const StatePlan = Schema.Struct({
	base: Schema.Struct({ name: Schema.String, about: Schema.String }),
	edits: Schema.Array(PlannedEdit),
	leftAlone: Schema.Array(Schema.Struct({ name: Schema.String, from: Origin })),
});
export type StatePlan = typeof StatePlan.Type;
