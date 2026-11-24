/**
 * A game's lifetime is the `Open` stream. The caller keeps it open while the game runs; the server stops the game and
 * gives its place back when the stream ends, which happens when the caller closes it, its scope ends, or its
 * connection drops (the server interrupts a closed connection's requests). The other commands name the session the
 * stream announced.
 */
import { Array as Arr, Formatter, Schema, SchemaRepresentation } from "effect";
import { Rpc, RpcGroup } from "effect/rpc";
import {
	DumpFailed,
	EmulatorRefused,
	EmulatorUnavailable,
	ImageBuildFailed,
	LaunchFailed,
	ModUnreadable,
	NameTaken,
	NobodyThere,
	NoFreePlace,
	NoGameHere,
	NoGraphicsCard,
	ProvesNothing,
	RunNotFound,
	RunUnreadable,
	SaveUnreadable,
	SessionNotFound,
	SpotRefused,
	StartNotTaken,
	StateRefused,
	UnknownName,
	ValueNotFound,
	ValueRefused,
	ValueUnreadable,
	VersionMismatch,
	WaitTimedOut,
	WriteUnsupported,
} from "../errors.ts";
import { Base, Catalog, NewBase } from "./bases.ts";
import { StatePlan } from "./plan.ts";
import { Outcome, RunList, RunRecord, ScriptSource } from "./record.ts";
import { ChestSpots, Mark, PlayedSpot, Recordings, Spot } from "./spots.ts";
import { Mod, ReadBack, StartState, TextSpeed } from "./state.ts";
import {
	Button,
	Buttons,
	Frames,
	Limit,
	NameKind,
	Picture,
	PlaceId,
	RunId,
	SessionId,
	StickPosition,
	Value,
} from "./values.ts";

const CommandErrors = Schema.Union([
	EmulatorUnavailable,
	EmulatorRefused,
	SessionNotFound,
	UnknownName,
	ValueNotFound,
	ValueUnreadable,
]);

const StartErrors = Schema.Union([
	ModUnreadable,
	UnknownName,
	StateRefused,
	StartNotTaken,
	NoFreePlace,
	NoGraphicsCard,
	ImageBuildFailed,
	LaunchFailed,
	EmulatorUnavailable,
	SaveUnreadable,
	NoGameHere,
	DumpFailed,
	ValueRefused,
]);

/** How many presses a command that presses until something holds makes at most. */
export const Presses = Schema.Int.check(
	Schema.isBetween({ minimum: 1, maximum: 200 })
);
/** What the `Open` stream says, in order: `started` once, then a `beat` every few seconds until it ends. */
export const SessionEvent = Schema.Union([
	Schema.TaggedStruct("started", {
		session: SessionId,
		run: RunId,
		place: PlaceId,
		readBack: ReadBack,
	}),
	/** Keeps a quiet connection alive through a tunnel, and makes a vanished caller's socket fail to write. */
	Schema.TaggedStruct("beat", {}),
]);
export type SessionEvent = typeof SessionEvent.Type;

/** A place and who holds it. */
export const PlaceStatus = Schema.Struct({
	place: PlaceId,
	holder: Schema.optionalKey(
		Schema.Struct({
			session: SessionId,
			run: RunId,
			owner: Schema.String,
			since: Schema.String,
		})
	),
});

/** A name found by `Search`, with what kind of thing it names and a line on what it is. */
export const NameMatch = Schema.Struct({
	kind: Schema.String,
	name: Schema.String,
	about: Schema.String,
});
export type NameMatch = typeof NameMatch.Type;

export class GameRpcs extends RpcGroup.make(
	/** The first call on every connection. */
	Rpc.make("Hello", {
		payload: { contract: Schema.String },
		success: Schema.Struct({
			machine: Schema.String,
			places: Schema.Int,
			gameData: Schema.Boolean,
		}),
		error: VersionMismatch,
	}),
	Rpc.make("Places", { success: Schema.Array(PlaceStatus) }),
	Rpc.make("Open", {
		payload: {
			/** The caller names the run, so it knows the record however the start ends. A name already kept is refused. */
			run: RunId,
			state: StartState,
			/** The mod the place gets alone, `none` for none; absent, the machine's own mod that skips. */
			mod: Schema.optionalKey(Schema.Union([Mod, Schema.Literal("none")])),
			/** `instant` unless given. */
			textSpeed: Schema.optionalKey(TextSpeed),
			script: Schema.optionalKey(ScriptSource),
			/** The broken run this one repeats. */
			attemptOf: Schema.optionalKey(RunId),
			/** Who asks, for the places list: a script's path, `mcp`, or a CI job. */
			owner: Schema.String,
			waitForPlace: Limit,
		},
		success: SessionEvent,
		error: StartErrors,
		stream: true,
	}),
	Rpc.make("Press", {
		payload: {
			session: SessionId,
			buttons: Buttons,
			frames: Frames,
		},
		error: CommandErrors,
	}),
	/**
	 * Reads the value, then presses and lets `every` frames pass until it holds `expected`, `times` presses at most. It
	 * answers the presses it took, none for a value that already holds, and fails with `WaitTimedOut` when they run out.
	 */
	Rpc.make("PressUntil", {
		payload: {
			session: SessionId,
			buttons: Buttons,
			value: Schema.String,
			expected: Value,
			times: Presses,
			every: Frames,
		},
		success: Schema.Int,
		error: Schema.Union([CommandErrors, WaitTimedOut, ValueRefused]),
	}),
	/**
	 * Takes a picture, presses `button` and lets `every` frames pass, until a dialog it saw open is over and Link can move,
	 * `times` presses at most. Each press is a step with its picture. It answers the presses and the pictures in order,
	 * and fails with `WaitTimedOut` when no dialog opens or the presses run out.
	 */
	Rpc.make("CompleteDialog", {
		payload: {
			session: SessionId,
			button: Button,
			times: Presses,
			every: Frames,
		},
		/** The pictures by name, in the run's folder; a far caller's copy brings them. */
		success: Schema.Struct({
			presses: Schema.Int,
			pictures: Schema.Array(Schema.String),
		}),
		error: Schema.Union([CommandErrors, WaitTimedOut]),
	}),
	Rpc.make("Stick", {
		payload: { session: SessionId, position: StickPosition, frames: Frames },
		error: CommandErrors,
	}),
	Rpc.make("Idle", {
		payload: { session: SessionId, frames: Frames },
		error: CommandErrors,
	}),
	Rpc.make("Wait", {
		payload: {
			session: SessionId,
			value: Schema.String,
			expected: Value,
			within: Limit,
		},
		success: Value,
		error: Schema.Union([CommandErrors, WaitTimedOut, ValueRefused]),
	}),
	Rpc.make("Read", {
		payload: {
			session: SessionId,
			values: Schema.NonEmptyArray(Schema.String),
		},
		success: Schema.Record(Schema.String, Value),
		error: CommandErrors,
	}),
	Rpc.make("Set", {
		payload: {
			session: SessionId,
			values: Schema.Record(Schema.String, Value),
		},
		error: Schema.Union([CommandErrors, WriteUnsupported, ValueRefused]),
	}),
	/**
	 * Whether the value holds `expected`, judged against the start. `check` proves an effect: it holds when the value
	 * holds `expected` now and did not at the start. `still` proves nothing changed: it holds when the value holds
	 * `expected` now and did at the start. A start that settles the answer either way fails with `ProvesNothing`. A
	 * value no save holds has no known start: `check` then holds when it holds `expected` now, and `still` is refused
	 * with `ProvesNothing`.
	 */
	Rpc.make("Check", {
		payload: {
			session: SessionId,
			form: Schema.Literals(["check", "still"]),
			value: Schema.String,
			expected: Value,
		},
		success: Schema.Struct({ actual: Value, held: Schema.Boolean }),
		error: Schema.Union([CommandErrors, ValueRefused, ProvesNothing]),
	}),
	Rpc.make("Screenshot", {
		payload: { session: SessionId, label: Schema.optionalKey(Schema.String) },
		success: Picture,
		error: CommandErrors,
	}),
	Rpc.make("HandOver", {
		payload: {
			session: SessionId,
			/** What the person is asked, in a sentence or two. */
			say: Schema.String,
			within: Limit,
		},
		error: Schema.Union([CommandErrors, NobodyThere]),
	}),
	Rpc.make("Note", {
		payload: { session: SessionId, text: Schema.String },
		error: SessionNotFound,
	}),
	/**
	 * Ends a game started by hand: its run ends `stopped` for `reason`, unless an end came first, and its `Open` stream
	 * closes. As at every end, the game stops, its last saves are kept, its place is given back, and the record takes the
	 * outcome last.
	 */
	Rpc.make("Stop", {
		payload: { session: SessionId, reason: Schema.String },
		error: SessionNotFound,
	}),
	/** The script's end, sent before its `Open` stream closes, so the host has the outcome to write. */
	Rpc.make("Finish", {
		payload: {
			session: SessionId,
			/** The script's result, encoded, with pictures by path. */
			result: Schema.optionalKey(Schema.Unknown),
			/** The failure the body ended on, as its tag and message. */
			failure: Schema.optionalKey(
				Schema.Struct({ tag: Schema.String, message: Schema.String })
			),
		},
		success: Outcome,
		error: SessionNotFound,
	}),
	/** What a state would write, word by word, without starting a game. */
	Rpc.make("Plan", {
		payload: {
			state: StartState,
			/** The mod the place would get, as `Open` takes it, whose new game's start values the plan takes. */
			mod: Schema.optionalKey(Schema.Union([Mod, Schema.Literal("none")])),
		},
		success: StatePlan,
		error: Schema.Union([
			ModUnreadable,
			UnknownName,
			StateRefused,
			ValueRefused,
			NoGameHere,
			DumpFailed,
			SaveUnreadable,
		]),
	}),
	/** `generated/names.d.ts` as this machine writes it from the game's tables, for a caller's type check. */
	Rpc.make("Names", {
		success: Schema.String,
		error: Schema.Union([NoGameHere, DumpFailed]),
	}),
	Rpc.make("Search", {
		payload: {
			text: Schema.String,
			kind: Schema.optionalKey(NameKind),
			limit: Schema.Int,
		},
		success: Schema.Array(NameMatch),
		error: Schema.Union([NoGameHere, DumpFailed]),
	}),
	/** Every chest of `remake/chests.tsv` with the spot each age starts on, from this machine's tables. */
	Rpc.make("Spots", {
		success: Schema.Array(ChestSpots),
		error: Schema.Union([NoGameHere, DumpFailed]),
	}),
	/** Keeps where Link stands in a running game, in this machine's recorded spots, and answers the spot kept. */
	Rpc.make("RecordSpot", {
		payload: { spot: PlayedSpot },
		success: Spot,
		error: Schema.Union([SpotRefused, NoGameHere, DumpFailed]),
	}),
	/** This machine's recorded spots and marks, which git does not have yet. */
	Rpc.make("Recorded", { success: Recordings }),
	/** Takes recorded spots and marks out once they are in git; one recorded or marked again since stays. */
	Rpc.make("Forget", { payload: { recordings: Recordings } }),
	/**
	 * Marks the spot a run of this machine started on with the run's outcome, passed or failed, and answers the mark. A
	 * spot that moved since the run is not marked.
	 */
	Rpc.make("MarkSpot", {
		payload: { run: RunId },
		success: Mark,
		error: Schema.Union([
			SpotRefused,
			RunNotFound,
			RunUnreadable,
			NoGameHere,
			DumpFailed,
		]),
	}),
	/** Every base, with what an agent choosing a start reads. */
	Rpc.make("Bases", { success: Catalog }),
	/** Keeps a save the game wrote as a base on this machine, which a state's base names at once. */
	Rpc.make("NameBase", {
		payload: NewBase,
		success: Base,
		error: Schema.Union([NameTaken, StateRefused, SaveUnreadable]),
	}),
	Rpc.make("Runs", {
		payload: { script: Schema.optionalKey(Schema.String), limit: Schema.Int },
		success: RunList,
	}),
	Rpc.make("Run", {
		payload: { run: RunId },
		success: RunRecord,
		error: Schema.Union([RunNotFound, RunUnreadable]),
	}),
	/** A picture or a kept save's file of a run, by its name in the run's folder. */
	Rpc.make("RunFile", {
		payload: { run: RunId, path: Schema.String },
		success: Schema.Uint8Array,
		error: RunNotFound,
	})
) {}

/**
 * A contract's digest: SHA-256 over each command's name and the JSON Schema of what it sends, answers and fails with,
 * as the wire carries them. Any change to a command, schema or error changes it, with nothing to raise by hand.
 */
export const contractOf = <R extends Rpc.AnyWithProps>(
	group: RpcGroup.RpcGroup<R>
) => {
	const rpcs = [...group.requests.values()];
	const schemas = Arr.flatMap(rpcs, (rpc) => [
		rpc.payloadSchema.ast,
		rpc.successSchema.ast,
		rpc.errorSchema.ast,
	]);
	const wire = Arr.isReadonlyArrayNonEmpty(schemas)
		? SchemaRepresentation.toJsonSchemaMultiDocument(
				SchemaRepresentation.toRepresentations(schemas)
			)
		: {};
	return new Bun.CryptoHasher("sha256")
		.update(
			Formatter.formatJson({ commands: Arr.map(rpcs, (rpc) => rpc._tag), wire })
		)
		.digest("hex");
};

/**
 * The contract this build speaks. `Hello` refuses a caller that speaks another, since the CI runner and the game
 * machine can run different commits.
 */
export const contract = contractOf(GameRpcs);
