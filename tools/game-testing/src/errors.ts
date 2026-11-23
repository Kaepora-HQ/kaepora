import {
	Array as Arr,
	Cause,
	Duration,
	Predicate,
	Schema,
	String as Str,
} from "effect";
import {
	NameKind,
	PlaceId,
	RunId,
	Saved,
	SessionId,
	Value,
} from "./contract/values.ts";

const searchable: ReadonlySet<string> = new Set(NameKind.literals);

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
		const search = searchable.has(this.kind)
			? ` search_names, or game-testing search, finds ${this.kind} names.`
			: "";
		return `No ${this.kind} is named ${this.asked}.${near}${search}`;
	}
}

/**
 * The start state was refused before the emulator started. `contradiction`: two words disagree, such as `withItems`
 * and `withoutItems` naming one item, two words writing one record two ways, or an age that does not fit the scene Link
 * stands in. `ambiguous`: a name two tables hold. `shut`: Link would stand inside a grotto the base leaves shut.
 * `mod-too-large`: a mod file is larger than its size-table entry, which the game drops silently. `unsaved`: `set`
 * names a value neither `progress.sav` nor `caption.sav` keeps, which only a running game can change. `no-slot`:
 * `equipped` names an item that no slot or button holds. `incomplete`: a word needs another to be honoured whole, which
 * the detail names. `no-place`: a location names a check with no actor of its own and no spot.
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
		"no-place",
	]),
	detail: Schema.String,
}) {
	override get message() {
		return `The start state is refused (${this.reason}): ${this.detail}`;
	}
}

/**
 * A mod could not be read from its folder, so nothing was started. `missing`: there is no folder there. `empty`: it
 * holds no file. `unnamed`: the path names no folder of its own, such as one ending in `..`. `no-skips`: the game's
 * machine keeps no mod that skips, which a start that names no mod gets.
 */
export class ModUnreadable extends Schema.TaggedError<
	ModUnreadable,
	{ readonly brand: unique symbol }
>("game-testing/errors/ModUnreadable")("ModUnreadable", {
	/** The mod's folder; for `list`, the mod's name and the list's path inside it. */
	folder: Schema.String,
	reason: Schema.Literals(["missing", "empty", "unnamed", "no-skips", "list"]),
	/** For `list`, why it does not read. */
	detail: Schema.optionalKey(Schema.String),
}) {
	override get message() {
		switch (this.reason) {
			case "list":
				return `The mod's game data list ${this.folder} does not read as the game's: ${this.detail ?? ""}. Nothing was started. Build the mod with the current patcher.`;
			case "missing":
				return `There is no mod folder at ${this.folder}, so nothing was started. Give the folder the patcher wrote, as an absolute path or one from the script's own folder.`;
			case "empty":
				return `The mod folder ${this.folder} holds no file, so nothing was started.`;
			case "unnamed":
				return `The mod folder ${this.folder} names no folder of its own, so nothing was started. Give the path of the folder the patcher wrote.`;
			default:
				return `The game's machine keeps no mod that skips at ${this.folder}, so nothing was started. Build it there with the machine's setup, or name mod: "none" for the game with no mod.`;
		}
	}
}

/** A mod is larger than a start can carry to the game's machine, so nothing was sent. */
export class ModTooLarge extends Schema.TaggedError<
	ModTooLarge,
	{ readonly brand: unique symbol }
>("game-testing/errors/ModTooLarge")("ModTooLarge", {
	folder: Schema.String,
	/** The mod's files as the start sends them, and the most a start carries. */
	bytes: Schema.Int,
	limit: Schema.Int,
}) {
	override get message() {
		const mebibyte = 1024 * 1024;
		return `The mod at ${this.folder} is ${(this.bytes / mebibyte).toFixed(1)} MiB as the start sends it, over the ${Math.floor(this.limit / mebibyte)} MiB a start can carry, so nothing was sent. A seed's mod is 6 to 9 MB: leave out what the look does not need.`;
	}
}

/** A far run's record, pictures and saves were not copied to this machine. The game's machine keeps them. */
export class CopyFailed extends Schema.TaggedError<
	CopyFailed,
	{ readonly brand: unique symbol }
>("game-testing/errors/CopyFailed")("CopyFailed", {
	run: RunId,
	reason: Schema.String,
}) {
	override get message() {
		return `The record, pictures and saves were not copied here: ${this.reason}. The game machine keeps them as run ${this.run}, which game-testing runs lists there.`;
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
		Schema.Struct({ value: Schema.String, written: Saved, held: Saved })
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
	/** The presses the wait made, for one that pressed between reads. */
	presses: Schema.optionalKey(Schema.Int),
	/** For a dialog: none opened after the first press, or one was still open when the presses ran out. */
	dialog: Schema.optionalKey(Schema.Literals(["none-opened", "still-open"])),
}) {
	override get message() {
		const within = Duration.format(this.within);
		switch (this.dialog) {
			case "none-opened":
				return `No dialog opened in the ${within} after the first press: nothing before Link speaks. Stand him before a person, a sign or a chest.`;
			case "still-open":
				return `The dialog was still open after ${this.presses} presses, ${within}. Give more times, up to 200, or more every frames at the game's own text speed.`;
			default:
				return `${this.value} still held ${String(this.last)} after ${this.presses === undefined ? within : `${this.presses} presses, ${within}`}; the wait expected ${String(this.expected)}.`;
		}
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
		return `No game with session ${this.session} runs on the game machine: it was stopped, its caller went away, or no call named it for 10 minutes. start_game starts another.`;
	}
}

/** The game with this session runs, but another caller started it, and the by-hand tools take only their server's own. */
export class SessionNotStartedHere extends Schema.TaggedError<
	SessionNotStartedHere,
	{ readonly brand: unique symbol }
>("game-testing/errors/SessionNotStartedHere")("SessionNotStartedHere", {
	session: SessionId,
}) {
	override get message() {
		return `The game with session ${this.session} runs on the game machine, but this server did not start it: the by-hand tools take only the games this server's start_game started. start_game starts one of your own.`;
	}
}

/**
 * A hand over found nobody. `no-person`: the run has nobody to ask, as in CI, or its MCP client cannot ask its user.
 * `declined`: the client or its user refused. `timed-out`: nobody answered within its limit.
 */
export class NobodyThere extends Schema.TaggedError<
	NobodyThere,
	{ readonly brand: unique symbol }
>("game-testing/errors/NobodyThere")("NobodyThere", {
	ask: Schema.String,
	reason: Schema.Literals(["no-person", "declined", "timed-out"]),
}) {
	override get message() {
		switch (this.reason) {
			case "no-person":
				return `This run has nobody to hand over to, and it asked: ${this.ask} Write the step as game calls instead.`;
			case "declined":
				return `The hand over was declined: ${this.ask} Write the step as game calls instead, or ask again.`;
			default:
				return `Nobody answered the hand over in time: ${this.ask}`;
		}
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
		const record =
			this.run === undefined
				? ""
				: `\nIts record is run ${this.run}; get_run, or game-testing runs, shows how far it got.`;
		return `${failedBy[this.reason]}:\n${this.detail}${record}`;
	}
}

const failedBy = {
	typecheck:
		"The script failed its type check, so no game started. Fix these lines and run it again",
	load: "The script did not load, so no game started",
	limit: "The script was stopped at a limit",
	exited: "The script did not finish",
} as const;

/** `keep` named a kept script that exists in the package this server runs from, so nothing ran and nothing was written. */
export class KeptScriptExists extends Schema.TaggedError<
	KeptScriptExists,
	{ readonly brand: unique symbol }
>("game-testing/errors/KeptScriptExists")("KeptScriptExists", {
	path: Schema.String,
}) {
	override get message() {
		return `${this.path} already exists in the package this server runs from, on your machine, so nothing ran. Keep the test under another name.`;
	}
}

/** No run record by this id is kept here. */
export class RunNotFound extends Schema.TaggedError<
	RunNotFound,
	{ readonly brand: unique symbol }
>("game-testing/errors/RunNotFound")("RunNotFound", { run: RunId }) {
	override get message() {
		return `No run ${this.run} is kept on the game machine.`;
	}
}

/** A running game holds no chest's value yet, as while it boots. */
export class GameStarting extends Schema.TaggedError<
	GameStarting,
	{ readonly brand: unique symbol }
>("game-testing/errors/GameStarting")("GameStarting", {}) {
	override readonly message =
		"The game holds no chest's value yet, so it is still starting.";
}

/** A spot sent from a running game was not kept, and the detail says why. */
export class SpotRefused extends Schema.TaggedError<
	SpotRefused,
	{ readonly brand: unique symbol }
>("game-testing/errors/SpotRefused")("SpotRefused", {
	spot: Schema.String,
	detail: Schema.String,
}) {
	override get message() {
		return `No spot is kept for ${this.spot}: ${this.detail}`;
	}
}

/** `serve` refused to start: it needs a token, so it never listens unguarded. */
export class TokenRequired extends Schema.TaggedError<
	TokenRequired,
	{ readonly brand: unique symbol }
>("game-testing/errors/TokenRequired")("TokenRequired", {
	/** The token file it read after `GAME_TESTING_TOKEN`. */
	file: Schema.String,
}) {
	override get message() {
		return `GAME_TESTING_TOKEN is not set and ${this.file} holds no token, so serve does not start. Put a long random string in either, such as one openssl rand -hex 32 prints; whoever holds it can run code on this machine.`;
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
		return `The game machine at ${this.url} is unavailable (${this.reason}): ${remoteNext[this.reason]}`;
	}
}

const remoteNext = {
	connect:
		"check GAME_TESTING_URL, that serve runs there, and that the tunnel is up.",
	token:
		"the token here, from GAME_TESTING_TOKEN or else game-testing/token in the config folder ($XDG_CONFIG_HOME, else ~/.config; on macOS ~/Library/Application Support), is not the token serve was started with there.",
	closed:
		"the connection closed: serve stopped there, or the tunnel dropped. Its game ended with it.",
} as const;

/**
 * `GAME_TESTING_URL` is refused. `not-http`: it is not an http or https URL. `secret`: it holds a name or a password,
 * which would reach a command line or an error.
 */
export class UrlRefused extends Schema.TaggedError<
	UrlRefused,
	{ readonly brand: unique symbol }
>("game-testing/errors/UrlRefused")("UrlRefused", {
	reason: Schema.Literals(["not-http", "secret"]),
}) {
	override get message() {
		return this.reason === "secret"
			? "GAME_TESTING_URL holds a name or a password, which would reach a command line or an error, so it is refused. Put the token in GAME_TESTING_TOKEN, and leave the URL without one."
			: "GAME_TESTING_URL is not an http or https URL, such as http://127.0.0.1:7481.";
	}
}

/** The two ends were built from different contracts: the far machine's package differs from this one's. */
export class VersionMismatch extends Schema.TaggedError<
	VersionMismatch,
	{ readonly brand: unique symbol }
>("game-testing/errors/VersionMismatch")("VersionMismatch", {
	/** The machine that refused, and the contract it speaks. */
	machine: Schema.String,
	machineSpeaks: Schema.String,
	/** The contract the caller speaks. */
	callerSpeaks: Schema.String,
}) {
	override get message() {
		const short = Str.slice(0, 12);
		return `${this.machine} speaks contract ${short(this.machineSpeaks)} and this copy speaks ${short(this.callerSpeaks)}. Run both from the same Kaepora commit.`;
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
	| ModUnreadable
	| ModTooLarge
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
