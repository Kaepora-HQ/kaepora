import { Effect, Schema, SchemaGetter, SchemaIssue } from "effect";
import { StatePlan } from "./plan.ts";
import { ReadBack, StartState, TextSpeed } from "./state.ts";
import { PlaceId, RunId, SessionId, Value } from "./values.ts";

/** An instant as ISO 8601 text, decoded to a `Temporal.Instant`. */
export const Instant = Schema.String.pipe(
	Schema.decodeTo(
		Schema.declare(
			(input: unknown): input is Temporal.Instant =>
				input instanceof Temporal.Instant
		),
		{
			decode: SchemaGetter.transformEffect((text: string) =>
				Effect.try({
					try: () => Temporal.Instant.from(text),
					catch: () =>
						new SchemaIssue.InvalidValue(
							{ expected: "an ISO 8601 instant" },
							text
						),
				})
			),
			encode: SchemaGetter.transform((instant: Temporal.Instant) =>
				instant.toString()
			),
		}
	)
);

/**
 * How a run ended. `passed`: it finished and every check held. `unchecked`: it finished with no check, so it proved
 * nothing. `failed`: a check did not hold, or the script failed on the game's answer, so the game said no. `broken`:
 * the run could not ask, because the emulator, the place, the connection or the script itself failed. Only `broken`
 * is worth a rerun to tell a flaky machine from a real fault. `running` while it lasts; `stopped` when a game held by
 * hand was ended, by `stop_game` or by no call naming it, and `reason` says which; `skipped` when it needs a person and
 * had none.
 */
export const Outcome = Schema.Union([
	Schema.TaggedStruct("running", {}),
	Schema.TaggedStruct("passed", {}),
	Schema.TaggedStruct("unchecked", {}),
	Schema.TaggedStruct("failed", { reason: Schema.String }),
	Schema.TaggedStruct("broken", {
		reason: Schema.String,
		error: Schema.String,
	}),
	Schema.TaggedStruct("stopped", { reason: Schema.String }),
	Schema.TaggedStruct("skipped", { reason: Schema.String }),
]);
export type Outcome = typeof Outcome.Type;

/** One command as it ran: what was asked, what came back, and how long it took. */
export const Step = Schema.Struct({
	at: Instant,
	command: Schema.Literals([
		"press",
		"press-until",
		"dialog",
		"stick",
		"idle",
		"wait",
		"read",
		"set",
		"check",
		"screenshot",
		"hand-over",
		"note",
	]),
	/** The command's input, as sent. */
	input: Schema.Unknown,
	/** Its result, absent for a command that gives none, or the failure's tag and message. */
	result: Schema.Union([
		Schema.TaggedStruct("ok", { value: Schema.optionalKey(Schema.Unknown) }),
		Schema.TaggedStruct("error", {
			tag: Schema.String,
			message: Schema.String,
		}),
	]),
	milliseconds: Schema.Finite,
	/**
	 * For a check: its form, the value expected, what the value held at the start when a save holds it, what it holds
	 * now, and whether the check held.
	 */
	check: Schema.optionalKey(
		Schema.Struct({
			form: Schema.Literals(["check", "still"]),
			value: Schema.String,
			expected: Value,
			start: Schema.optionalKey(Value),
			actual: Value,
			held: Schema.Boolean,
		})
	),
	/** Pictures this step kept, relative to the run's folder. */
	pictures: Schema.optionalKey(Schema.Array(Schema.String)),
});
export type Step = typeof Step.Type;

/** What identifies the script a run came from, so runs of one script can be put side by side. */
export const ScriptSource = Schema.Struct({
	/** The kept script's path under `tools/game-testing/scripts/`, absent for one written inline. */
	path: Schema.optionalKey(Schema.String),
	/** SHA-256 of the source the run executed. */
	digest: Schema.String,
	/** The source itself, for one written inline. */
	source: Schema.optionalKey(Schema.String),
});

export class RunRecord extends Schema.Class<
	RunRecord,
	{ readonly brand: unique symbol }
>("game-testing/contract/record/RunRecord")({
	id: RunId,
	session: SessionId,
	machine: Schema.String,
	/** The place the game ran in, absent when the start ended before one was taken. */
	place: Schema.optionalKey(PlaceId),
	/** The Lichii folder the place ran, and the build its debug server names once it answers. */
	emulator: Schema.Struct({
		program: Schema.String,
		build: Schema.optionalKey(Schema.String),
	}),
	/** The key the game file's tables are cached under. */
	game: Schema.String,
	/**
	 * The mod the place got, by its folder's name and the SHA-256 over its files, each path then its bytes, or `none`;
	 * absent when the start ended before a place was taken.
	 */
	mod: Schema.optionalKey(
		Schema.Union([
			Schema.Struct({ name: Schema.String, digest: Schema.String }),
			Schema.Literal("none"),
		])
	),
	/**
	 * How the running game wrote its text, as read from its program once Link could move: Lichii leaves its instant
	 * text out when a mod changed the bytes it patches. Absent when the start never got that far.
	 */
	textSpeed: Schema.optionalKey(TextSpeed),
	script: Schema.optionalKey(ScriptSource),
	state: StartState,
	/**
	 * What the start wrote into its base, each value with where it came from, and what it left alone; absent until the
	 * state is planned.
	 */
	plan: Schema.optionalKey(
		Schema.Struct({
			edits: StatePlan.fields.edits,
			leftAlone: StatePlan.fields.leftAlone,
		})
	),
	/** Every value the state wrote, as the running game held it once Link could move; absent until then. */
	readBack: Schema.optionalKey(ReadBack),
	steps: Schema.Array(Step),
	/** What the script returned, encoded. */
	result: Schema.optionalKey(Schema.Unknown),
	outcome: Outcome,
	startedAt: Instant,
	endedAt: Schema.optionalKey(Instant),
	/** The run this one repeats, when `run --rerun-broken` ran it again after a broken run. */
	attemptOf: Schema.optionalKey(RunId),
	/** The OpenTelemetry trace the run's start belongs to, which the collector holds when traces are sent. */
	traceId: Schema.optionalKey(Schema.String),
}) {}

/** One line of a run list. */
export const RunSummary = Schema.Struct({
	id: RunId,
	script: Schema.optionalKey(Schema.String),
	outcome: Outcome,
	startedAt: Instant,
	milliseconds: Schema.optionalKey(Schema.Finite),
});
export type RunSummary = typeof RunSummary.Type;

/** A run list, newest first, and how many records it could not read. */
export const RunList = Schema.Struct({
	runs: Schema.Array(RunSummary),
	unreadable: Schema.Int,
});
export type RunList = typeof RunList.Type;
