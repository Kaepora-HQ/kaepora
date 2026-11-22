/**
 * What a script file is. A kept script under `scripts/` default-exports `script({...})`. The state is declared, not
 * built in the body, so it is planned and refused before any game starts, and the record names it.
 */
import { Effect, Predicate, Schema } from "effect";
import { ModSource, StartState } from "./contract/state.ts";
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
	readonly mod?: typeof ModSource.Encoded;
	/** `here` when it hands over to a person, which `run` skips. */
	readonly person?: "here" | "nobody";
	/** Drives the started game. What it returns is the run's result. */
	readonly run: (game: RunningGame) => Effect.Effect<A, ScriptError>;
	readonly state: typeof StartState.Encoded;
}

/** Declares a script, so its body's types are checked against `RunningGame`. */
export const script = <A>(definition: Script<A>) => definition;

/** What a script declares beside its body, which its child says before its game starts. */
export const Declaration = Schema.Struct({
	about: Schema.String,
	limit: Schema.optionalKey(Limit),
	mod: Schema.optionalKey(ModSource),
	person: Schema.optionalKey(Schema.Literals(["here", "nobody"])),
	state: StartState,
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
