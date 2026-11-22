import { Array as Arr, Cause, Duration, Predicate, Schema } from "effect";
import { PlaceId, RunId, SessionId, Value } from "./contract/values.ts";

/** A name no table of the game holds. `suggestions` are the nearest names of the same kind. */
export class UnknownName extends Schema.TaggedError<
	UnknownName,
	{ readonly brand: unique symbol }
>("game-testing/errors/UnknownName")("UnknownName", {
	kind: Schema.Literals([
		"value",
		"item",
		"check",
		"landing",
		"spot",
		"scene",
		"room",
		"quest",
		"step",
		"event",
		"base",
		"subject",
	]),
	asked: Schema.String,
	suggestions: Schema.Array(Schema.String),
}) {
	override get message() {
		const near =
			this.suggestions.length === 0
				? ""
				: ` Nearest: ${Arr.join(this.suggestions, ", ")}.`;
		return `No ${this.kind} is named ${this.asked}.${near} Run game-testing search ${this.asked}.`;
	}
}

/**
 * The start state was refused before the emulator started. `contradiction`: two words disagree, such as has and lacks
 * one item, two words writing one record two ways, or an age that does not fit the scene Link stands in. `ambiguous`:
 * a name two tables hold. `shut`: Link would stand inside a grotto the base leaves shut. `mod-too-large`: a mod file is
 * larger than its size-table entry, which the game drops silently. `unsaved`: `set` names a value neither
 * `progress.sav` nor `caption.sav` keeps, which only a running game can change. `no-slot`: `equip` names an item that
 * no slot or button holds. `incomplete`: a word needs another to be honoured whole, which the detail names.
 */
export class StateRefused extends Schema.TaggedError<
	StateRefused,
	{ readonly brand: unique symbol }
>("game-testing/errors/StateRefused")("StateRefused", {
	reason: Schema.Literals([
		"contradiction",
		"ambiguous",
		"shut",
		"mod-too-large",
		"unsaved",
		"no-slot",
		"incomplete",
	]),
	detail: Schema.String,
}) {
	override get message() {
		return `The start state is refused (${this.reason}): ${this.detail}`;
	}
}

/** Every place on the machine stayed held for the whole wait. */
export class NoFreePlace extends Schema.TaggedError<
	NoFreePlace,
	{ readonly brand: unique symbol }
>("game-testing/errors/NoFreePlace")("NoFreePlace", {
	places: Schema.Int,
	waited: Schema.DurationFromString,
}) {
	override get message() {
		return `All ${this.places} places stayed held for ${Duration.format(this.waited)}. game-testing places shows who holds each.`;
	}
}

/**
 * Podman lists no device `nvidia.com/gpu=all`, so no place can reach this machine's NVIDIA card, and nothing was
 * started.
 */
export class NoGraphicsCard extends Schema.TaggedError<
	NoGraphicsCard,
	{ readonly brand: unique symbol }
>("game-testing/errors/NoGraphicsCard")("NoGraphicsCard", {}) {
	override readonly message =
		"Podman lists no device nvidia.com/gpu=all, so no place here can reach the NVIDIA card, and nothing was started. Install NVIDIA's Container Toolkit (the nvidia-container-toolkit package), or write its device list again with sudo nvidia-ctk cdi generate --output=/var/run/cdi/nvidia.yaml.";
}

/** The place image did not build, so nothing was started. The next take builds it again. */
export class ImageBuildFailed extends Schema.TaggedError<
	ImageBuildFailed,
	{ readonly brand: unique symbol }
>("game-testing/errors/ImageBuildFailed")("ImageBuildFailed", {
	image: Schema.String,
	detail: Schema.String,
}) {
	override get message() {
		return `podman build of ${this.image} failed, and nothing was started: ${this.detail}`;
	}
}

/**
 * The emulator did not reach a ready signal. `stage` names the first signal it missed, in order: `spawn`, `debug-server`,
 * `first-frame`, `title`, `loaded`, `control`. `detail` says why: podman's error, the container's exit, the signal's
 * limit, or the debug server's error. `logTail` is the end of its place's log.
 */
export class LaunchFailed extends Schema.TaggedError<
	LaunchFailed,
	{ readonly brand: unique symbol }
>("game-testing/errors/LaunchFailed")("LaunchFailed", {
	place: PlaceId,
	stage: Schema.Literals([
		"spawn",
		"debug-server",
		"first-frame",
		"title",
		"loaded",
		"control",
	]),
	detail: Schema.String,
	logTail: Schema.Array(Schema.String),
}) {
	override get message() {
		return `The game in place ${this.place} stopped short of ${this.stage}: ${this.detail}`;
	}
}

/**
 * The debug server did not answer, so the connection is over and every later call fails the same way. `connect`: it
 * refused the connection. `closed`: it closed it. `deadline`: a call passed its deadline. `garbled`: a reply did not
 * match its request. `logTail` is the end of Lichii's log when the connection ended.
 */
export class EmulatorUnavailable extends Schema.TaggedError<
	EmulatorUnavailable,
	{ readonly brand: unique symbol }
>("game-testing/errors/EmulatorUnavailable")("EmulatorUnavailable", {
	/** The call left unanswered, or `connect` for the connection itself. */
	method: Schema.String,
	reason: Schema.Literals(["connect", "closed", "deadline", "garbled"]),
	logTail: Schema.Array(Schema.String),
}) {
	override get message() {
		const log =
			this.logTail.length === 0
				? ""
				: ` Lichii's log ends:\n${Arr.join(this.logTail, "\n")}`;
		return `Lichii's debug server did not answer ${this.method} (${this.reason}). The game may have stopped.${log}`;
	}
}

/** The debug server answered a call with an error. */
export class EmulatorRefused extends Schema.TaggedError<
	EmulatorRefused,
	{ readonly brand: unique symbol }
>("game-testing/errors/EmulatorRefused")("EmulatorRefused", {
	method: Schema.String,
	error: Schema.String,
}) {
	override get message() {
		return `Lichii refused ${this.method}: ${this.error}`;
	}
}

/** This Lichii build has no `write_memory`, which `set` needs. */
export class WriteUnsupported extends Schema.TaggedError<
	WriteUnsupported,
	{ readonly brand: unique symbol }
>("game-testing/errors/WriteUnsupported")("WriteUnsupported", {}) {
	override readonly message =
		"This Lichii cannot write memory. set needs a build of the fork's nx2 branch.";
}

/** A named value was not found in the running game's memory. */
export class ValueNotFound extends Schema.TaggedError<
	ValueNotFound,
	{ readonly brand: unique symbol }
>("game-testing/errors/ValueNotFound")("ValueNotFound", {
	value: Schema.String,
}) {
	override get message() {
		return `${this.value} is in the game data list but was not found in the running game's memory.`;
	}
}

/**
 * A value given for a saved value does not fit its type, so it can never be set, held or waited for: a number for a
 * Bool, an Enum's choice the list does not have, or text too long for a String64. `holds` says what it takes.
 */
export class ValueRefused extends Schema.TaggedError<
	ValueRefused,
	{ readonly brand: unique symbol }
>("game-testing/errors/ValueRefused")("ValueRefused", {
	value: Schema.String,
	given: Value,
	holds: Schema.String,
}) {
	override get message() {
		return `${this.value} holds ${this.holds}, not ${String(this.given)}.`;
	}
}

/** A saved value of a type the reader of the game's memory does not take yet: a String16, String32 or Vector3. */
export class ValueUnreadable extends Schema.TaggedError<
	ValueUnreadable,
	{ readonly brand: unique symbol }
>("game-testing/errors/ValueUnreadable")("ValueUnreadable", {
	value: Schema.String,
	type: Schema.String,
}) {
	override get message() {
		return `${this.value} is a ${this.type}, which the game's memory is not read for. Read a Bool, Int, UInt, Float, Enum or String64.`;
	}
}

/**
 * A check whose start already settles it, so it shows nothing about the script's steps. `check` asks for an effect:
 * the value held `expected` at the start too. `still` asks that nothing changed: the value did not hold `expected` at
 * the start, or no save holds it. `remedy` names the word of the start state that would change the start.
 */
export class ProvesNothing extends Schema.TaggedError<
	ProvesNothing,
	{ readonly brand: unique symbol }
>("game-testing/errors/ProvesNothing")("ProvesNothing", {
	form: Schema.Literals(["check", "still"]),
	value: Schema.String,
	expected: Value,
	/** Absent for a value no save holds. */
	start: Schema.optionalKey(Value),
	remedy: Schema.String,
}) {
	override get message() {
		if (this.start === undefined) {
			return `${this.value} has no known start, since no save holds it, so nothing shows it stayed ${String(this.expected)}. ${this.remedy}`;
		}
		return this.form === "check"
			? `${this.value} already held ${String(this.expected)} at the start, so the check proves nothing. ${this.remedy}`
			: `${this.value} held ${String(this.start)} at the start, not ${String(this.expected)}, so it cannot stay ${String(this.expected)}. ${this.remedy}`;
	}
}

/** Once Link could move, the game held values other than the start state wrote, so the save did not load as built. */
export class StartNotTaken extends Schema.TaggedError<
	StartNotTaken,
	{ readonly brand: unique symbol }
>("game-testing/errors/StartNotTaken")("StartNotTaken", {
	differences: Schema.Array(
		Schema.Struct({ value: Schema.String, written: Value, held: Value })
	),
}) {
	override get message() {
		return `The game does not hold the state as it was written: ${Arr.join(
			Arr.map(
				this.differences,
				({ value, written, held }) =>
					`${value} holds ${String(held)}, not ${String(written)}`
			),
			"; "
		)}.`;
	}
}

/** A wait's limit passed before the value matched. `last` is what it held at the end. */
export class WaitTimedOut extends Schema.TaggedError<
	WaitTimedOut,
	{ readonly brand: unique symbol }
>("game-testing/errors/WaitTimedOut")("WaitTimedOut", {
	value: Schema.String,
	expected: Value,
	last: Value,
	within: Schema.DurationFromString,
}) {
	override get message() {
		return `${this.value} still held ${String(this.last)} after ${Duration.format(this.within)}; the wait expected ${String(this.expected)}.`;
	}
}

/** No game with this session is running here: it was stopped, or its owner went away. */
export class SessionNotFound extends Schema.TaggedError<
	SessionNotFound,
	{ readonly brand: unique symbol }
>("game-testing/errors/SessionNotFound")("SessionNotFound", {
	session: SessionId,
}) {
	override get message() {
		return `No game with session ${this.session} runs on this machine. It was stopped or its caller went away.`;
	}
}

/** A hand over found nobody: the run has no person (CI), or nobody answered within its limit. */
export class NobodyThere extends Schema.TaggedError<
	NobodyThere,
	{ readonly brand: unique symbol }
>("game-testing/errors/NobodyThere")("NobodyThere", {
	ask: Schema.String,
	reason: Schema.Literals(["no-person", "timed-out"]),
}) {
	override get message() {
		return this.reason === "no-person"
			? `This run has nobody to hand over to, and it asked: ${this.ask} Run it with game-testing sit.`
			: `Nobody answered the hand over in time: ${this.ask}`;
	}
}

/** kaepora-dump failed. */
export class DumpFailed extends Schema.TaggedError<
	DumpFailed,
	{ readonly brand: unique symbol }
>("game-testing/errors/DumpFailed")("DumpFailed", {
	exitCode: Schema.Int,
	stderr: Schema.String,
}) {
	override get message() {
		return `kaepora-dump exited with ${this.exitCode}: ${this.stderr}`;
	}
}

/** This machine has no game file, so nothing here can start a game or read its tables. */
export class NoGameHere extends Schema.TaggedError<
	NoGameHere,
	{ readonly brand: unique symbol }
>("game-testing/errors/NoGameHere")("NoGameHere", {
	path: Schema.optionalKey(Schema.String),
}) {
	override get message() {
		return this.path === undefined
			? "GAME_TESTING_GAME_FILE is not set, so this machine cannot start a game. Set it, or set GAME_TESTING_URL to use a machine that has one."
			: `No game file at ${this.path}.`;
	}
}

/** A save file did not decode: its header or a record group is not the layout the codec reads. */
export class SaveUnreadable extends Schema.TaggedError<
	SaveUnreadable,
	{ readonly brand: unique symbol }
>("game-testing/errors/SaveUnreadable")("SaveUnreadable", {
	path: Schema.String,
	detail: Schema.String,
}) {
	override get message() {
		return `The save ${this.path} is unreadable: ${this.detail}`;
	}
}

/**
 * A script did not finish. `typecheck`: it failed the type check. `load`: it did not import or has no default script.
 * `limit`: it ran past its limit. `exited`: its process ended before it finished.
 */
export class ScriptFailed extends Schema.TaggedError<
	ScriptFailed,
	{ readonly brand: unique symbol }
>("game-testing/errors/ScriptFailed")("ScriptFailed", {
	reason: Schema.Literals(["typecheck", "load", "limit", "exited"]),
	detail: Schema.String,
	run: Schema.optionalKey(RunId),
}) {
	override get message() {
		return `The script failed (${this.reason}): ${this.detail}`;
	}
}

/** No run record by this id is kept here. */
export class RunNotFound extends Schema.TaggedError<
	RunNotFound,
	{ readonly brand: unique symbol }
>("game-testing/errors/RunNotFound")("RunNotFound", { run: RunId }) {
	override get message() {
		return `No run ${this.run} is kept on this machine.`;
	}
}

/** `serve` refused to start: it needs a token, so it never listens unguarded. */
export class TokenRequired extends Schema.TaggedError<
	TokenRequired,
	{ readonly brand: unique symbol }
>("game-testing/errors/TokenRequired")("TokenRequired", {
	key: Schema.String,
}) {
	override get message() {
		return `${this.key} is not set, so serve does not start. Set it to a long random string.`;
	}
}

/** The far machine could not be reached, or it refused the token. */
export class RemoteUnavailable extends Schema.TaggedError<
	RemoteUnavailable,
	{ readonly brand: unique symbol }
>("game-testing/errors/RemoteUnavailable")("RemoteUnavailable", {
	url: Schema.String,
	reason: Schema.Literals(["connect", "token", "closed"]),
}) {
	override get message() {
		return `The game machine at ${this.url} is unavailable (${this.reason}).`;
	}
}

/** The two ends were built from different contracts: the far machine's package differs from this one's. */
export class VersionMismatch extends Schema.TaggedError<
	VersionMismatch,
	{ readonly brand: unique symbol }
>("game-testing/errors/VersionMismatch")("VersionMismatch", {
	here: Schema.Int,
	there: Schema.Int,
	machine: Schema.String,
}) {
	override get message() {
		return `${this.machine} speaks contract ${this.there} and this copy speaks ${this.here}. Update the older one to the same Kaepora commit.`;
	}
}

/** What a command on a running game can fail with, on any transport. */
export type CommandError =
	| EmulatorUnavailable
	| EmulatorRefused
	| SessionNotFound
	| UnknownName
	| ValueNotFound
	| ValueUnreadable
	| RemoteUnavailable;

/** What starting a game can fail with, on any transport. */
export type StartError =
	| UnknownName
	| StateRefused
	| StartNotTaken
	| NoFreePlace
	| NoGraphicsCard
	| ImageBuildFailed
	| LaunchFailed
	| EmulatorUnavailable
	| SaveUnreadable
	| NoGameHere
	| DumpFailed
	| ValueRefused
	| RemoteUnavailable;

/** A failure as a record keeps it: the error's tag and message, or `Defect` and its text for anything else. */
export const failureOf = (cause: Cause.Cause<unknown>) => {
	const error = Cause.squash(cause);
	return Predicate.hasProperty(error, "_tag") && error instanceof Error
		? { tag: String(error._tag), message: error.message }
		: { tag: "Defect", message: String(error) };
};
