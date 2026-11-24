/** The saves a start is built on: a base is a save the game wrote, kept under a name with the SHA-256 of each of its files. */
import { Schema, String as Str } from "effect";
import { Age, Location, RunSave } from "./state.ts";
import { RunId, SaveNumber } from "./values.ts";

/** A base's name: lowercase words joined by hyphens, which a folder and a table's cell both take. */
export const BaseName = Schema.String.check(
	Schema.isPattern(/^[a-z0-9]+(?:-[a-z0-9]+)*$/u)
);

const lineBreakOrTab = /[\t\n\r]/u;

/** A line on what a base holds, with no tab or line break, which would break its table. */
export const About = Schema.String.check(
	Schema.makeFilter((about) =>
		Str.isNonEmpty(Str.trim(about)) && !lineBreakOrTab.test(about)
			? undefined
			: "a line of text, with no tab or line break"
	)
);

export const Hour = Schema.Literals(["day", "night"]);

/** A save a run kept: the run, and the save's number in its folder, `save-<n>-progress.sav`. */
export const KeptFrom = Schema.Struct({ run: RunId, save: SaveNumber });
export type KeptFrom = typeof KeptFrom.Type;

/** A run's save as a sentence names it. */
export const keptText = (from: KeptFrom) =>
	`save ${from.save} of run ${from.run}`;

/**
 * A base: a save the game wrote, by its name, with the SHA-256 of its three files. One named from a run's save keeps
 * which, and that run's record holds how the game got there.
 */
export const Base = Schema.Struct({
	name: BaseName,
	hour: Hour,
	age: Age,
	/** The SHA-256 of its progress.sav. */
	progress: Schema.String,
	/** The SHA-256 of its caption.sav. */
	caption: Schema.String,
	/** The SHA-256 of its option.sav, empty for a base whose option.sav was not kept. */
	option: Schema.String,
	about: About,
	from: Schema.optionalKey(KeptFrom),
});
export type Base = typeof Base.Type;

/** Where Link stands in a base, as its save holds it. */
export const Stand = Location;

/** Every base, each with what an agent choosing a start reads. */
export const Catalog = Schema.Struct({
	bases: Schema.Array(
		Schema.Struct({
			...Base.fields,
			/** Whether its row is recorded on this machine and not yet in git. */
			recorded: Schema.Boolean,
			/** Absent when its files are not on this machine. */
			stands: Schema.optionalKey(Stand),
		})
	),
});
export type Catalog = typeof Catalog.Type;

/** A save's own files, such as a folder of play holds: a base named from them without option.sav takes the new game's. */
export const SaveFiles = Schema.Struct({
	progress: Schema.Uint8Array,
	caption: Schema.Uint8Array,
	option: Schema.optionalKey(Schema.Uint8Array),
});

/** What a base is named from: a save a run kept, or a save's own files. */
export const BaseSource = Schema.Union([RunSave, SaveFiles]);
export type BaseSource = typeof BaseSource.Type;

/** A save to keep as a base, under a name and a line on what it holds, with the hour it was saved at. */
export const NewBase = Schema.Struct({
	name: BaseName,
	about: About,
	hour: Hour,
	source: BaseSource,
});
export type NewBase = typeof NewBase.Type;
