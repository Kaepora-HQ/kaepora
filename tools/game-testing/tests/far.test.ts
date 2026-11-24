import { BunServices } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	Effect,
	Exit,
	FileSystem,
	Layer,
	Option,
	Redacted,
	Schedule,
	Schema,
	String as Str,
	Stream,
} from "effect";
import { Command } from "effect/cli";
import { ChildProcess, ChildProcessSpawner } from "effect/process";
import { Rpc, RpcClient, RpcGroup } from "effect/rpc";
import { TestConsole } from "effect/testing";
import { cli } from "../src/cli.ts";
import { RunRecord } from "../src/contract/record.ts";
import { StartState } from "../src/contract/state.ts";
import { newRunId, SessionId } from "../src/contract/values.ts";
import { connection } from "../src/far.ts";
import { Runs } from "../src/host/runs.ts";
import { packageRoot } from "../src/scripts/runner.ts";
import { modList, startList } from "./fake-game.ts";
import type { FakeLichii } from "./fake-lichii.ts";
import type { Machine } from "./fake-machine.ts";
import {
	fakeCaller,
	game as fakeGame,
	fakeMachine,
	served,
	token,
} from "./fake-machine.ts";

const rupeesPlanned = /^PlayerStatus\.CurrentRupee = 50 {4}was 0/u;
const baseFound = /^base {2}test-base {2}/u;

const openChest = (lichii: FakeLichii) => {
	Effect.runSync(lichii.put("IsTreasureBoxOpen.5005", true));
};
const lasting = { timeout: 60_000 };

/** A look as an agent writes it in its own scratch, outside the repository, importing the package by its path. */
const look = (options: {
	readonly declares?: string;
	readonly read?: string;
	readonly state?: string;
}) =>
	`import { Effect, script } from "${packageRoot}/src/script.ts";

export default script({
	about: "The chest Link stands before opens to A.",
	state: ${options.state ?? '{ base: "test-base" }'},
	${options.declares ?? ""}
	run: (game) =>
		Effect.gen(function* () {
			yield* game.press("A");
			${options.read === undefined ? "" : `yield* game.read("${options.read}");`}
			const opened = yield* game.check("IsTreasureBoxOpen.5005", true);
			return { opened, shot: yield* game.screenshot("after-chest") };
		}),
});
`;

/** A far machine with `serve`, and a caller with no game whose commands reach it: what each command printed, and how it ended. */
const farAndCaller = (options?: {
	readonly onA?: (lichii: FakeLichii) => void;
	readonly autosaveOnA?: Readonly<Record<string, boolean>>;
}) =>
	Effect.gen(function* () {
		const fs = yield* FileSystem.FileSystem;
		const far = yield* fakeMachine(options);
		const caller = yield* fakeCaller(yield* served(far));
		const scratch = `${caller.home}/scratch`;
		yield* fs.makeDirectory(scratch);
		const command = (args: readonly string[]) =>
			Effect.gen(function* () {
				const exit = yield* Effect.exit(
					Command.runWith(cli, { version: "test" })(args)
				);
				const lines = Arr.map(yield* TestConsole.logLines, String);
				const errors = Arr.map(yield* TestConsole.errorLines, String);
				return { succeeded: Exit.isSuccess(exit), lines, errors };
			}).pipe(Effect.provide([caller.platform, TestConsole.layer]));
		return { far, caller, scratch, command };
	});

const creates = (machine: Machine) =>
	Effect.map(
		machine.podman.calls,
		(calls) => Arr.filter(calls, (args) => args[0] === "create").length
	);

describe("a caller on another machine, with no game", () => {
	it.live(
		"a far run with a mod puts the mod's files in the place",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, scratch, command } = yield* farAndCaller({
					onA: openChest,
				});
				yield* fs.makeDirectory(`${scratch}/Test Mod/romfs/Banc`, {
					recursive: true,
				});
				yield* fs.writeFile(
					`${scratch}/Test Mod/romfs/Banc/Field.bcett.byml.zs`,
					new Uint8Array([1, 2, 3])
				);
				yield* fs.writeFileString(
					`${scratch}/look.ts`,
					look({ declares: 'mod: { folder: "./Test Mod" },' })
				);
				const { succeeded, lines } = yield* command([
					"run",
					`${scratch}/look.ts`,
				]);
				expect(lines[0]).toStartWith("passed  ");
				expect(succeeded).toBe(true);
				const [started] = yield* far.starts;
				expect(started?.mods).toEqual({
					"Test Mod/romfs/Banc/Field.bcett.byml.zs": new Uint8Array([1, 2, 3]),
				});
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a mod file whose name climbs out of the mod's folder is refused, and nothing is written or started",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const far = yield* fakeMachine();
				const url = yield* served(far);
				// A caller that skips the contract's checks, as a hostile one would.
				const Loose = RpcGroup.make(
					Rpc.make("Open", {
						payload: Schema.Unknown,
						success: Schema.Unknown,
						error: Schema.Unknown,
						stream: true,
					})
				);
				const connected = yield* Layer.build(
					connection(url, Option.some(Redacted.make(token)))
				);
				const client = yield* RpcClient.make(Loose).pipe(
					Effect.provideContext(connected)
				);
				const open = (mod: unknown) =>
					Effect.flatMap(newRunId, (run) =>
						client
							.Open({
								run,
								state: { base: "test-base" },
								owner: "far.test.ts",
								waitForPlace: "10 seconds",
								mod,
							})
							.pipe(Stream.runDrain, Effect.exit)
					);
				const bytes = "AQID";
				const climbed = yield* open({
					name: "Climber",
					files: [{ path: "romfs/../../escape.txt", bytes }],
				});
				const named = yield* open({
					name: "..",
					files: [{ path: "escape.txt", bytes }],
				});
				expect(Exit.isFailure(climbed)).toBe(true);
				expect(Exit.isFailure(named)).toBe(true);
				const written = yield* fs.readDirectory(far.home, { recursive: true });
				expect(Arr.filter(written, Str.endsWith("escape.txt"))).toEqual([]);
				expect(yield* creates(far)).toBe(0);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a far run leaves its record and pictures on the caller's machine, and says where",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, caller, command } = yield* farAndCaller({
					onA: openChest,
				});
				const { lines } = yield* command([
					"run",
					`${import.meta.dir}/scripts/opens-chest.ts`,
				]);
				expect(lines[0]).toStartWith("passed  ");
				const run = Option.getOrThrow(
					Arr.findFirst(Str.split(lines[0] ?? "", "  "), Str.startsWith("20"))
				);
				const folder = `${caller.home}/state/game-testing/runs/${run}`;
				expect(lines).toContain(`    record and pictures: ${folder}`);
				const record = yield* Schema.decodeEffect(
					Schema.fromJsonString(RunRecord)
				)(yield* fs.readFileString(`${folder}/record.json`));
				expect(record.outcome).toEqual({ _tag: "passed" });
				expect(yield* fs.readFile(`${folder}/picture-1.png`)).toEqual(
					yield* fs.readFile(
						`${far.home}/state/game-testing/runs/${run}/picture-1.png`
					)
				);
				expect(yield* creates(far)).toBe(1);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a far run's kept saves come to the caller's machine with its record",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, caller, command } = yield* farAndCaller({
					onA: openChest,
					autosaveOnA: { "IsTreasureBoxOpen.5005": true },
				});
				const { lines } = yield* command([
					"run",
					`${import.meta.dir}/scripts/opens-chest.ts`,
				]);
				const run = Option.getOrThrow(
					Arr.findFirst(Str.split(lines[0] ?? "", "  "), Str.startsWith("20"))
				);
				const here = `${caller.home}/state/game-testing/runs/${run}`;
				const record = yield* Schema.decodeEffect(
					Schema.fromJsonString(RunRecord)
				)(yield* fs.readFileString(`${here}/record.json`));
				const files = Arr.flatMap(record.saves ?? [], (save) => [
					save.progress.path,
					save.caption.path,
				]);
				expect(files).toHaveLength(4);
				for (const file of files) {
					expect(yield* fs.readFile(`${here}/${file}`)).toEqual(
						yield* fs.readFile(
							`${far.home}/state/game-testing/runs/${run}/${file}`
						)
					);
				}
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"plan, search, places and names answer through the far machine",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { caller, scratch, command } = yield* farAndCaller();
				yield* fs.writeFileString(
					`${scratch}/state.json`,
					'{ "base": "test-base", "set": { "PlayerStatus.CurrentRupee": 50 } }'
				);
				const planned = yield* command(["plan", `${scratch}/state.json`]);
				expect(planned.lines[0]).toStartWith("From test-base: ");
				expect(planned.lines).toContainEqual(
					expect.stringMatching(rupeesPlanned)
				);
				const found = yield* command(["search", "test-ba"]);
				expect(found.lines).toContainEqual(expect.stringMatching(baseFound));
				const listed = yield* command(["places"]);
				expect(listed.lines).toEqual(["1  free"]);
				const named = yield* command(["names"]);
				expect(named.succeeded).toBe(true);
				expect(
					yield* fs.readFileString(
						`${caller.home}/cache/game-testing/names.d.ts`
					)
				).toContain('"IsTreasureBoxOpen.5005": boolean;');
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a run killed outright takes its script with it, and the far game stops",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
				const far = yield* fakeMachine();
				const caller = yield* fakeCaller(yield* served(far));
				yield* fs.writeFileString(
					`${caller.home}/holds.ts`,
					`import { Effect, script } from "${packageRoot}/src/script.ts";

export default script({
	about: "Holds its game until its run is stopped.",
	state: { base: "test-base" },
	run: () => Effect.never,
});
`
				);
				const run = yield* spawner.spawn(
					ChildProcess.make(
						process.execPath,
						[`${packageRoot}/src/main.ts`, "run", `${caller.home}/holds.ts`],
						{
							extendEnv: true,
							env: {
								HOME: caller.home,
								XDG_CACHE_HOME: `${caller.home}/cache`,
								XDG_STATE_HOME: `${caller.home}/state`,
								XDG_RUNTIME_DIR: `${caller.home}/run`,
								GAME_TESTING_URL: yield* served(far),
								GAME_TESTING_TOKEN: token,
							},
						}
					)
				);
				const held = (count: number) =>
					far.podman.containers.pipe(
						Effect.repeat({
							schedule: Schedule.spaced("50 millis"),
							until: (containers) => containers.size === count,
						}),
						Effect.timeout("20 seconds"),
						Effect.option
					);
				expect(Option.isSome(yield* held(1))).toBe(true);
				yield* run.kill({ killSignal: "SIGKILL" });
				expect(Option.isSome(yield* held(0))).toBe(true);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a far run's type check catches a misspelled name before any game starts",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, scratch, command } = yield* farAndCaller();
				yield* fs.writeFileString(
					`${scratch}/look.ts`,
					look({ read: "IsTreasureBoxOpen.5050" })
				);
				const { succeeded, lines } = yield* command([
					"run",
					`${scratch}/look.ts`,
				]);
				expect(succeeded).toBe(false);
				expect(lines[0]).toStartWith("broken  ");
				expect(Arr.join(lines, "\n")).toContain("IsTreasureBoxOpen.5050");
				expect(yield* creates(far)).toBe(0);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);
});

describe("what a far run promises its caller", () => {
	it.live(
		"a mod the size table refuses is refused through the start, with what is too large, and no place is taken",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, scratch, command } = yield* farAndCaller();
				yield* fs.makeDirectory(`${scratch}/Too Large Mod/romfs/Fake`, {
					recursive: true,
				});
				yield* fs.writeFile(
					`${scratch}/Too Large Mod/romfs/Fake/Big.byml.zs`,
					new Uint8Array(4096)
				);
				yield* fs.writeFileString(
					`${scratch}/look.ts`,
					look({ declares: 'mod: { folder: "./Too Large Mod" },' })
				);
				const { succeeded, lines } = yield* command([
					"run",
					`${scratch}/look.ts`,
				]);
				expect(succeeded).toBe(false);
				expect(Arr.join(lines, "\n")).toContain(
					"romfs/Fake/Big.byml.zs needs 4096 bytes in the size table, which gives it 2048."
				);
				expect(yield* creates(far)).toBe(0);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a mod file whose size the game's table cannot measure is named in the record, and the start goes on",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { caller, scratch, command } = yield* farAndCaller({
					onA: openChest,
				});
				const schedule =
					"Component/AIScheduleParam/Npc_Fake.game__component__AIScheduleParam.bgyml";
				yield* fs.makeDirectory(
					`${scratch}/Unmeasured Mod/romfs/Component/AIScheduleParam`,
					{ recursive: true }
				);
				yield* fs.writeFile(
					`${scratch}/Unmeasured Mod/romfs/${schedule}`,
					new Uint8Array(4096)
				);
				yield* fs.writeFileString(
					`${scratch}/look.ts`,
					look({ declares: 'mod: { folder: "./Unmeasured Mod" },' })
				);
				const { lines } = yield* command(["run", `${scratch}/look.ts`]);
				expect(lines[0]).toStartWith("passed  ");
				const run = Arr.lastNonEmpty(Str.split(lines[0] ?? "", "  "));
				const record = yield* Schema.decodeEffect(
					Schema.fromJsonString(RunRecord)
				)(
					yield* fs.readFileString(
						`${caller.home}/state/game-testing/runs/${run}/record.json`
					)
				);
				expect(record.unmeasured).toEqual([
					{
						path: schedule,
						reason:
							"the game's own size table gives files of its kind no one overhead, so what a larger one needs is not known",
					},
				]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a mod too large to send is refused before anything is sent, with its size and the limit",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, scratch, command } = yield* farAndCaller();
				yield* fs.makeDirectory(`${scratch}/Huge Mod/romfs`, {
					recursive: true,
				});
				yield* fs.writeFile(
					`${scratch}/Huge Mod/romfs/Huge.zs`,
					new Uint8Array(49 * 1024 * 1024)
				);
				yield* fs.writeFileString(
					`${scratch}/look.ts`,
					look({ declares: 'mod: { folder: "./Huge Mod" },' })
				);
				const { succeeded, lines } = yield* command([
					"run",
					`${scratch}/look.ts`,
				]);
				expect(succeeded).toBe(false);
				expect(Arr.join(lines, "\n")).toContain(
					"is 65.3 MiB as the start sends it, over the 63 MiB a start can carry"
				);
				expect(yield* creates(far)).toBe(0);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a far run whose copy fails says so under its line, with the run's name there, and exits as its outcome",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, scratch, command } = yield* farAndCaller({
					onA: openChest,
				});
				// The body takes its own picture away on the game's machine, as a copy that fails meets it.
				yield* fs.writeFileString(
					`${scratch}/look.ts`,
					`import { Effect, script } from "${packageRoot}/src/script.ts";

export default script({
	about: "The chest opens, and its picture is gone before the copy.",
	state: { base: "test-base" },
	run: (game) =>
		Effect.gen(function* () {
			yield* game.press("A");
			const opened = yield* game.check("IsTreasureBoxOpen.5005", true);
			const shot = yield* game.screenshot("after-chest");
			yield* Effect.promise(() =>
				Bun.file(\`${far.home}/state/game-testing/runs/\${game.run}/picture-1.png\`).delete()
			);
			return { opened, shot };
		}),
});
`
				);
				const { succeeded, lines } = yield* command([
					"run",
					`${scratch}/look.ts`,
				]);
				expect(lines[0]).toStartWith("passed  ");
				expect(succeeded).toBe(true);
				const run = Option.getOrThrow(
					Arr.findFirst(Str.split(lines[0] ?? "", "  "), Str.startsWith("20"))
				);
				expect(lines[1]).toBe(
					`    The record, pictures and saves were not copied here: the game machine has no file picture-1.png. The game machine keeps them as run ${run}, which game-testing runs lists there.`
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a copy refuses a file name that is no picture's or save's, with a sentence",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const refused = yield* Effect.gen(function* () {
					const runs = yield* Runs;
					const run = yield* newRunId;
					return yield* Effect.flip(
						runs.copy(
							RunRecord.make({
								id: run,
								session: SessionId.make("far"),
								machine: "far",
								emulator: { program: "lichii" },
								game: "5eed",
								state: StartState.make({ base: "test-base" }),
								steps: [],
								outcome: { _tag: "passed" },
								startedAt: Temporal.Instant.fromEpochMilliseconds(0),
							}),
							[{ path: "../escape.png", bytes: new Uint8Array([1]) }]
						)
					);
				}).pipe(
					Effect.provide(Runs.layer.pipe(Layer.provide(machine.platform)))
				);
				expect(refused.message).toContain(
					"the game machine named a file ../escape.png, which is no picture's or save's name"
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a run stopped at its limit leaves a copy that says how it ended",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { caller, command } = yield* farAndCaller();
				const { lines } = yield* command([
					"run",
					`${import.meta.dir}/scripts/waits-forever.ts`,
				]);
				expect(lines[0]).toStartWith("broken  ");
				const run = Option.getOrThrow(
					Arr.findFirst(Str.split(lines[0] ?? "", "  "), Str.startsWith("20"))
				);
				const record = yield* Schema.decodeEffect(
					Schema.fromJsonString(RunRecord)
				)(
					yield* fs.readFileString(
						`${caller.home}/state/game-testing/runs/${run}/record.json`
					)
				);
				expect(record.outcome._tag).toBe("broken");
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"runs lists the records it can read and says how many it could not",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const machine = yield* fakeMachine();
				const old = `${machine.home}/state/game-testing/runs/20261121-000000-abcd`;
				yield* fs.makeDirectory(old, { recursive: true });
				yield* fs.writeFileString(
					`${old}/record.json`,
					'{ "id": "20261121-000000-abcd", "mod": { "source": { "folder": "/x" } } }'
				);
				const lines = yield* Effect.gen(function* () {
					yield* Command.runWith(cli, { version: "test" })(["runs"]);
					return Arr.map(yield* TestConsole.logLines, String);
				}).pipe(Effect.provide([machine.platform, TestConsole.layer]));
				expect(lines).toContain(
					"1 more records could not be read: a record written by another version of the tool, or one being written."
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);
});

describe("a place's game", () => {
	it.live(
		"runs with instant text and the machine's mod that skips, unless the script asks for the game itself",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, scratch, command } = yield* farAndCaller({
					onA: openChest,
				});
				yield* fs.writeFileString(`${scratch}/player.ts`, look({}));
				yield* fs.writeFileString(
					`${scratch}/game.ts`,
					look({ declares: 'mod: "none", textSpeed: "game",' })
				);
				yield* command(["run", `${scratch}/player.ts`, `${scratch}/game.ts`]);
				const [player, game] = yield* far.starts;
				expect(player?.mods).toEqual({
					"skips/romfs/System/Skips.txt": new Uint8Array([7, 7]),
				});
				expect(player?.settings).toContain(
					"1\\disabled\\1\\d=+Text Speed Instant (built-in)"
				);
				expect(game?.mods).toEqual({});
				expect(game?.settings).not.toContain("Text Speed");
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a look starts with every value its mod's new game starts with, unless its state keeps the base's, and plan shows them apart",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, caller, scratch, command } = yield* farAndCaller({
					onA: openChest,
				});
				const skips = `${far.home}/data/game-testing/skips`;
				yield* fs.makeDirectory(`${skips}/romfs/GameData`, { recursive: true });
				yield* fs.writeFile(
					`${skips}/${startList}`,
					modList(fakeGame, { "IsTreasureBoxOpen.6006": true })
				);
				yield* fs.writeFileString(`${scratch}/mod.ts`, look({}));
				yield* fs.writeFileString(
					`${scratch}/base.ts`,
					look({ state: '{ base: "test-base", startValues: "base" }' })
				);
				const ran = yield* command([
					"run",
					`${scratch}/mod.ts`,
					`${scratch}/base.ts`,
				]);
				const records = yield* Effect.forEach(
					Arr.filter(ran.lines, Str.startsWith("passed  ")),
					(line) =>
						Effect.flatMap(
							fs.readFileString(
								`${caller.home}/state/game-testing/runs/${Arr.lastNonEmpty(Str.split(line, "  "))}/record.json`
							),
							Schema.decodeEffect(Schema.fromJsonString(RunRecord))
						),
					{ concurrency: 1 }
				);
				const started = Arr.map(
					records,
					(record) => record.readBack?.values["IsTreasureBoxOpen.6006"]
				);
				expect(started).toEqual([true, undefined]);
				expect(records[0]?.plan?.edits).toContainEqual(
					expect.objectContaining({
						name: "IsTreasureBoxOpen.6006",
						to: true,
						from: { _tag: "mod", mod: "skips" },
					})
				);
				expect(records[1]?.plan?.edits).not.toContainEqual(
					expect.objectContaining({ name: "IsTreasureBoxOpen.6006" })
				);

				const planned = yield* command(["plan", `${scratch}/mod.ts`]);
				expect(planned.lines.slice(-2)).toEqual([
					"From the mod skips, as its new game starts:",
					"IsTreasureBoxOpen.6006 = true    was false",
				]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a look with a mod that adds a value to the game's list starts, and its plan says the mod added it",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { far, caller, scratch, command } = yield* farAndCaller({
					onA: openChest,
				});
				const skips = `${far.home}/data/game-testing/skips`;
				yield* fs.makeDirectory(`${skips}/romfs/GameData`, { recursive: true });
				yield* fs.writeFile(
					`${skips}/${startList}`,
					modList(
						{
							...fakeGame,
							values: {
								...fakeGame.values,
								Kaepora_Once_1: { type: "Bool", value: false },
							},
						},
						{}
					)
				);
				yield* fs.writeFileString(`${scratch}/look.ts`, look({}));
				const ran = yield* command(["run", `${scratch}/look.ts`]);
				expect(ran.lines[0]).toStartWith("passed  ");
				const run = Arr.lastNonEmpty(Str.split(ran.lines[0] ?? "", "  "));
				const record = yield* Schema.decodeEffect(
					Schema.fromJsonString(RunRecord)
				)(
					yield* fs.readFileString(
						`${caller.home}/state/game-testing/runs/${run}/record.json`
					)
				);
				expect(record.plan?.edits).toContainEqual(
					expect.objectContaining({
						_tag: "added",
						name: `0x${Bun.hash.murmur32v3("Kaepora_Once_1").toString(16)}`,
						to: false,
						from: { _tag: "mod", mod: "skips" },
					})
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a machine with no mod that skips refuses a start that names no mod, and says what to do",
		() =>
			Effect.gen(function* () {
				const far = yield* fakeMachine({ noSkips: true });
				const caller = yield* fakeCaller(yield* served(far));
				const { lines } = yield* Effect.gen(function* () {
					yield* Effect.exit(
						Command.runWith(cli, { version: "test" })([
							"run",
							`${import.meta.dir}/scripts/opens-chest.ts`,
						])
					);
					return { lines: Arr.map(yield* TestConsole.logLines, String) };
				}).pipe(Effect.provide([caller.platform, TestConsole.layer]));
				expect(lines[0]).toStartWith("broken  ");
				expect(Arr.join(lines, "\n")).toContain('mod: "none"');
				expect(yield* creates(far)).toBe(0);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);
});
