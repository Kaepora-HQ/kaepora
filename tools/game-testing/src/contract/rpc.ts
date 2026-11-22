/**
 * A game's lifetime is the `Open` stream. The caller keeps it open while the game runs; the server stops the game and
 * gives its place back when the stream ends, which happens when the caller closes it, its scope ends, or its
 * connection drops (the server interrupts a closed connection's requests). The other commands name the session the
 * stream announced.
 */
import { Schema } from "effect";
import { Rpc, RpcGroup } from "effect/rpc";
import {
	DumpFailed,
	EmulatorRefused,
	EmulatorUnavailable,
	ImageBuildFailed,
	LaunchFailed,
	NobodyThere,
	NoFreePlace,
	NoGameHere,
	NoGraphicsCard,
	RunNotFound,
	SaveUnreadable,
	SessionNotFound,
	StateRefused,
	UnknownName,
	ValueNotFound,
	ValueRefused,
	VersionMismatch,
	WaitTimedOut,
	WriteUnsupported,
} from "../errors.ts";
import { Outcome, RunRecord, RunSummary } from "./record.ts";
import { ModSource, ReadBack, StartState } from "./state.ts";
import {
	Buttons,
	Frames,
	Limit,
	Picture,
	PlaceId,
	RunId,
	Saved,
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
]);

const StartErrors = Schema.Union([
	UnknownName,
	StateRefused,
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

/**
 * The contract's version. A change to any command, schema or error raises it, and `Hello` refuses a caller built from
 * another one, since the CI runner and the game machine can run different commits.
 */
export const contract = 3;

/** Who answers a hand over in this session: a person at the machine, or nobody (CI refuses every hand over). */
export const Person = Schema.Literals(["here", "nobody"]);

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

/**
 * One record a state writes, with what the base held and the word it came from: a saved value by name, in
 * `progress.sav` or `caption.sav`, or a per-actor key by its placement hash in decimal.
 */
export const PlannedEdit = Schema.Union([
	Schema.TaggedStruct("value", {
		name: Schema.String,
		file: Schema.Literals(["progress", "caption"]),
		to: Saved,
		base: Saved,
		from: Schema.String,
	}),
	Schema.TaggedStruct("key", {
		key: Schema.String,
		to: Schema.Boolean,
		base: Schema.Boolean,
		from: Schema.String,
	}),
]);
export type PlannedEdit = typeof PlannedEdit.Type;

/**
 * What a state writes, and the values a fresh place's files name that are not the place's own, which it leaves as the
 * base holds them, so a test that needs one adds it with `set`.
 */
export const StatePlan = Schema.Struct({
	edits: Schema.Array(PlannedEdit),
	leftAlone: Schema.Array(
		Schema.Struct({ name: Schema.String, from: Schema.String })
	),
});
export type StatePlan = typeof StatePlan.Type;

/** A name found by `Search`, with what kind of thing it names and a line on what it is. */
export const NameMatch = Schema.Struct({
	kind: Schema.String,
	name: Schema.String,
	about: Schema.String,
});

export class GameRpcs extends RpcGroup.make(
	/** The first call on every connection. */
	Rpc.make("Hello", {
		payload: { contract: Schema.Int },
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
			state: StartState,
			mod: Schema.optionalKey(ModSource),
			person: Person,
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
	Rpc.make("Check", {
		payload: { session: SessionId, value: Schema.String, expected: Value },
		success: Schema.Struct({ actual: Value, held: Schema.Boolean }),
		error: Schema.Union([CommandErrors, ValueRefused]),
	}),
	Rpc.make("Screenshot", {
		payload: { session: SessionId, label: Schema.optionalKey(Schema.String) },
		success: Picture,
		error: CommandErrors,
	}),
	Rpc.make("HandOver", {
		payload: {
			session: SessionId,
			/** What the person is to do, in a sentence or two. */
			say: Schema.String,
			/** A question to answer when done; absent when the person only presses on. */
			ask: Schema.optionalKey(Schema.String),
			within: Limit,
		},
		success: Schema.Struct({ answer: Schema.optionalKey(Schema.String) }),
		error: Schema.Union([CommandErrors, NobodyThere]),
	}),
	Rpc.make("Note", {
		payload: { session: SessionId, text: Schema.String },
		error: SessionNotFound,
	}),
	/** The script's end, sent before its `Open` stream closes, so the host can write the outcome. */
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
		payload: { state: StartState },
		success: StatePlan,
		error: Schema.Union([
			UnknownName,
			StateRefused,
			ValueRefused,
			NoGameHere,
			DumpFailed,
			SaveUnreadable,
		]),
	}),
	Rpc.make("Search", {
		payload: {
			text: Schema.String,
			kind: Schema.optionalKey(Schema.String),
			limit: Schema.Int,
		},
		success: Schema.Array(NameMatch),
		error: Schema.Union([NoGameHere, DumpFailed]),
	}),
	Rpc.make("Runs", {
		payload: { script: Schema.optionalKey(Schema.String), limit: Schema.Int },
		success: Schema.Array(RunSummary),
	}),
	Rpc.make("Run", {
		payload: { run: RunId },
		success: RunRecord,
		error: RunNotFound,
	}),
	Rpc.make("RunPicture", {
		payload: { run: RunId, path: Schema.String },
		success: Schema.Uint8Array,
		error: RunNotFound,
	})
) {}
