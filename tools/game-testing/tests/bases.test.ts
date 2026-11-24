import { setDefaultTimeout } from "bun:test";
import { BunFileSystem, BunServices } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	ConfigProvider,
	Deferred,
	Effect,
	Exit,
	Fiber,
	FileSystem,
	Layer,
	Schedule,
	Schema,
	String as Str,
} from "effect";
import { UserDirectories } from "effect/persistence";
import { ChildProcess, ChildProcessSpawner } from "effect/process";
import { Base } from "../src/contract/bases.ts";
import { StartState } from "../src/contract/state.ts";
import { newRunId, RunId, SessionId } from "../src/contract/values.ts";
import { GameData } from "../src/data/game-data.ts";
import type { RunningGame } from "../src/game.ts";
import { Game } from "../src/game.ts";
import { bases as basesOnMachine } from "../src/host/host.ts";
import { Runs } from "../src/host/runs.ts";
import { ran } from "../src/process.ts";
import { Bases } from "../src/save/bases.ts";
import { StartStates } from "../src/save/state.ts";
import { baseLine, OwnTables, searchNames } from "../src/save/tables.ts";
import type { ScriptError } from "../src/script.ts";
import { packageRoot } from "../src/scripts/runner.ts";
import type { FakeGame } from "./fake-game.ts";
import { fakeSave } from "./fake-game.ts";
import type { FakeLichii } from "./fake-lichii.ts";
import { fakeMachine } from "./fake-machine.ts";

// Every game boots through the title's three menus, one press a second, as the game's own do.
setDefaultTimeout(60_000);

/** `IsTreasureBoxOpen.5005` by its hash, so the test type-checks whatever names a machine generated. */
const chest = "0x1d2a8fff";
const openChest = (lichii: FakeLichii) => {
	Effect.runSync(lichii.put("IsTreasureBoxOpen.5005", true));
};

/** Runs `body` in a game started in `state`, to its end, and gives its run's record. */
const look = (
	state: ConstructorParameters<typeof StartState>[0],
	body: (game: RunningGame) => Effect.Effect<unknown, ScriptError>
) =>
	Effect.gen(function* () {
		const games = yield* Game;
		const runs = yield* Runs;
		const run = yield* newRunId;
		yield* Effect.scoped(
			Effect.gen(function* () {
				const game = yield* games.start({
					run,
					owner: "bases.test.ts",
					state: StartState.make(state),
				});
				return yield* games.finish(game, yield* Effect.exit(body(game)));
			})
		);
		return yield* runs.get(run).pipe(
			Effect.repeat({
				schedule: Schedule.spaced("20 millis"),
				until: (kept) => kept.outcome._tag !== "running",
			}),
			Effect.timeout("5 seconds")
		);
	});

describe("the bases", () => {
	it.live(
		"a run's last save starts a look, and a name makes it a base found by search and checked by its hashes",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const machine = yield* fakeMachine({
					onA: openChest,
					autosaveOnA: { "IsTreasureBoxOpen.5005": true },
				});
				yield* Effect.gen(function* () {
					const bases = yield* Bases;
					const opened = yield* look({ base: "test-base" }, (game) =>
						Effect.andThen(game.press("A"), game.check(chest, true))
					);
					expect(opened.outcome._tag).toBe("passed");

					const again = yield* look({ base: { run: opened.id } }, (game) =>
						game.still(chest, true)
					);
					expect(again.outcome._tag).toBe("passed");
					expect(again.base).toMatchObject({
						from: { run: opened.id, save: 1 },
					});

					const named = yield* bases.name({
						name: "chest-open",
						about: "Before the chest, opened.",
						hour: "day",
						source: { run: opened.id },
					});
					expect(named).toMatchObject({
						age: "child",
						from: { run: opened.id, save: 1 },
					});
					const byName = yield* look({ base: "chest-open" }, (game) =>
						game.still(chest, true)
					);
					expect(byName.outcome._tag).toBe("passed");
					expect(byName.base).toMatchObject({
						name: "chest-open",
						from: { run: opened.id, save: 1 },
					});
					const found = yield* searchNames(
						yield* GameData,
						yield* OwnTables,
						{ spots: [], bases: yield* bases.all },
						"chest-open",
						{ limit: 5, kind: "base" }
					);
					expect(Arr.map(found, (match) => match.name)).toEqual(["chest-open"]);

					yield* fs.writeFile(
						`${machine.home}/data/game-testing/bases/chest-open/progress.sav`,
						new Uint8Array([1, 2, 3])
					);
					const states = yield* StartStates;
					const refused = yield* Effect.flip(
						states.plan(StartState.make({ base: "chest-open" }), "none")
					);
					expect(refused._tag).toBe("SaveUnreadable");
				}).pipe(Effect.provide(machine.layer));
			}).pipe(Effect.provide(BunServices.layer))
	);

	it.live(
		"a save from a folder becomes a base, and the list shows each base with its age, hour and where Link stands",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const machine = yield* fakeMachine();
				yield* Effect.gen(function* () {
					const bases = yield* Bases;
					const folder = `${machine.home}/data/game-testing/bases/test-base`;
					yield* bases.name({
						name: "from-play",
						about: "A save from play.",
						hour: "night",
						source: {
							progress: yield* fs.readFile(`${folder}/progress.sav`),
							caption: yield* fs.readFile(`${folder}/caption.sav`),
						},
					});
					const listed = yield* bases.catalog;
					expect(
						Arr.map(listed.bases, ({ name, age, hour, recorded, stands }) => ({
							name,
							age,
							hour,
							recorded,
							stands,
						}))
					).toEqual([
						{
							name: "test-base",
							age: "child",
							hour: "day",
							recorded: false,
							stands: { scene: "KokiriForest", position: [1, 2, 3], facing: 0 },
						},
						{
							name: "new-game-autosave",
							age: "child",
							hour: "day",
							recorded: false,
							stands: { scene: "KokiriForest", position: [1, 2, 3], facing: 0 },
						},
						{
							name: "from-play",
							age: "child",
							hour: "night",
							recorded: true,
							stands: { scene: "KokiriForest", position: [1, 2, 3], facing: 0 },
						},
					]);
					const from = yield* fs.readFile(
						`${machine.home}/data/game-testing/bases/from-play/option.sav`
					);
					const newGame = yield* fs.readFile(
						`${machine.home}/data/game-testing/bases/new-game-autosave/option.sav`
					);
					expect(from).toEqual(newGame);
				}).pipe(Effect.provide(machine.layer));
			}).pipe(Effect.provide(BunServices.layer))
	);
});

/** A save of a game with only what a base's catalog line and its age read. */
const tiny: FakeGame = {
	key: "5eed",
	values: {
		Sequence_IsChildhood: { type: "Bool", value: true },
		Sequence_CurrentBanc: { type: "String64", value: "KokiriForest" },
		DungeonRoom: { type: "Int", value: -1 },
		"0x4a6bdb7d": { type: "String32", value: "" },
		"PlayerStatus.SavePos": { type: "Vector3", value: [1, 2, 3] },
		"PlayerStatus.SavePosRadY": { type: "Float", value: 0 },
		"CaptionData.IsAutoSave": { type: "Bool", value: true, file: 2 },
	},
};

const sha256 = (bytes: Uint8Array) =>
	new Bun.CryptoHasher("sha256").update(bytes).digest("hex");

/**
 * The bases of a machine with no game: its one tracked base, `new-game-autosave`, whose files are in place, and the
 * files of a save to name. `fs` is the file system the bases use.
 */
const machineBases = (fs: Layer.Layer<FileSystem.FileSystem>) =>
	Effect.gen(function* () {
		const files = yield* FileSystem.FileSystem;
		const home = yield* files.makeTempDirectoryScoped();
		const save = {
			progress: yield* fakeSave(tiny, 0),
			caption: yield* fakeSave(tiny, 2),
			option: new Uint8Array([1, 2, 3]),
		};
		const folder = `${home}/data/game-testing/bases/new-game-autosave`;
		yield* files.makeDirectory(folder, { recursive: true });
		for (const part of ["progress", "caption", "option"] as const) {
			yield* files.writeFile(`${folder}/${part}.sav`, save[part]);
		}
		const own = Layer.succeed(
			OwnTables,
			OwnTables.of({
				spots: [],
				chests: [],
				bases: new Map([
					[
						"new-game-autosave",
						{
							name: "new-game-autosave",
							hour: "day",
							age: "child",
							progress: sha256(save.progress),
							caption: sha256(save.caption),
							option: sha256(save.option),
							about: "the tiny game's start",
						},
					],
				]),
				tiers: [],
				checks: new Map(),
			})
		);
		const layer = basesOnMachine.pipe(
			Layer.provide([own, UserDirectories.layer]),
			Layer.provide(fs),
			Layer.provide(
				ConfigProvider.layer(
					ConfigProvider.fromUnknown({
						HOME: home,
						XDG_DATA_HOME: `${home}/data`,
						XDG_STATE_HOME: `${home}/state`,
					})
				)
			)
		);
		return { home, save, layer };
	}).pipe(Effect.provide(BunFileSystem.layer));

/** The real file system, where a row.json is written in two halves with a wait at `gate` between them. */
const halting = (
	reached: Deferred.Deferred<void>,
	gate: Deferred.Deferred<void>
) =>
	Layer.effect(
		FileSystem.FileSystem,
		Effect.map(FileSystem.FileSystem, (real) =>
			FileSystem.FileSystem.of({
				...real,
				writeFileString: (path, text, options) =>
					Str.endsWith("row.json")(path)
						? Effect.gen(function* () {
								yield* real.writeFileString(
									path,
									Str.slice(0, 20)(text),
									options
								);
								yield* Deferred.succeed(reached, undefined);
								yield* Deferred.await(gate);
								yield* real.writeFileString(path, text, options);
							})
						: real.writeFileString(path, text, options),
			})
		)
	).pipe(Layer.provide(BunFileSystem.layer));

const fromPlay = (save: {
	readonly progress: Uint8Array;
	readonly caption: Uint8Array;
}) => ({
	name: "from-play",
	about: "A save from play.",
	hour: "night" as const,
	source: { progress: save.progress, caption: save.caption },
});

describe("a base on a machine", () => {
	it.live(
		"a reader beside a naming sees the base whole or not at all, and a naming cut short leaves the name free",
		() =>
			Effect.gen(function* () {
				const reached = yield* Deferred.make<void>();
				const gate = yield* Deferred.make<void>();
				const machine = yield* machineBases(halting(reached, gate));
				yield* Effect.gen(function* () {
					const bases = yield* Bases;
					const naming = yield* Effect.forkChild(
						bases.name(fromPlay(machine.save))
					);
					yield* Deferred.await(reached);
					const listed = yield* bases.catalog;
					expect(Arr.map(listed.bases, (base) => base.name)).toEqual([
						"new-game-autosave",
					]);
					expect((yield* Effect.flip(bases.footing("from-play")))._tag).toBe(
						"UnknownName"
					);
					yield* Fiber.interrupt(naming);
					yield* Deferred.succeed(gate, undefined);
					const named = yield* bases.name(fromPlay(machine.save));
					expect(named.name).toBe("from-play");
					expect((yield* bases.footing("from-play")).base).toMatchObject({
						name: "from-play",
					});
				}).pipe(Effect.provide(machine.layer));
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer))
	);

	it.live(
		"a recorded row that cannot be read stops its own base with a sentence naming its file, and the others work",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const machine = yield* machineBases(BunFileSystem.layer);
				const broken = `${machine.home}/data/game-testing/bases/broken`;
				yield* fs.makeDirectory(broken, { recursive: true });
				yield* fs.writeFileString(`${broken}/row.json`, "{");
				yield* Effect.gen(function* () {
					const bases = yield* Bases;
					const refused = yield* Effect.flip(bases.footing("broken"));
					expect(refused.message).toContain(`${broken}/row.json`);
					yield* bases.name(fromPlay(machine.save));
					expect((yield* bases.footing("from-play")).base).toMatchObject({
						name: "from-play",
					});
					expect(
						Arr.map((yield* bases.catalog).bases, (base) => base.name)
					).toEqual(["new-game-autosave", "from-play"]);
				}).pipe(Effect.provide(machine.layer));
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer))
	);

	it.live(
		"{ run, save } takes that save of the run, and refuses a save it lacks, save 0, a run not kept and a record this build cannot read",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const machine = yield* machineBases(BunFileSystem.layer);
				yield* Effect.gen(function* () {
					const bases = yield* Bases;
					const runs = yield* Runs;
					const run = yield* newRunId;
					yield* Effect.scoped(
						Effect.gen(function* () {
							const recorder = yield* runs.begin({
								id: run,
								session: SessionId.make("a-session"),
								machine: "a-machine",
								emulator: { program: "lichii" },
								game: "5eed",
								state: StartState.make({ base: "new-game-autosave" }),
							});
							const { progress, caption, option } = machine.save;
							yield* recorder.save({
								kind: "built",
								slot: "slot_00",
								progress,
								caption,
								option,
							});
							yield* recorder.save({
								kind: "autosave",
								slot: "slot_01",
								progress,
								caption,
							});
							yield* recorder.save({
								kind: "menu",
								slot: "slot_00",
								progress,
								caption,
							});
							yield* recorder.end({ _tag: "unchecked" });
						})
					);
					const last = yield* bases.footing({ run });
					expect(last.base.from).toEqual({ run, save: 2 });
					const first = yield* bases.footing({ run, save: 1 });
					expect(first.files.progress.path).toBe(
						`${runs.folder(run)}/save-1-progress.sav`
					);
					expect(
						(yield* Effect.flip(bases.footing({ run, save: 3 }))).message
					).toContain("its saves the game wrote are 1, 2");
					expect(
						Exit.isFailure(
							Schema.decodeExit(StartState)({ base: { run, save: 0 } })
						)
					).toBe(true);
					const missing = RunId.make("20261123-000000-none");
					expect(
						(yield* Effect.flip(bases.footing({ run: missing }))).message
					).toContain(`No run ${missing} is kept on the game machine`);
					const unreadable = RunId.make("20261123-000000-oldy");
					yield* fs.makeDirectory(runs.folder(unreadable), { recursive: true });
					yield* fs.writeFileString(
						`${runs.folder(unreadable)}/record.json`,
						"{}"
					);
					expect(
						(yield* Effect.flip(bases.footing({ run: unreadable }))).message
					).toContain(`Run ${unreadable}'s record`);
				}).pipe(Effect.provide(machine.layer));
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer))
	);
});

/**
 * A copy of this package in a folder of its own, beside a link to the repository's remake tables, so a command run from
 * it writes the copy's tables, never this package's. `run` runs its command line with `home` as the user's home.
 */
const packageCopy = Effect.gen(function* () {
	const fs = yield* FileSystem.FileSystem;
	const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
	const root = yield* fs.makeTempDirectoryScoped();
	const home = yield* fs.makeTempDirectoryScoped();
	const copy = `${root}/tools/game-testing`;
	yield* fs.makeDirectory(copy, { recursive: true });
	for (const part of ["src", "tables", "package.json"]) {
		yield* fs.copy(`${packageRoot}/${part}`, `${copy}/${part}`);
	}
	yield* fs.symlink(`${packageRoot}/node_modules`, `${copy}/node_modules`);
	yield* fs.symlink(`${packageRoot}/../../remake`, `${root}/remake`);
	const run = (args: readonly string[]) =>
		ran(
			spawner,
			ChildProcess.make(process.execPath, [`${copy}/src/main.ts`, ...args], {
				extendEnv: true,
				env: {
					HOME: home,
					XDG_DATA_HOME: `${home}/data`,
					XDG_STATE_HOME: `${home}/state`,
					XDG_CACHE_HOME: `${home}/cache`,
					XDG_RUNTIME_DIR: `${home}/run`,
				},
			})
		);
	return { copy, home, run };
});

describe("the tables in git", () => {
	it.live(
		"move-bases adds to the package's table each recorded row it lacks, leaves a row it has, and prints the table's path",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { copy, home, run } = yield* packageCopy;
				const table = `${copy}/tables/bases.tsv`;
				const before = yield* fs.readFileString(table);
				const row = (base: Base) =>
					Effect.gen(function* () {
						const folder = `${home}/data/game-testing/bases/${base.name}`;
						yield* fs.makeDirectory(folder, { recursive: true });
						yield* fs.writeFileString(
							`${folder}/row.json`,
							yield* Schema.encodeEffect(Schema.fromJsonString(Base))(base)
						);
					});
				const named: Base = {
					name: "kept-one",
					hour: "day",
					age: "child",
					progress: "aa",
					caption: "bb",
					option: "cc",
					about: "A base named on this machine.",
					from: { run: RunId.make("20261123-000000-abcd"), save: 1 },
				};
				yield* row(named);
				yield* row({ ...named, name: "early-child", about: "Not git's row." });
				const moved = yield* run(["move-bases"]);
				expect(moved.code).toBe(0);
				expect(moved.stdout).toContain("kept-one  moved");
				expect(moved.stdout).toContain(table);
				expect(yield* fs.readFileString(table)).toBe(
					`${before}${baseLine(named)}\n`
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer))
	);

	it.live(
		"a wrong row of tables/bases.tsv stops the tool with a sentence naming the file and the line",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { copy, run } = yield* packageCopy;
				const table = `${copy}/tables/bases.tsv`;
				const rows = yield* fs.readFileString(table);
				yield* fs.writeFileString(table, `${rows}broken-row\tnoon\n`);
				const line =
					Arr.filter(Str.split(rows, "\n"), Str.isNonEmpty).length + 1;
				const base = yield* run(["bases"]);
				expect(base.code).not.toBe(0);
				expect(base.stdout).toContain(`${table} line ${line}:`);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer))
	);
});
