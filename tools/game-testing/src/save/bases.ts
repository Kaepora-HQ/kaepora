/**
 * The saves a start is built on. A base's three files are the game's data and stay on the machine with the game, in
 * `bases/<name>/` under its data folder. The tracked bases are the rows of `tables/bases.tsv`, which git decides. A base
 * named on this machine keeps its row beside its files, as `row.json`, and works at once; once git has its row, the row
 * of git is the one read. A start can also be built on a save a run kept, read from the run's own folder.
 */
import {
	Array as Arr,
	Context,
	Effect,
	FileSystem,
	Layer,
	Option,
	Order,
	Record as Rec,
	Schema,
} from "effect";
import { UserDirectories } from "effect/persistence";
import type { Base, Catalog, KeptFrom, NewBase } from "../contract/bases.ts";
import { Base as BaseSchema } from "../contract/bases.ts";
import type { PlannedBase } from "../contract/plan.ts";
import type { KeptSave } from "../contract/record.ts";
import type { Location, RunSave, StartState } from "../contract/state.ts";
import type { RunId } from "../contract/values.ts";
import { Position } from "../contract/values.ts";
import { nearest, valueHash } from "../data/game-data.ts";
import {
	NameTaken,
	SaveUnreadable,
	StateRefused,
	UnknownName,
} from "../errors.ts";
import { Runs } from "../host/runs.ts";
import { sha256 } from "../sha256.ts";
import { decode, read } from "./codec.ts";
import { ageIn, degrees, unnamed } from "./held.ts";
import { OwnTables } from "./tables.ts";

type FileName = "progress" | "caption" | "option";

/** The three files of a start's base on this machine, each with the SHA-256 it must have, and how a plan names it. */
export interface Footing {
	readonly base: PlannedBase;
	readonly files: Readonly<
		Record<FileName, { readonly path: string; readonly sha256: string }>
	>;
	/** What gives the files' SHA-256, as a sentence names it: a base's row, or a run's record. */
	readonly listedIn: string;
}

/** The base whose option.sav a base named from files without one takes, as `child-after-impa` does. */
const newGame = "new-game-autosave";

const RowJson = Schema.fromJsonString(BaseSchema);

/** The values of a save that say where Link stands: the scene, its dungeon entry, the room, the place and the facing. */
const StandValues = Schema.Struct({
	scene: Schema.String,
	dungeon: Schema.String,
	room: Schema.Int,
	position: Position,
	radians: Schema.Finite,
});
const standNames = {
	scene: "Sequence_CurrentBanc",
	dungeon: unnamed.dungeon,
	room: "DungeonRoom",
	position: "PlayerStatus.SavePos",
	radians: "PlayerStatus.SavePosRadY",
} as const;

/** A kept save as a sentence says it: its kind, its run, and the step, or the press of a step, it followed. */
const keptAbout = (save: KeptSave, run: RunId) =>
	`${save.kind === "autosave" ? "An autosave" : "A save from the menu"} the game wrote in run ${run}, ${
		save.afterPress === undefined
			? `after its step ${save.afterSteps}`
			: `at press ${save.afterPress} of its step ${save.afterSteps + 1}`
	}.`;

export class Bases extends Context.Service<
	Bases,
	{
		/** Every base: the tracked ones, then the ones named on this machine that git does not have yet. */
		readonly all: Effect.Effect<readonly Base[]>;
		/**
		 * Where a state's base is on this machine: a base by its name, or a save a run kept. A base whose recorded row
		 * cannot be read is refused with the row's file.
		 */
		readonly footing: (
			base: StartState["base"]
		) => Effect.Effect<Footing, UnknownName | StateRefused | SaveUnreadable>;
		/** A file of a start's base, checked against its SHA-256. */
		readonly bytes: (
			footing: Footing,
			file: FileName
		) => Effect.Effect<Uint8Array, SaveUnreadable>;
		/** Keeps a save the game wrote as a base: its files in the base's folder, and its row beside them. */
		readonly name: (
			request: NewBase
		) => Effect.Effect<Base, NameTaken | StateRefused | SaveUnreadable>;
		/** Every base and named start, with what an agent choosing a start reads. */
		readonly catalog: Effect.Effect<Catalog>;
	}
>()("game-testing/save/bases") {
	static readonly layer = Layer.effect(
		Bases,
		Effect.gen(function* () {
			const own = yield* OwnTables;
			const runs = yield* Runs;
			const fs = yield* FileSystem.FileSystem;
			const directories = yield* UserDirectories.UserDirectories;
			const home = yield* directories.data("game-testing").pipe(Effect.orDie);
			const folder = `${home}/bases`;
			yield* Effect.orDie(fs.makeDirectory(folder, { recursive: true }));

			/**
			 * The rows named on this machine that git does not have yet, each read whole or refused with its file. A folder
			 * without a row is a tracked base's.
			 */
			const recorded = Effect.gen(function* () {
				const entries = yield* Effect.orDie(fs.readDirectory(folder));
				const names = Arr.filter(entries, (entry) => !own.bases.has(entry));
				const rows = yield* Effect.forEach(
					Arr.sort(names, Order.String),
					(entry) => {
						const path = `${folder}/${entry}/row.json`;
						const row = (text: string) =>
							Schema.decodeEffect(RowJson)(text).pipe(
								Effect.mapError((issue) =>
									SaveUnreadable.make({
										path,
										detail: `it is not a base's row: ${issue.message}`,
									})
								),
								Effect.result,
								Effect.map((parsed) => [entry, parsed] as const)
							);
						return fs.readFileString(path).pipe(
							Effect.option,
							Effect.flatMap((text) =>
								Effect.transposeOption(Option.map(text, row))
							)
						);
					},
					{ concurrency: 4 }
				);
				return new Map(Arr.getSomes(rows));
			});
			const all = Effect.map(recorded, (here) => [
				...own.bases.values(),
				...Arr.getSuccesses(here.values()),
			]);

			const filesOf = (base: Base): Footing["files"] => ({
				progress: {
					path: `${folder}/${base.name}/progress.sav`,
					sha256: base.progress,
				},
				caption: {
					path: `${folder}/${base.name}/caption.sav`,
					sha256: base.caption,
				},
				option: {
					path: `${folder}/${base.name}/option.sav`,
					sha256: base.option,
				},
			});

			const footingOf = (base: Base): Footing => ({
				base: {
					name: base.name,
					about: base.about,
					...(base.from === undefined ? {} : { from: base.from }),
				},
				files: filesOf(base),
				listedIn: `the row of base ${base.name}`,
			});

			const named = Effect.fnUntraced(function* (asked: string) {
				const tracked = own.bases.get(asked);
				if (tracked !== undefined) {
					return footingOf(tracked);
				}
				const here = yield* recorded;
				const row = here.get(asked);
				if (row !== undefined) {
					return yield* Effect.map(Effect.fromResult(row), footingOf);
				}
				return yield* UnknownName.make({
					kind: "base",
					asked,
					suggestions: nearest(asked, [
						...own.bases.keys(),
						...here.keys(),
						...own.starts.keys(),
					]),
				});
			});

			/** A save a run kept that the game wrote, with the option.sav the run started with. */
			const kept = Effect.fnUntraced(function* (ref: RunSave) {
				const refuse = (detail: string) =>
					StateRefused.make({ reason: "no-save", detail });
				const record = yield* runs.get(ref.run).pipe(
					Effect.catchTags({
						RunNotFound: () =>
							refuse(`No run ${ref.run} is kept on the game machine.`),
						RunUnreadable: (unreadable) => refuse(unreadable.message),
					})
				);
				const saves = record.saves ?? [];
				const written = Arr.filter(
					Arr.map(saves, (each, index) => ({ save: each, index })),
					(each) => each.save.kind !== "built"
				);
				const picked =
					ref.save === undefined
						? Arr.last(written)
						: Arr.findFirst(written, (each) => each.index === ref.save);
				if (Option.isNone(picked)) {
					return yield* refuse(
						written.length === 0
							? `Run ${ref.run} kept no save the game wrote.`
							: `Run ${ref.run} kept no save ${ref.save} the game wrote: its saves the game wrote are ${Arr.join(
									Arr.map(written, ({ index }) => String(index)),
									", "
								)}.`
					);
				}
				const option = Arr.findFirst(
					saves,
					(each) => each.kind === "built"
				).pipe(Option.flatMapNullishOr((built) => built.option));
				if (Option.isNone(option)) {
					return yield* refuse(
						`Run ${ref.run} kept no option.sav, so its saves cannot start a look.`
					);
				}
				const { save } = picked.value;
				const at = runs.folder(ref.run);
				const from: KeptFrom = { run: ref.run, save: picked.value.index };
				const footing: Footing = {
					base: { about: keptAbout(save, ref.run), from },
					files: {
						progress: {
							path: `${at}/${save.progress.path}`,
							sha256: save.progress.sha256,
						},
						caption: {
							path: `${at}/${save.caption.path}`,
							sha256: save.caption.sha256,
						},
						option: {
							path: `${at}/${option.value.path}`,
							sha256: option.value.sha256,
						},
					},
					listedIn: `the record of run ${ref.run}`,
				};
				return footing;
			});

			const footing = (base: StartState["base"]) =>
				typeof base === "string" ? named(base) : kept(base);

			const bytes = Effect.fn("Bases.bytes")(function* (
				at: Footing,
				file: FileName
			) {
				const { path, sha256: expected } = at.files[file];
				if (expected === "") {
					return yield* SaveUnreadable.make({
						path,
						detail: `${at.listedIn} gives no ${file}.sav`,
					});
				}
				const held = yield* fs
					.readFile(path)
					.pipe(
						Effect.mapError(() =>
							SaveUnreadable.make({ path, detail: "there is no such file" })
						)
					);
				if (sha256(held) !== expected) {
					return yield* SaveUnreadable.make({
						path,
						detail: `its SHA-256 is ${sha256(held)}, not the ${expected} ${at.listedIn} gives`,
					});
				}
				return held;
			});

			/** The three files a base is named from, and the run's save they were kept as. */
			const sourceFiles = Effect.fnUntraced(function* (
				source: NewBase["source"]
			) {
				if ("run" in source) {
					const at = yield* kept(source);
					return {
						progress: yield* bytes(at, "progress"),
						caption: yield* bytes(at, "caption"),
						option: yield* bytes(at, "option"),
						from: at.base.from,
					};
				}
				const option =
					source.option ??
					(yield* Effect.flatMap(Effect.orDie(named(newGame)), (at) =>
						bytes(at, "option")
					));
				return { ...source, option, from: undefined };
			});

			const keep = Effect.fn("Bases.name")(function* (request: NewBase) {
				const taken = NameTaken.make({ base: request.name });
				const here = yield* recorded;
				if (
					own.bases.has(request.name) ||
					own.starts.has(request.name) ||
					here.has(request.name)
				) {
					return yield* taken;
				}
				const files = yield* sourceFiles(request.source);
				const progress = yield* decode("progress.sav", files.progress);
				yield* decode("caption.sav", files.caption);
				const age = ageIn(progress);
				if (Option.isNone(age)) {
					return yield* SaveUnreadable.make({
						path: "progress.sav",
						detail:
							"it holds no Sequence_IsChildhood, so it is not a save of this game",
					});
				}
				const { from } = files;
				const base: Base = {
					name: request.name,
					hour: request.hour,
					age: age.value,
					progress: sha256(files.progress),
					caption: sha256(files.caption),
					option: sha256(files.option),
					about: request.about,
					...(from === undefined ? {} : { from }),
				};
				const row = yield* Effect.orDie(Schema.encodeEffect(RowJson)(base));
				// The base is written whole in a draft folder beside the bases and renamed into place: a reader sees all of it
				// or none, a naming cut short leaves only its draft, which is removed, and of two namings of one name only one
				// rename lands.
				const draft = yield* Effect.orDie(
					fs.makeTempDirectoryScoped({ directory: home, prefix: "base-draft-" })
				);
				for (const file of ["progress", "caption", "option"] as const) {
					yield* Effect.orDie(
						fs.writeFile(`${draft}/${file}.sav`, files[file])
					);
				}
				yield* Effect.orDie(fs.writeFileString(`${draft}/row.json`, row));
				yield* fs
					.rename(draft, `${folder}/${request.name}`)
					.pipe(Effect.mapError(() => taken));
				return base;
			}, Effect.scoped);

			/** Where Link stands in a base, as its progress.sav holds it; none when its files are not here. */
			const standOf = (base: Base) =>
				bytes(footingOf(base), "progress").pipe(
					Effect.flatMap((file) => Effect.orDie(decode("progress.sav", file))),
					Effect.flatMap((save) =>
						Effect.orDie(
							Schema.decodeUnknownEffect(StandValues)(
								Rec.map(standNames, (value) =>
									Option.getOrUndefined(read(save, valueHash(value)))
								)
							)
						)
					),
					Effect.map((held): typeof Location.Type => ({
						scene: held.scene,
						...(held.dungeon === "" ? {} : { room: held.room }),
						position: held.position,
						facing: degrees(held.radians),
					})),
					Effect.option
				);

			const catalog = Effect.gen(function* () {
				const every = yield* all;
				const bases = yield* Effect.forEach(
					every,
					Effect.fnUntraced(function* (base) {
						const stands = yield* standOf(base);
						return {
							...base,
							recorded: !own.bases.has(base.name),
							...Option.match(stands, {
								onNone: () => ({}),
								onSome: (at) => ({ stands: at }),
							}),
						};
					}),
					{ concurrency: 4 }
				);
				const starts = Arr.flatMap([...own.starts.values()], (start) =>
					Arr.map(
						Arr.filter(bases, (base) => base.name === start.base),
						(base) => {
							const stands = start.location ?? base.stands;
							return {
								start,
								age: start.age ?? base.age,
								hour: base.hour,
								...(stands === undefined ? {} : { stands }),
							};
						}
					)
				);
				const listed: Catalog = { bases, starts };
				return listed;
			});

			return Bases.of({
				all,
				footing,
				bytes,
				name: keep,
				catalog,
			});
		})
	);
}
