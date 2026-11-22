import { Effect, Schema, SchemaGetter, SchemaIssue } from "effect";
import { ModSource, ReadBack, StartState } from "./state.ts";
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
 * How a run ended. `passed`: it finished and every check held. `failed`: a check did not hold, or the script failed
 * on the game's answer, so the game said no. `broken`: the run could not ask, because the emulator, the place, the
 * connection or the script itself failed. Only `broken` is worth a rerun to tell a flaky machine from a real fault.
 * `running` while it lasts; `stopped` when its caller ended it before the script finished; `skipped` when it needs a
 * person and had none.
 */
export const Outcome = Schema.Union([
	Schema.TaggedStruct("running", {}),
	Schema.TaggedStruct("passed", {}),
	Schema.TaggedStruct("failed", { reason: Schema.String }),
	Schema.TaggedStruct("broken", {
		reason: Schema.String,
		error: Schema.String,
	}),
	Schema.TaggedStruct("stopped", {}),
	Schema.TaggedStruct("skipped", { reason: Schema.String }),
]);
export type Outcome = typeof Outcome.Type;

/** One command as it ran: what was asked, what came back, and how long it took. */
export const Step = Schema.Struct({
	at: Instant,
	command: Schema.Literals([
		"press",
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
	/** Its result, or the failure's message. */
	result: Schema.Union([
		Schema.TaggedStruct("ok", { value: Schema.Unknown }),
		Schema.TaggedStruct("error", {
			tag: Schema.String,
			message: Schema.String,
		}),
	]),
	milliseconds: Schema.Finite,
	/** For a check: the value expected and whether it held. */
	check: Schema.optionalKey(
		Schema.Struct({
			value: Schema.String,
			expected: Value,
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
	place: PlaceId,
	/** Lichii's build, as its log names it, and the program's path. */
	emulator: Schema.Struct({ build: Schema.String, program: Schema.String }),
	/** The key the game file's tables are cached under. */
	game: Schema.String,
	mod: Schema.optionalKey(
		Schema.Struct({ source: ModSource, digest: Schema.String })
	),
	script: Schema.optionalKey(ScriptSource),
	state: StartState,
	/** Every value the state wrote, as the running game held it after the start. */
	readBack: ReadBack,
	steps: Schema.Array(Step),
	/** What the script returned, encoded. */
	result: Schema.optionalKey(Schema.Unknown),
	outcome: Outcome,
	startedAt: Instant,
	endedAt: Schema.optionalKey(Instant),
	/** The run this one repeats, when `run --rerun-broken` ran it again after a broken run. */
	attemptOf: Schema.optionalKey(RunId),
	/** The OpenTelemetry trace of the run, when traces are sent. */
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
