/**
 * What a script file is. A kept script under `scripts/` default-exports `script({...})`. The state is declared, not
 * built in the body, so it is planned and refused before any game starts, and the record names it. A script outside
 * the package imports `script` and `Effect` from this file by its path, which needs no packages beside the script.
 */
import { Effect, Predicate, Schema } from "effect";
import { ModFolder, StartState, TextSpeed } from "./contract/state.ts";
import { Limit } from "./contract/values.ts";
import type {
	CommandError,
	NobodyThere,
	ProvesNothing,
	ValueRefused,
	WaitTimedOut,
	WriteUnsupported,
} from "./errors.ts";
import { ScriptFailed } from "./errors.ts";
import type { RunningGame } from "./game.ts";

/** What a script's body can fail with. A wait that times out marks the run `failed`; any other failure `broken`. */
export type ScriptError =
	| CommandError
	| ValueRefused
	| ProvesNothing
	| WaitTimedOut
	| NobodyThere
	| WriteUnsupported;

export interface Script<A> {
	/** One line on what it shows. */
	readonly about: string;
	/** The most the body may take once Link can move, such as `10 minutes`; 5 minutes unless given. */
	readonly limit?: string;
	/**
	 * The mod the place gets alone: a folder the patcher wrote, as an absolute path or one from the script's own
	 * folder, or `none` for the game as it ships. Absent, the machine's own mod that skips what a player can skip.
	 */
	readonly mod?: typeof ModFolder.Encoded | "none";
	/** `here` when it hands over to a person, which `run` skips. */
	readonly person?: "here" | "nobody";
	/** Drives the started game. What it returns is the run's result. */
	readonly run: (game: RunningGame) => Effect.Effect<A, ScriptError>;
	readonly state: typeof StartState.Encoded;
	/** `game` writes the game's text at its own speed; `instant`, as a player has it, unless given. */
	readonly textSpeed?: TextSpeed;
}

/** Declares a script, so its body's types are checked against `RunningGame`. */
export const script = <A>(definition: Script<A>) => definition;

/** What a script declares beside its body, which its child says before its game starts. */
export const Declaration = Schema.Struct({
	about: Schema.String,
	limit: Schema.optionalKey(Limit),
	mod: Schema.optionalKey(Schema.Union([ModFolder, Schema.Literal("none")])),
	person: Schema.optionalKey(Schema.Literals(["here", "nobody"])),
	state: StartState,
	textSpeed: Schema.optionalKey(TextSpeed),
});

/** A script file's default export, as its child reads it. */
const Declared = Schema.Struct({
	...Declaration.fields,
	run: Schema.declare((input: unknown): input is Script<unknown>["run"] =>
		Predicate.isFunction(input)
	),
});
export type Declared = typeof Declared.Type;

/** Imports a script file and reads its declaration. */
export const load = Effect.fn("Script.load")(function* (path: string) {
	const module: unknown = yield* Effect.tryPromise({
		try: () => import(path),
		catch: (cause) =>
			ScriptFailed.make({ reason: "load", detail: String(cause) }),
	});
	const exported = Predicate.hasProperty(module, "default")
		? module.default
		: undefined;
	return yield* Schema.decodeUnknownEffect(Declared)(exported).pipe(
		Effect.mapError((error) =>
			ScriptFailed.make({
				reason: "load",
				detail: `${path} does not default-export a script: ${error.message}`,
			})
		)
	);
});

export { Effect } from "effect";
