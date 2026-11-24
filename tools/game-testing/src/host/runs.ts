/**
 * The records of the runs on this machine: one folder per run under `~/.local/state/game-testing/runs/`, with
 * `record.json`, the debug wire as `wire.jsonl`, the run's pictures and its saves. A record is written whole after
 * every step, so a run that dies leaves what it reached.
 */
import type { Scope } from "effect";
import {
	Array as Arr,
	Cause,
	Clock,
	Context,
	Effect,
	Exit,
	FileSystem,
	Layer,
	Option,
	Order,
	Ref,
	Schema,
	Semaphore,
} from "effect";
import { UserDirectories } from "effect/persistence";
import type {
	KeptSave,
	Outcome,
	RunList,
	RunSummary,
	Step,
	UnmeasuredFile,
} from "../contract/record.ts";
import { RunRecord } from "../contract/record.ts";
import type { ReadBack, TextSpeed } from "../contract/state.ts";
import type { PlaceId } from "../contract/values.ts";
import { Picture, RunId } from "../contract/values.ts";
import type { WireLog } from "../emulator/emulator.ts";
import { wireLog } from "../emulator/emulator.ts";
import { CopyFailed, RunNotFound } from "../errors.ts";
import { sha256 } from "../sha256.ts";

/** What a run's record holds before its state is planned. */
export type RunHeader = Omit<
	ConstructorParameters<typeof RunRecord>[0],
	| "steps"
	| "outcome"
	| "startedAt"
	| "endedAt"
	| "readBack"
	| "plan"
	| "result"
	| "place"
	| "mod"
>;

/** Writes one run's record as it goes. */
export interface Recorder {
	readonly end: (outcome: Outcome, result?: unknown) => Effect.Effect<void>;
	readonly id: RunId;
	/** Keeps a picture in the run's folder, named by its place in the run. */
	readonly picture: (
		png: Uint8Array,
		label: string | undefined
	) => Effect.Effect<Picture>;
	/** The start's plan: each value it writes, with where it came from, and what it leaves alone. */
	readonly planned: (
		plan: NonNullable<RunRecord["plan"]>
	) => Effect.Effect<void>;
	/**
	 * Keeps a save in the run's folder, numbered by its place in the run's order, and lists it in the record after the
	 * steps that have run.
	 */
	readonly save: (save: {
		readonly kind: KeptSave["kind"];
		readonly slot: string;
		readonly progress: Uint8Array;
		readonly caption: Uint8Array;
		readonly afterPress?: number;
	}) => Effect.Effect<void>;
	/** Link can move: the emulator's build, how the game writes its text, and the values the state wrote as it holds them. */
	readonly started: (details: {
		readonly build: string;
		readonly readBack: typeof ReadBack.Type;
		readonly textSpeed: TextSpeed;
	}) => Effect.Effect<void>;
	readonly step: (step: Step) => Effect.Effect<void>;
	/** A place was taken: the place, the mod it gets with its digest, and the mod's files the size table cannot measure. */
	readonly taken: (details: {
		readonly place: PlaceId;
		readonly mod: NonNullable<RunRecord["mod"]>;
		readonly unmeasured: readonly UnmeasuredFile[];
	}) => Effect.Effect<void>;
	readonly wire: WireLog;
}

const RecordJson = Schema.fromJsonString(RunRecord);
/** The files a run's folder keeps beside its record: its pictures and its saves. */
const keptName = /^(?:picture-\d+\.png|save-\d+-(?:progress|caption)\.sav)$/u;

/** Why a run that ended without its outcome is broken: what it failed on, or its caller going away. */
const brokenBy = (exit: Exit.Exit<unknown, unknown>): Outcome => {
	if (Exit.isSuccess(exit) || Cause.hasInterruptsOnly(exit.cause)) {
		return {
			_tag: "broken",
			reason: "its caller went away before the script finished",
			error: "",
		};
	}
	const error = Cause.squash(exit.cause);
	return {
		_tag: "broken",
		reason: "the game did not start",
		error: error instanceof Error ? error.message : String(error),
	};
};

export class Runs extends Context.Service<
	Runs,
	{
		/**
		 * Opens a run's record. When the scope closes, a record with no outcome yet is written `broken`, with what the
		 * scope closed on.
		 */
		readonly begin: (
			header: RunHeader
		) => Effect.Effect<Recorder, never, Scope.Scope>;
		/** The newest runs first, of one script when it is given, and how many records could not be read. */
		readonly list: (filter: {
			readonly script?: string | undefined;
			readonly limit: number;
		}) => Effect.Effect<RunList>;
		readonly get: (run: RunId) => Effect.Effect<RunRecord, RunNotFound>;
		/** A picture or a kept save's file of a run, by its name in the run's folder. */
		readonly file: (
			run: RunId,
			path: string
		) => Effect.Effect<Uint8Array, RunNotFound>;
		/** The folder that holds a run's record, pictures and saves. */
		readonly folder: (run: RunId) => string;
		/** Keeps a run another machine recorded, with its pictures and saves by name, as its own folder holds them there. */
		readonly copy: (
			record: RunRecord,
			files: readonly { readonly path: string; readonly bytes: Uint8Array }[]
		) => Effect.Effect<void, CopyFailed>;
	}
>()("game-testing/host/runs") {
	static readonly layer = Layer.effect(
		Runs,
		Effect.gen(function* () {
			const fs = yield* FileSystem.FileSystem;
			const directories = yield* UserDirectories.UserDirectories;
			const state = yield* Effect.orDie(directories.state("game-testing"));
			const root = `${state}/runs`;
			yield* Effect.orDie(fs.makeDirectory(root, { recursive: true }));
			const folderOf = (run: RunId) => `${root}/${run}`;

			/** Written beside and renamed over the last one, so a reader only ever sees a whole record. */
			const write = Effect.fnUntraced(function* (record: RunRecord) {
				const text = yield* Schema.encodeEffect(RecordJson)(record);
				const folder = folderOf(record.id);
				yield* fs.writeFileString(`${folder}/record.json.new`, text);
				yield* fs.rename(`${folder}/record.json.new`, `${folder}/record.json`);
			}, Effect.orDie);

			const get = Effect.fn("Runs.get")(function* (run: RunId) {
				const text = yield* fs
					.readFileString(`${folderOf(run)}/record.json`)
					.pipe(Effect.mapError(() => RunNotFound.make({ run })));
				return yield* Effect.orDie(Schema.decodeEffect(RecordJson)(text));
			});

			const begin = Effect.fn("Runs.begin")(function* (header: RunHeader) {
				const folder = folderOf(header.id);
				// Not recursive, so a run whose name is already kept fails here instead of writing over it.
				yield* Effect.orDie(fs.makeDirectory(folder));
				const millis = yield* Clock.currentTimeMillis;
				const record = yield* Ref.make(
					RunRecord.make({
						...header,
						steps: [],
						outcome: { _tag: "running" },
						startedAt: Temporal.Instant.fromEpochMilliseconds(millis),
					})
				);
				const permit = yield* Semaphore.make(1);
				const update = (change: (record: RunRecord) => RunRecord) =>
					permit.withPermit(
						Effect.flatMap(Ref.updateAndGet(record, change), write)
					);
				const pictures = yield* Ref.make(0);
				const saves = yield* Ref.make(0);
				const end = Effect.fnUntraced(function* (
					outcome: Outcome,
					result?: unknown
				) {
					const ended = yield* Clock.currentTimeMillis;
					yield* update((current) =>
						current.outcome._tag === "running"
							? RunRecord.make({
									...current,
									outcome,
									...(result === undefined ? {} : { result }),
									endedAt: Temporal.Instant.fromEpochMilliseconds(ended),
								})
							: current
					);
				});
				yield* update((current) => current);
				yield* Effect.addFinalizer((exit) => exit.pipe(brokenBy, end));
				const recorder: Recorder = {
					id: header.id,
					wire: yield* wireLog(`${folder}/wire.jsonl`).pipe(
						Effect.provideService(FileSystem.FileSystem, fs),
						Effect.orDie
					),
					started: ({ build, readBack, textSpeed }) =>
						update((current) =>
							RunRecord.make({
								...current,
								emulator: { ...current.emulator, build },
								readBack,
								textSpeed,
							})
						),
					planned: (plan) =>
						update((current) => RunRecord.make({ ...current, plan })),
					step: (step) =>
						update((current) =>
							RunRecord.make({ ...current, steps: [...current.steps, step] })
						),
					save: Effect.fnUntraced(function* ({
						kind,
						slot,
						progress,
						caption,
						afterPress,
					}) {
						const order = yield* Ref.getAndUpdate(saves, (n) => n + 1);
						const kept = (part: "progress" | "caption", bytes: Uint8Array) =>
							Effect.as(
								Effect.orDie(
									fs.writeFile(`${folder}/save-${order}-${part}.sav`, bytes)
								),
								{ path: `save-${order}-${part}.sav`, sha256: sha256(bytes) }
							);
						const files = {
							progress: yield* kept("progress", progress),
							caption: yield* kept("caption", caption),
						};
						const found = yield* Clock.currentTimeMillis;
						yield* update((current) =>
							RunRecord.make({
								...current,
								saves: [
									...(current.saves ?? []),
									{
										kind,
										slot,
										afterSteps: current.steps.length,
										...(afterPress === undefined ? {} : { afterPress }),
										found: Temporal.Instant.fromEpochMilliseconds(found),
										...files,
									},
								],
							})
						);
					}),
					taken: ({ place, mod, unmeasured }) =>
						update((current) =>
							RunRecord.make({
								...current,
								place,
								mod,
								...(unmeasured.length === 0 ? {} : { unmeasured }),
							})
						),
					picture: Effect.fnUntraced(function* (png, label) {
						const count = yield* Ref.updateAndGet(pictures, (n) => n + 1);
						const path = `picture-${count}.png`;
						yield* Effect.orDie(fs.writeFile(`${folder}/${path}`, png));
						return Picture.make({
							path,
							png,
							...(label === undefined ? {} : { label }),
						});
					}),
					end,
				};
				return recorder;
			});

			const list = Effect.fn("Runs.list")(function* (filter: {
				readonly script?: string | undefined;
				readonly limit: number;
			}) {
				const names = yield* Effect.orDie(fs.readDirectory(root));
				const newest = Arr.sort(
					Arr.filter(names, (name) => Schema.is(RunId)(name)),
					Order.flip(Order.String)
				);
				// A record another contract wrote may not decode; it is counted, not listed.
				const records = yield* Effect.forEach(
					newest,
					(name) =>
						fs
							.readFileString(`${folderOf(RunId.make(name))}/record.json`)
							.pipe(
								Effect.flatMap(Schema.decodeEffect(RecordJson)),
								Effect.option
							),
					{ concurrency: 4 }
				);
				const runs = Arr.take(
					Arr.flatMap(records, (found): RunSummary[] => {
						if (found._tag === "None") {
							return [];
						}
						const run = found.value;
						if (
							filter.script !== undefined &&
							run.script?.path !== filter.script
						) {
							return [];
						}
						return [
							{
								id: run.id,
								outcome: run.outcome,
								startedAt: run.startedAt,
								...(run.script?.path === undefined
									? {}
									: { script: run.script.path }),
								...(run.endedAt === undefined
									? {}
									: {
											milliseconds:
												run.endedAt.epochMilliseconds -
												run.startedAt.epochMilliseconds,
										}),
							},
						];
					}),
					filter.limit
				);
				const listed: RunList = {
					runs,
					unreadable: Arr.filter(records, Option.isNone).length,
				};
				return listed;
			});

			const file = Effect.fn("Runs.file")(function* (run: RunId, path: string) {
				if (!keptName.test(path)) {
					return yield* RunNotFound.make({ run });
				}
				return yield* fs
					.readFile(`${folderOf(run)}/${path}`)
					.pipe(Effect.mapError(() => RunNotFound.make({ run })));
			});

			const copy = Effect.fnUntraced(function* (
				record: RunRecord,
				files: readonly { readonly path: string; readonly bytes: Uint8Array }[]
			) {
				// A file is written only by a picture's or a save's own name, so nothing lands outside the run's folder.
				const refused = Arr.findFirst(
					files,
					({ path }) => !keptName.test(path)
				);
				if (Option.isSome(refused)) {
					return yield* CopyFailed.make({
						run: record.id,
						reason: `the game machine named a file ${refused.value.path}, which is no picture's or save's name`,
					});
				}
				const folder = folderOf(record.id);
				yield* Effect.orDie(fs.makeDirectory(folder, { recursive: true }));
				for (const kept of files) {
					yield* Effect.orDie(
						fs.writeFile(`${folder}/${kept.path}`, kept.bytes)
					);
				}
				yield* write(record);
			});

			return Runs.of({ begin, list, get, file, folder: folderOf, copy });
		})
	);
}
