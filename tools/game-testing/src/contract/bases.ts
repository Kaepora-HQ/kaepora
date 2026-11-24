/**
 * The saves a start is built on and the starts named in git: a base is a save the game wrote, kept under a name with the
 * SHA-256 of each of its files; a named start is a base and words, under a name of its own.
 */
import { Schema, String as Str, Struct } from "effect";
import { Age, Location, RunSave, StartState } from "./state.ts";
import { RunId, SaveNumber } from "./values.ts";

/** A base's or a start's name: lowercase words joined by hyphens, which a folder and a table's cell both take. */
export const BaseName = Schema.String.check(
	Schema.isPattern(/^[a-z0-9]+(?:-[a-z0-9]+)*$/u)
);

const lineBreakOrTab = /[\t\n\r]/u;

/** A line on what a base or a start holds, with no tab or line break, which would break its table. */
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

/**
 * A start by name: a base by its name, and the words a look adds to it, kept in `tables/starts.json` so many looks
 * share one start. A look that names it adds its own words: a word that holds one value, such as `location`, `age` or
 * one value of `set`, is the look's where both give it, and the lists of both join.
 */
export const NamedStart = Schema.Struct({
	name: BaseName,
	about: About,
	base: BaseName,
	...Struct.omit(StartState.fields, ["base"]),
});
export type NamedStart = typeof NamedStart.Type;

/** Two lists of a word joined, the start's first; absent when neither gives it. */
const both = <A>(
	start: readonly A[] | undefined,
	look: readonly A[] | undefined
) =>
	start === undefined || look === undefined
		? (start ?? look)
		: [...start, ...look];

/** A look's state with a named start's words joined to its own, on the named start's base. */
export const joined = (start: NamedStart, look: StartState) => {
	const word = {
		location: look.location ?? start.location,
		age: look.age ?? start.age,
		startValues: look.startValues ?? start.startValues,
		withItems: both(start.withItems, look.withItems),
		withoutItems: both(start.withoutItems, look.withoutItems),
		equipped: both(start.equipped, look.equipped),
		reset: both(start.reset, look.reset),
		set: look.set === undefined ? start.set : { ...start.set, ...look.set },
	};
	const quests =
		look.story?.quests === undefined
			? start.story?.quests
			: { ...start.story?.quests, ...look.story.quests };
	const events = both(start.story?.events, look.story?.events);
	return StartState.make({
		base: start.base,
		...(word.location === undefined ? {} : { location: word.location }),
		...(word.age === undefined ? {} : { age: word.age }),
		...(word.startValues === undefined
			? {}
			: { startValues: word.startValues }),
		...(word.withItems === undefined ? {} : { withItems: word.withItems }),
		...(word.withoutItems === undefined
			? {}
			: { withoutItems: word.withoutItems }),
		...(word.equipped === undefined ? {} : { equipped: word.equipped }),
		...(word.reset === undefined ? {} : { reset: word.reset }),
		...(word.set === undefined ? {} : { set: word.set }),
		...(quests === undefined && events === undefined
			? {}
			: {
					story: {
						...(quests === undefined ? {} : { quests }),
						...(events === undefined ? {} : { events }),
					},
				}),
	});
};

/** Where Link stands in a base, as its save holds it; for a start, its own `location` if it gives one. */
export const Stand = Location;

/** Every base and every named start, each with what an agent choosing a start reads. */
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
	/** Each start as `tables/starts.json` gives it, with the age, the hour and the place its look starts at. */
	starts: Schema.Array(
		Schema.Struct({
			start: NamedStart,
			age: Age,
			hour: Hour,
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
