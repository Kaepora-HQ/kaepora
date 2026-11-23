/**
 * The MCP tools. An agent reads no source, so each description is the tool's manual: what it does, what to pass,
 * what comes back and what to do next. `run_script` is the core: the agent sends a whole test and gets its outcome.
 */
import { Option, Schema } from "effect";
import { Tool, Toolkit } from "effect/ai";
import { StatePlan } from "../contract/plan.ts";
import { Outcome, RunSummary } from "../contract/record.ts";
import { NameMatch, PlaceStatus } from "../contract/rpc.ts";
import {
	ModFolder,
	ReadBack,
	StartState,
	TextSpeed,
} from "../contract/state.ts";
import {
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
} from "../contract/values.ts";
import {
	DumpFailed,
	EmulatorRefused,
	EmulatorUnavailable,
	ImageBuildFailed,
	KeptScriptExists,
	LaunchFailed,
	ModTooLarge,
	ModUnreadable,
	NoFreePlace,
	NoGameHere,
	NoGraphicsCard,
	RunNotFound,
	SaveUnreadable,
	ScriptFailed,
	SessionNotFound,
	StartNotTaken,
	StateRefused,
	UnknownName,
	ValueNotFound,
	ValueRefused,
	ValueUnreadable,
	WriteUnsupported,
} from "../errors.ts";

/** What a tool can fail with. The agent reads each error's message, which says what to do next. */
const ToolFailure = Schema.Union([
	KeptScriptExists,
	ModTooLarge,
	ModUnreadable,
	UnknownName,
	StateRefused,
	StartNotTaken,
	NoFreePlace,
	NoGraphicsCard,
	ImageBuildFailed,
	LaunchFailed,
	EmulatorUnavailable,
	EmulatorRefused,
	SessionNotFound,
	ValueNotFound,
	ValueRefused,
	ValueUnreadable,
	WriteUnsupported,
	SaveUnreadable,
	NoGameHere,
	DumpFailed,
	ScriptFailed,
	RunNotFound,
]);

const state = StartState.annotate({
	description:
		"The start state: a base save, and words in the game's own names that change it. Only base is needed. base: a base save, such as new-game-autosave, a new game's first autosave with a child in Kokiri Forest by day; a base it does not know is refused with the ones it knows. location: where Link stands: a check by its spoiler log name such as KF Mido Top Left Chest, which stands him before it; a spot; a landing such as KokiriForest/Mido_House_Front; or { scene, room, position: [x, y, z], facing } in the game's own coordinates, with facing in degrees. age: child or adult; the sword and shield swap with it. withItems: items Link holds as well as what the base holds, such as Item_Range_Bow, or { item, count } such as { item: \"Item_Bomb\", count: 20 }, the count he holds; an item's bag, tier and limit come with it. withoutItems: items taken from what the base holds. equipped: items worn or on a button. reset: places put back as on a first visit: a check such as GV Chest, a scene such as ForestTemple, or a scene and room such as ForestTemple/17. story: { quests: { <quest>: <step> }, events: [<event setting>] }. set: saved values by name, written last, such as { \"PlayerStatus.CurrentRupee\": 50 }. startValues: mod unless given, so the save starts each value the mod's new game starts with, where the base still holds the game's own; base keeps the base's own, for a save older than its mod. search_names finds every name these take.",
});

const session = SessionId.annotate({
	description: "The session start_game answered.",
});

const run = RunId.annotate({
	description: "A run's id, such as 20261122-142233-k3f9.",
});

const mod = Schema.Union([ModFolder, Schema.Literal("none")]).annotate({
	description:
		"The mod the game's place gets alone: { folder } with the absolute path of a folder the patcher wrote, such as one for a seed, on the machine this server runs on; or \"none\" for the game as it ships. Unless given, the place gets the game machine's own mod: what every Kaepora seed carries, with nothing shuffled. It skips the scenes a player can skip and the scene at each new area. It also brings a weapon's container with it, such as the quiver with the Fairy Bow, lets bombs be lifted without the Goron's Bracelet, patches lifting, diving and dungeon items as a seed does, changes the Door of Time and the Master Sword's pedestal, and moves no quest on at the first arrival in Zora's Domain or the Market. It skips the game's tutorial messages and Saria's greeting. A look's save starts as a new game of its mod starts: each value the mod starts a new game with is written where the base still holds the game's own start, and plan_state lists them, each from mod <name>. Build a look's own mod with the current patcher: an older one loses the skips and may lose instant text.",
});

const textSpeed = TextSpeed.annotate({
	description:
		"instant, unless given: each page of the game's text shows at once, through Lichii's instant text add-on, as a player has it. game shows it at the game's own speed, to look at the text itself. Lichii leaves the add-on out when a mod changed the bytes it patches, as many older mods did; the run's record says the text speed the game had.",
});

/** Where an answer was cut at whole entries to keep within its size, and how to read the rest. */
const more = Schema.optionalKey(Schema.String);

/** What a run comes to, as run_script and wait_run answer it, from its record. */
export const Ran = Schema.Struct({
	/** Absent when the run ended before the game's machine kept a record of it. */
	run: Schema.optionalKey(RunId),
	outcome: Outcome,
	result: Schema.optionalKey(Schema.Unknown),
	kept: Schema.optionalKey(Schema.String),
	notKept: Schema.optionalKey(Schema.String),
	more,
	pictures: Schema.Array(Picture),
});

export const runScript = Tool.make("run_script", {
	description: `Runs one test on this machine's game and answers how it ended. It starts a game in state, runs body against it once Link can move, stops the game, and answers the run's id, its outcome, what the body returned, and the pictures the body returned, as images, four at most. A start takes one to three minutes.

body is the inside of an Effect.gen function, with game and Effect in scope. Each game call is one step of the run's record:
- yield* game.press("A") holds a button for 4 frames; yield* game.press(["ZL", "A"], { frames: 10 }) holds several.
- yield* game.stick({ stick: "Left", x: 0, y: 1 }, { frames: 30 }) holds a stick; each axis runs from -1 to 1.
- yield* game.idle(60) lets 60 frames pass. The game runs at about 60 frames a second.
- yield* game.wait(name, value, { within: "15 seconds" }) waits until a saved value holds value, 10 seconds unless given.
- const held = yield* game.read(name), and yield* game.set(name, value).
- yield* game.check(name, value) proves an effect: it holds when the value holds value now and did not at the start. yield* game.still(name, value) proves the value stayed as it started.
- const shot = yield* game.screenshot("after-chest") keeps a picture; return it to see it.
- yield* game.note("text") adds a line to the record.
- yield* game.handOver("what to ask") asks your user a question through this client and waits for the answer. They cannot see or control the game yet, so ask only what they can answer from what you have shown them. The body's limit counts their time too, so set limit above the wait. A detached run, or a client that cannot ask, finds nobody, and the run ends broken.
End with return of what you want back, such as return { opened, shot }. A name is a saved value's name from search_names, such as IsGet.Weapon_Range_Slingshot. A Bool holds true or false, an Int or Float a number, an Enum its choice's name, a String64 text.

outcome: passed when every check held. failed when a check did not hold or a wait ran out: the game said no. unchecked when the body made no check, which proves nothing. broken when the run could not ask the game: read its error; the emulator, the place or the body failed, and a second run tells a flaky machine from a fault.

The body is type-checked before any game starts, so a misspelled name or a wrong call comes back in seconds with the compiler's lines: fix it and call again. limit caps the body's time from Link in control. keep also saves the test on the game's machine, as tools/game-testing/scripts/<keep>.ts, a kept script that game-testing run runs again. A name already in use there is refused before the run starts, and only a run that passed or failed is kept; the answer says kept or notKept, and why. A kept script must pass the package's checks, bun run typecheck and bun run check, before it is committed. detach answers the run's id once its record exists; then call wait_run until the run ends. Use detach when your client stops waiting for a tool call within about five minutes.`,
	parameters: Schema.Struct({
		about: Schema.String.annotate({
			description:
				"One line on what the test shows, such as: The top left chest in Mido's house opens to A.",
		}),
		state,
		body: Schema.String.annotate({
			description:
				"The test: the inside of an Effect.gen function, with game and Effect in scope.",
		}),
		mod: Schema.optionalKey(mod),
		textSpeed: Schema.optionalKey(textSpeed),
		limit: Schema.optionalKey(
			Schema.String.check(
				Schema.makeFilter((text) =>
					Option.isSome(Schema.decodeOption(Limit)(text))
						? undefined
						: "a time such as 2 minutes or 90 seconds"
				)
			).annotate({
				description:
					"The most the body may take once Link can move, such as 2 minutes; 5 minutes unless given.",
			})
		),
		keep: Schema.optionalKey(
			Schema.String.check(
				Schema.isPattern(/^[a-z0-9]+(?:-[a-z0-9]+)*$/u)
			).annotate({
				description:
					"A name in lowercase words joined by hyphens, such as deku-tree-slingshot-chest, to keep the test under.",
			})
		),
		detach: Schema.optionalKey(
			Schema.Boolean.annotate({
				description: "Answer the run's id at once; wait_run gives the end.",
			})
		),
	}),
	success: Ran,
	failure: ToolFailure,
})
	// The body runs as the user who runs this server, with that user's reach.
	.annotate(Tool.Destructive, true)
	.annotate(Tool.OpenWorld, true);

export const waitRun = Tool.make("wait_run", {
	description:
		"Waits up to 45 seconds for a run to end, reading its record, and answers as run_script does: first the run's id and outcome, then what the body returned, then the pictures the result names as images, four at most. It reads any run on this machine by its id, whoever started it: a detached run_script, a game from start_game, or one that ended long ago. While the run goes on, its outcome is running: call wait_run again. get_run reads the rest of the record, its steps a part at a time.",
	parameters: Schema.Struct({ run }),
	success: Ran,
	failure: ToolFailure,
})
	.annotate(Tool.Readonly, true)
	.annotate(Tool.OpenWorld, false);

export const searchNames = Tool.make("search_names", {
	description:
		"Finds the names that contain text, in the name or in its line, shortest first. kind narrows it: value (saved values, for read_values, set_values, a state's set, and a body's read, wait, set, check and still), item (for a state's withItems, withoutItems and equipped), quest (with its steps, for story.quests), event (event settings, for story.events), scene (for location and reset), landing (for location), check (a check by its spoiler log name, for location and reset), spot (a place Link can stand, for location) or base (a base save, for base). Answers names, each with its kind, the name and a line on it. Every name a state or a body takes is one of these, and one that is not is refused with its nearest names.",
	parameters: Schema.Struct({
		text: Schema.String.annotate({
			description: "Part of a name, such as Slingshot or TreasureBox.",
		}),
		kind: Schema.optionalKey(NameKind),
		limit: Schema.optionalKey(
			Schema.Int.check(Schema.isBetween({ minimum: 1, maximum: 200 })).annotate(
				{ description: "How many names to answer, 40 unless given." }
			)
		),
	}),
	success: Schema.Struct({ names: Schema.Array(NameMatch), more }),
	failure: ToolFailure,
})
	.annotate(Tool.Readonly, true)
	.annotate(Tool.OpenWorld, false);

export const planState = Tool.make("plan_state", {
	description:
		"Shows what a start state writes into its base save, without starting a game: each value, what the base held, and the word it came from, or mod <name> for a value the mod's new game starts with; and the values named by the files of a place it resets that it leaves as the base holds them, so you can add one with set. A state that cannot be honoured is refused with what to change. A state plan_state takes, start_game and run_script take too.",
	parameters: Schema.Struct({ state, mod: Schema.optionalKey(mod) }),
	success: Schema.Struct({ ...StatePlan.fields, more }),
	failure: ToolFailure,
})
	.annotate(Tool.Readonly, true)
	.annotate(Tool.OpenWorld, false);

export const startGame = Tool.make("start_game", {
	description:
		"Starts a game in the state, to look around by hand, and answers once Link can move: its session, its run's id, its place, and each value the state wrote as the game holds it. Drive it by its session with press, stick, read_values, set_values and screenshot, and end it with stop_game. Every call is a step in the run's record. The game stops by itself when no call names its session for 10 minutes, and its run then ends stopped; it stops after 2 hours in any case. A start takes one to three minutes. A test that must pass or fail is a run_script body instead: only a body's checks give an outcome.",
	parameters: Schema.Struct({
		state,
		mod: Schema.optionalKey(mod),
		textSpeed: Schema.optionalKey(textSpeed),
	}),
	success: Schema.Struct({
		session: SessionId,
		run: RunId,
		place: PlaceId,
		readBack: ReadBack,
		more,
	}),
	failure: ToolFailure,
})
	.annotate(Tool.Destructive, false)
	.annotate(Tool.OpenWorld, false);

export const stopGame = Tool.make("stop_game", {
	description:
		"Stops the session's game, gives its place back, and ends its run stopped. Answers the run's id and its outcome as the record holds it; get_run reads the record.",
	parameters: Schema.Struct({ session }),
	success: Schema.Struct({ run: RunId, outcome: Outcome }),
	failure: ToolFailure,
}).annotate(Tool.OpenWorld, false);

export const press = Tool.make("press", {
	description:
		"Holds the buttons together in the session's game for frames frames, 4 unless given, then lets go, and answers once the game has shown those frames. Buttons: A, B, X, Y, L, R, ZL, ZR, Plus, Minus, Up, Down, Left and Right (the D-pad), StickL and StickR (a stick pressed in). L or R with Plus and Minus is refused: that chord stops Lichii.",
	parameters: Schema.Struct({
		session,
		buttons: Buttons,
		frames: Schema.optionalKey(Frames),
	}),
	failure: ToolFailure,
})
	.annotate(Tool.Destructive, false)
	.annotate(Tool.OpenWorld, false);

export const stick = Tool.make("stick", {
	description:
		"Holds a stick at a position in the session's game for frames frames, then lets it go back to the centre. x and y run from -1 to 1, and y 1 is the stick pushed up. The game runs at about 60 frames a second, so 60 frames is about a second of walking.",
	parameters: Schema.Struct({
		session,
		position: StickPosition,
		frames: Frames,
	}),
	failure: ToolFailure,
})
	.annotate(Tool.Destructive, false)
	.annotate(Tool.OpenWorld, false);

export const readValues = Tool.make("read_values", {
	description:
		"Reads saved values by name from the session's running game, as the game holds them now. Answers each name with its value: a Bool true or false, an Int or Float a number, an Enum its choice's name, a String64 text. search_names finds the names.",
	parameters: Schema.Struct({
		session,
		values: Schema.NonEmptyArray(Schema.String).annotate({
			description: "Saved values' names, such as PlayerStatus.CurrentRupee.",
		}),
	}),
	success: Schema.Struct({
		values: Schema.Record(Schema.String, Value),
		more,
	}),
	failure: ToolFailure,
})
	// Each read is a step in the run's record.
	.annotate(Tool.Destructive, false)
	.annotate(Tool.OpenWorld, false);

export const setValues = Tool.make("set_values", {
	description:
		"Writes saved values by name into the session's running game, at once. The game may show a change only when it reads the value again, such as on entering a room. Needs a Lichii build that can write memory.",
	parameters: Schema.Struct({
		session,
		values: Schema.Record(Schema.String, Value).annotate({
			description:
				'Each name with the value to write, such as { "PlayerStatus.CurrentRupee": 50 }.',
		}),
	}),
	failure: ToolFailure,
})
	.annotate(Tool.Destructive, false)
	.annotate(Tool.OpenWorld, false);

export const screenshot = Tool.make("screenshot", {
	description:
		"Shows the session's game as it is now, as an image. The picture is kept with the run's record too, under label when you give one.",
	parameters: Schema.Struct({
		session,
		label: Schema.optionalKey(Schema.String),
	}),
	success: Picture,
	failure: ToolFailure,
})
	// The picture and its step are kept with the run.
	.annotate(Tool.Destructive, false)
	.annotate(Tool.OpenWorld, false);

export const places = Tool.make("places", {
	description:
		"Lists this machine's places, where games run one at a time each: which are free, and the session, run, owner and start of the game in each held one. A start waits up to 10 minutes for a free place.",
	parameters: Tool.EmptyParams,
	success: Schema.Array(PlaceStatus),
	failure: ToolFailure,
})
	.annotate(Tool.Readonly, true)
	.annotate(Tool.OpenWorld, false);

export const listRuns = Tool.make("list_runs", {
	description:
		"Lists the runs on this machine, newest first: each run's id, outcome, kept script and how long it took. script narrows it to one kept script's runs, by its path such as scripts/mido-top-left-chest.ts. unreadable counts the records it could not read, such as one written by another version of the tool.",
	parameters: Schema.Struct({
		script: Schema.optionalKey(Schema.String),
		limit: Schema.optionalKey(
			Schema.Int.check(Schema.isBetween({ minimum: 1, maximum: 200 })).annotate(
				{ description: "How many runs to answer, 20 unless given." }
			)
		),
	}),
	success: Schema.Struct({
		runs: Schema.Array(RunSummary),
		unreadable: Schema.Int,
		more,
	}),
	failure: ToolFailure,
})
	.annotate(Tool.Readonly, true)
	.annotate(Tool.OpenWorld, false);

export const getRun = Tool.make("get_run", {
	description:
		"Answers one run's record a part at a time, each within 20,000 bytes. First come the run's id, its outcome and what the body returned; then the state it asked for, how many values its plan wrote, each value as the game held it at the start, and the rest of the record; then as many whole steps as fit, each with its input, result, time and check. part says which steps are here: from, count, and of in all. When steps remain, next names the from to ask for, and get_run with that from gives the next part. With pictures, the screenshots of this part's steps come back too, as images, and a part ends early to hold four at most. With plan, the parts hold the start's plan in place of the steps: each value it wrote, what the base held and where it came from (a word of the state, the mod, or a rule of the tool's own), and what it left alone.",
	parameters: Schema.Struct({
		run,
		from: Schema.optionalKey(
			Schema.Int.check(Schema.isGreaterThanOrEqualTo(0)).annotate({
				description: "The first step to answer, 0 unless given.",
			})
		),
		pictures: Schema.optionalKey(Schema.Boolean),
		plan: Schema.optionalKey(Schema.Boolean),
	}),
	success: Schema.Struct({
		record: Schema.Unknown,
		pictures: Schema.Array(Picture),
	}),
	failure: ToolFailure,
})
	.annotate(Tool.Readonly, true)
	.annotate(Tool.OpenWorld, false);

export const tools = Toolkit.make(
	runScript,
	waitRun,
	searchNames,
	planState,
	startGame,
	stopGame,
	press,
	stick,
	readValues,
	setValues,
	screenshot,
	places,
	listRuns,
	getRun
);
