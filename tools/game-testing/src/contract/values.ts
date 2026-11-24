import {
	Array as Arr,
	Clock,
	Effect,
	Random,
	Schema,
	String as Str,
} from "effect";

/** A saved value as the game data list types it: Bool, Int, UInt, Float, Enum (its choice's name) or String64. */
export const Value = Schema.Union([
	Schema.Boolean,
	Schema.Finite,
	Schema.String,
]);
export type Value = typeof Value.Type;

/** The kinds of name the game's tables give, which the cache of the game's tables keeps. */
export const TableKind = Schema.Literals([
	"value",
	"item",
	"quest",
	"scene",
	"landing",
]);
export type TableKind = typeof TableKind.Type;

/**
 * Every kind of name a state or a body takes: the tables' kinds, the event settings among the saved values, and the
 * checks, spots and bases of Kaepora's own tables.
 */
export const NameKind = Schema.Literals([
	...TableKind.literals,
	"event",
	"check",
	"spot",
	"base",
]);
export type NameKind = typeof NameKind.Type;

/** A place in a scene's units, x, y and z. */
export const Position = Schema.Tuple([
	Schema.Finite,
	Schema.Finite,
	Schema.Finite,
]);

/** What a save record holds: a value, or a position such as where Link stands. */
export const Saved = Schema.Union([Value, Position]);
export type Saved = typeof Saved.Type;

/** A place on this machine that can hold one game, numbered from 1. */
export const PlaceId = Schema.Int.check(Schema.isGreaterThanOrEqualTo(1)).pipe(
	Schema.brand("PlaceId")
);
export type PlaceId = typeof PlaceId.Type;

/** One started game, from its start to its stop. Random, so a far client cannot guess another's. */
export const SessionId = Schema.String.pipe(Schema.brand("SessionId"));
export type SessionId = typeof SessionId.Type;

/**
 * One run's record, named by its start time in UTC and a random suffix, such as `20261121-142233-k3f9`. The record's
 * folder takes the name, so nothing else passes.
 */
/** A save's number among a run's saves, `save-<n>-progress.sav`, from 1: save 0 is the one the tool built. */
export const SaveNumber = Schema.Int.check(Schema.isGreaterThanOrEqualTo(1));

export const RunId = Schema.String.check(
	Schema.isPattern(/^\d{8}-\d{6}-[0-9a-z]{4}$/u)
).pipe(Schema.brand("RunId"));
export type RunId = typeof RunId.Type;

const suffixLetters = "0123456789abcdefghijklmnopqrstuvwxyz";

export const newRunId = Effect.gen(function* () {
	const millis = yield* Clock.currentTimeMillis;
	const now =
		Temporal.Instant.fromEpochMilliseconds(millis).toZonedDateTimeISO("UTC");
	const two = (n: number) => Str.padStart(2, "0")(String(n));
	const suffix = yield* Effect.forEach(
		Arr.range(1, 4),
		() =>
			Effect.map(
				Random.nextIntBetween(0, suffixLetters.length, { halfOpen: true }),
				(at) => suffixLetters[at] ?? ""
			),
		{ concurrency: 1 }
	);
	return RunId.make(
		`${now.year}${two(now.month)}${two(now.day)}-${two(now.hour)}${two(now.minute)}${two(now.second)}-${Arr.join(suffix, "")}`
	);
});

/** A button as the debug server names it. */
export const Button = Schema.Literals([
	"A",
	"B",
	"X",
	"Y",
	"L",
	"R",
	"ZL",
	"ZR",
	"Plus",
	"Minus",
	"Up",
	"Down",
	"Left",
	"Right",
	"StickL",
	"StickR",
]);
export type Button = typeof Button.Type;

const isHotkey = (buttons: readonly Button[]) =>
	Arr.contains(buttons, "Plus") &&
	Arr.contains(buttons, "Minus") &&
	(Arr.contains(buttons, "L") || Arr.contains(buttons, "R"));

/**
 * Buttons held together. Refused when they make Lichii's stop or restart hotkey, and branded so that only checked
 * buttons reach the emulator: build them with `Buttons.make`.
 */
export const Buttons = Schema.NonEmptyArray(Button).pipe(
	Schema.check(
		Schema.makeFilter((buttons) =>
			isHotkey(buttons)
				? "L or R with Plus and Minus stops or restarts Lichii, so it is never pressed."
				: undefined
		)
	),
	Schema.brand("Buttons")
);
export type Buttons = typeof Buttons.Type;

/** A stick position, each axis from -1 to 1. */
export const StickPosition = Schema.Struct({
	stick: Schema.Literals(["Left", "Right"]),
	x: Schema.Finite.check(Schema.isBetween({ minimum: -1, maximum: 1 })),
	y: Schema.Finite.check(Schema.isBetween({ minimum: -1, maximum: 1 })),
});
export type StickPosition = typeof StickPosition.Type;

/**
 * A count of the game's frames, the unit of every input. At most 13,350: the server holds one input for 900 seconds
 * at most, and at 15 frames a second, the slowest a hidden Hyprland workspace draws, 13,350 frames take 890 seconds.
 */
export const Frames = Schema.Int.check(
	Schema.isBetween({ minimum: 1, maximum: 13_350 })
);

/** A time limit as text a person writes, such as `20 seconds`. */
export const Limit = Schema.DurationFromString;

/** A picture of the game: where it is kept on the game's machine, and its PNG bytes. */
export class Picture extends Schema.Class<
	Picture,
	{ readonly brand: unique symbol }
>("game-testing/contract/values/Picture")({
	/** The file in the run's folder on the machine with the game. */
	path: Schema.String,
	/** What the script called it, such as `after-chest`. */
	label: Schema.optionalKey(Schema.String),
	png: Schema.Uint8Array,
}) {}
