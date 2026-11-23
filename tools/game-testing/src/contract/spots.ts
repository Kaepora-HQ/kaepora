/**
 * Where Link starts for a location by name. A spot is a row of `tables/spots.tsv`, or of the recorded table beside it on
 * the machine with the game, which wins. Its scene says which age it serves: a scene whose name carries `_Child` or
 * `_Adult` serves that age, any other both.
 */
import { Schema } from "effect";
import { Position, RunId } from "./values.ts";

/**
 * How a spot was found: `worked out` one unit before the check's actor, from a `save` the game wrote, or from `play`,
 * read from a running game.
 */
export const Found = Schema.Literals(["worked out", "save", "play"]);
export type Found = typeof Found.Type;

/** A look that started on the spot, and how it ended. */
export const Look = Schema.Struct({
	outcome: Schema.Literals(["passed", "failed"]),
	run: RunId,
});
export type Look = typeof Look.Type;

/** A spot's name, which a table holds in one cell: no tab and no line break. */
export const SpotName = Schema.String.check(
	Schema.isPattern(/^[^\t\r\n]+$/u, {
		expected: "a name with no tab or line break",
	})
);

export const Spot = Schema.Struct({
	/** A check's spoiler log name, or a spot's own name. */
	name: SpotName,
	scene: Schema.String,
	room: Schema.optionalKey(Schema.Int),
	position: Position,
	/** Degrees: 0 faces the scene's +Z, 90 its +X. */
	facing: Schema.Finite,
	found: Found,
	/** The save, the run or the game it was found in, by a path inside the repository's folders or in words. */
	evidence: Schema.String,
	look: Schema.optionalKey(Look),
	/** What stands in the way there, in words. */
	note: Schema.String,
});
export type Spot = typeof Spot.Type;

/** What a spot is worth: `proven` once a look passed from it, `found` when the game showed it, else `worked out`. */
export const SpotKind = Schema.Literals(["worked out", "found", "proven"]);
export type SpotKind = typeof SpotKind.Type;

export const kindOf = (spot: Spot): SpotKind => {
	if (spot.look?.outcome === "passed") {
		return "proven";
	}
	return spot.found === "worked out" ? "worked out" : "found";
};

/** Where a start stands Link: a spot's kind, a `landing` of the game, or a place the state `given` by hand. */
export const StartSpot = Schema.Struct({
	scene: Schema.String,
	room: Schema.optionalKey(Schema.Int),
	position: Position,
	facing: Schema.Finite,
	kind: Schema.Literals(["given", "landing", ...SpotKind.literals]),
	/** How it was found, and the look that started there, in words. */
	about: Schema.String,
});
export type StartSpot = typeof StartSpot.Type;

/** The ages a spot serves: `either` for a scene both ages stand in. */
export const SpotAge = Schema.Literals(["child", "adult", "either"]);
export type SpotAge = typeof SpotAge.Type;

/** A chest and the spots it has, one for each age that stands in its own scene; none when it has no place. */
export const ChestSpots = Schema.Struct({
	name: Schema.String,
	/** The folder of the chest's scene, such as `KokiriForest`; empty for a chest the remake does not have. */
	area: Schema.String,
	spots: Schema.Array(
		Schema.Struct({
			age: SpotAge,
			kind: SpotKind,
			about: Schema.String,
		})
	),
});
export type ChestSpots = typeof ChestSpots.Type;

/** Where Link stands in a running game, sent to be kept as a spot found in play. */
export const PlayedSpot = Schema.Struct({
	name: SpotName,
	scene: Schema.String,
	/** The game's `DungeonRoom`, which is a room only inside a dungeon. */
	room: Schema.Int,
	position: Position,
	facing: Schema.Finite,
	/** The actor of the chest Link has just opened, by its hash, so the spot stands near that chest and faces it. */
	chest: Schema.optionalKey(Schema.String),
	/** A name that is no check and no spot yet, kept on purpose. */
	new: Schema.optionalKey(Schema.Boolean),
});
export type PlayedSpot = typeof PlayedSpot.Type;
