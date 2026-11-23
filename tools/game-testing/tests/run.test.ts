import { BunServices } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	Effect,
	Exit,
	FileSystem,
	Layer,
	Option,
	Order,
	Schedule,
	Schema,
	String as Str,
} from "effect";
import { Command } from "effect/cli";
import { TestConsole } from "effect/testing";
import { cli } from "../src/cli.ts";
import { RunId } from "../src/contract/values.ts";
import { Runs } from "../src/host/runs.ts";
import { packageRoot } from "../src/scripts/runner.ts";
import type { FakeLichii } from "./fake-lichii.ts";
import { fakeMachine } from "./fake-machine.ts";

const scripts = `${import.meta.dir}/scripts`;

const openChest = (lichii: FakeLichii) => {
	Effect.runSync(lichii.put("IsTreasureBoxOpen.5005", true));
};
const endGame = (lichii: FakeLichii) => {
	Effect.runSync(lichii.exit);
};
/** Gives this process, and so every child it starts, another PATH, and answers the one it had. */
const swapPath = (path: string | undefined) => {
	const before = process.env.PATH;
	process.env.PATH = path;
	return before;
};

/** Runs the command line on a fake machine and gives what it printed, whether it succeeded, and the records it left. */
const command = (
	args: readonly string[],
	options?: {
		readonly places?: number;
		readonly onA?: (lichii: FakeLichii) => void;
	}
) =>
	Effect.gen(function* () {
		const machine = yield* fakeMachine(options);
		return yield* Effect.gen(function* () {
			const exit = yield* Effect.exit(
				Command.runWith(cli, { version: "test" })(args)
			);
			const lines = Arr.map(yield* TestConsole.logLines, String);
			const errors = Arr.map(yield* TestConsole.errorLines, String);
			// A game whose child died is ended by the host when it sees the socket close.
			const freed = yield* machine.podman.containers.pipe(
				Effect.repeat({
					schedule: Schedule.spaced("20 millis"),
					until: (held) => held.size === 0,
				}),
				Effect.timeout("10 seconds"),
				Effect.option
			);
			const runs = yield* Runs;
			const ids = Arr.flatMap(lines, (line) =>
				Arr.filter(Str.split(line, "  "), (word) => Schema.is(RunId)(word))
			);
			const records = yield* Effect.forEach(
				ids,
				(id) => runs.get(RunId.make(id)),
				{ concurrency: 1 }
			);
			return {
				succeeded: Exit.isSuccess(exit),
				lines,
				errors,
				records,
				freed: Option.isSome(freed),
				creates: Arr.filter(
					yield* machine.podman.calls,
					(call) => call[0] === "create"
				).length,
			};
		}).pipe(
			Effect.provide([
				Runs.layer.pipe(Layer.provideMerge(machine.platform)),
				TestConsole.layer,
			])
		);
	}).pipe(Effect.provide(BunServices.layer));

const lasting = { timeout: 60_000 };

describe("the command line", () => {
	it.live(
		"prove marks a chest's spot proven when A opens it, and says a look from it failed when A does not",
		() =>
			Effect.gen(function* () {
				const prove = ["prove", "--base", "test-base", "Fake Chest Check"];
				const opened = yield* command(prove, { onA: openChest });
				expect(opened.succeeded).toBe(true);
				expect(opened.lines[0]).toStartWith("passed  Fake Chest Check  ");
				expect(opened.lines).toContain("    marked: its spot is proven");
				const shut = yield* command(prove);
				expect(shut.succeeded).toBe(false);
				expect(shut.lines[0]).toStartWith("failed  Fake Chest Check  ");
				expect(shut.lines).toContainEqual(
					expect.stringContaining("marked: a look from its spot failed")
				);
			}),
		lasting
	);

	it.live(
		"run of a kept script ends passed, and its record holds each step and its picture",
		() =>
			Effect.gen(function* () {
				const { succeeded, lines, errors, records } = yield* command(
					["run", `${scripts}/opens-chest.ts`],
					{ onA: openChest }
				);
				expect(succeeded).toBe(true);
				expect(lines[0]).toStartWith("passed  ");
				expect(errors).toEqual([]);
				const [record] = records;
				expect(record?.outcome).toEqual({ _tag: "passed" });
				expect(record?.script?.path).toBe("tests/scripts/opens-chest.ts");
				expect(Arr.map(record?.steps ?? [], (step) => step.command)).toEqual([
					"press",
					"check",
					"screenshot",
				]);
				expect(record?.result).toEqual({
					opened: true,
					shot: "picture-1.png",
				});
			}),
		lasting
	);

	it.live(
		"a check that does not hold ends the run failed, and run exits 1",
		() =>
			Effect.gen(function* () {
				const { succeeded, lines } = yield* command(
					["run", `${scripts}/checks-other-chest.ts`],
					{ onA: openChest }
				);
				expect(succeeded).toBe(false);
				expect(lines[0]).toStartWith("failed  ");
				expect(lines[1]).toBe("    0x46dd3676 held false, not true");
			}),
		lasting
	);

	it.live(
		"a run whose Lichii dies is broken, and --rerun-broken runs it once more as an attempt of the first",
		() =>
			Effect.gen(function* () {
				const { succeeded, records } = yield* command(
					["run", "--rerun-broken", `${scripts}/opens-chest.ts`],
					{ onA: endGame }
				);
				expect(succeeded).toBe(false);
				const [first, second] = records;
				expect(first?.outcome._tag).toBe("broken");
				expect(second?.outcome._tag).toBe("broken");
				expect(second?.attemptOf).toBe(first?.id);
			}),
		lasting
	);

	it.live(
		"a script past its limit is stopped, and its game with it",
		() =>
			Effect.gen(function* () {
				const { lines, freed } = yield* command([
					"run",
					`${scripts}/waits-forever.ts`,
				]);
				expect(lines[0]).toStartWith("broken  ");
				expect(lines[2]).toBe(
					"    it ran past its limit of 4s and was stopped"
				);
				expect(freed).toBe(true);
			}),
		lasting
	);

	it.live(
		"a script whose process dies gives its place back",
		() =>
			Effect.gen(function* () {
				const { lines, freed, records } = yield* command([
					"run",
					`${scripts}/ends-itself.ts`,
				]);
				expect(lines[0]).toStartWith("broken  ");
				expect(freed).toBe(true);
				expect(records[0]?.outcome).toEqual({
					_tag: "broken",
					reason: "its caller went away before the script finished",
					error: "",
				});
			}),
		lasting
	);

	it.live(
		"a script runs on a machine with Bun and no Node",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const folder = `${packageRoot}/.cache/test-scripts`;
				yield* fs.makeDirectory(folder, { recursive: true });
				const bin = yield* fs.makeTempDirectoryScoped({ directory: folder });
				yield* fs.symlink(process.execPath, `${bin}/bun`);
				yield* Effect.acquireRelease(
					Effect.sync(() => swapPath(bin)),
					(before) => Effect.sync(() => swapPath(before))
				);
				const { lines } = yield* command(["run", `${scripts}/opens-chest.ts`], {
					onA: openChest,
				});
				expect(lines[0]).toStartWith("passed  ");
			}).pipe(Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a misspelled name fails the type check before any game starts",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const folder = `${packageRoot}/.cache/test-scripts`;
				yield* fs.makeDirectory(folder, { recursive: true });
				const scratch = yield* fs.makeTempDirectoryScoped({
					directory: folder,
				});
				yield* fs.writeFileString(
					`${scratch}/misspelled.ts`,
					`import { script } from "../../../src/script.ts";\n\nexport default script({\n\tabout: "A press of a button whose name is misspelled.",\n\tstate: { base: "test-base" },\n\trun: (game) => game.press("Plsu"),\n});\n`
				);
				const { lines, creates } = yield* command([
					"run",
					`${scratch}/misspelled.ts`,
				]);
				expect(lines[0]).toStartWith("broken  ");
				expect(lines[1]).toBe("    the script failed (typecheck)");
				expect(lines[2]).toContain('"Plsu"');
				expect(creates).toBe(0);
			}).pipe(Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a script given by a path that holds .. runs",
		() =>
			Effect.gen(function* () {
				const { succeeded, lines } = yield* command(
					["run", `${scripts}/../scripts/opens-chest.ts`],
					{ onA: openChest }
				);
				expect(lines[0]).toStartWith("passed  ");
				expect(succeeded).toBe(true);
			}),
		lasting
	);

	it.live(
		"the type check reads the generated names",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const folder = `${packageRoot}/.cache/test-scripts`;
				yield* fs.makeDirectory(folder, { recursive: true });
				const scratch = yield* fs.makeTempDirectoryScoped({
					directory: folder,
				});
				// A file of its own beside names.d.ts, which a machine with the game holds.
				const generated = `${packageRoot}/generated`;
				yield* fs.makeDirectory(generated, { recursive: true });
				const declared = yield* fs.makeTempFileScoped({
					directory: generated,
					suffix: ".d.ts",
				});
				yield* fs.writeFileString(
					declared,
					"declare const readByTheTypeCheck: true;\n"
				);
				yield* fs.writeFileString(
					`${scratch}/generated-names.ts`,
					`import { Effect } from "effect";\nimport { script } from "../../../src/script.ts";\n\nconst opens: typeof readByTheTypeCheck = true;\n\nexport default script({\n\tabout: "The chest opens, with an expected value only a generated name types.",\n\tstate: { base: "test-base" },\n\trun: (game) =>\n\t\tEffect.gen(function* () {\n\t\t\tyield* game.press("A");\n\t\t\treturn yield* game.check("0x1d2a8fff", opens);\n\t\t}),\n});\n`
				);
				const { lines } = yield* command(
					["run", `${scratch}/generated-names.ts`],
					{ onA: openChest }
				);
				expect(lines[0]).toStartWith("passed  ");
			}).pipe(Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a script that hands over to a person is skipped, and run exits 0",
		() =>
			Effect.gen(function* () {
				const { succeeded, lines, creates } = yield* command([
					"run",
					`${scripts}/hands-over.ts`,
				]);
				expect(succeeded).toBe(true);
				expect(lines[0]).toStartWith("skipped  ");
				expect(creates).toBe(0);
			}),
		lasting
	);

	it.live(
		"two scripts with --parallel 2 run at once, each in a place of its own",
		() =>
			Effect.gen(function* () {
				const { succeeded, records } = yield* command(
					[
						"run",
						"--parallel",
						"2",
						`${scripts}/opens-chest.ts`,
						`${scripts}/opens-chest.ts`,
					],
					{ places: 2, onA: openChest }
				);
				expect(succeeded).toBe(true);
				expect(
					Arr.sort(
						Arr.map(records, (record): number => record.place ?? 0),
						Order.Number
					)
				).toEqual([1, 2]);
			}),
		lasting
	);

	it.live(
		"plan prints each value a state writes and the word it came from",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const folder = yield* fs.makeTempDirectoryScoped();
				yield* fs.writeFileString(
					`${folder}/state.json`,
					'{ "base": "test-base", "set": { "PlayerStatus.CurrentRupee": 25 } }'
				);
				const { succeeded, lines } = yield* command([
					"plan",
					`${folder}/state.json`,
				]);
				expect(succeeded).toBe(true);
				expect(lines).toContain(
					"PlayerStatus.CurrentRupee = 25    was 0, from set PlayerStatus.CurrentRupee"
				);
			}).pipe(Effect.provide(BunServices.layer))
	);
	it.live(
		"a script whose module never finishes loading is stopped, and run carries on",
		() =>
			Effect.gen(function* () {
				const { succeeded, lines, creates } = yield* command([
					"run",
					`${scripts}/hangs-at-top.ts`,
				]);
				expect(succeeded).toBe(false);
				expect(lines[0]).toBe(`broken  ${scripts}/hangs-at-top.ts`);
				expect(lines[2]).toBe(
					"    it did not declare itself within 15s and was stopped"
				);
				expect(creates).toBe(0);
			}),
		lasting
	);

	it.live(
		"a script that waits for a place longer than its own limit still runs to its end",
		() =>
			Effect.gen(function* () {
				const { succeeded, lines } = yield* command(
					[
						"run",
						"--parallel",
						"2",
						`${scripts}/takes-two-seconds.ts`,
						`${scripts}/takes-two-seconds.ts`,
					],
					{ onA: openChest }
				);
				expect(succeeded).toBe(true);
				expect(
					Arr.filter(lines, (line) => Str.startsWith("passed  ")(line)).length
				).toBe(2);
			}),
		lasting
	);

	it.live(
		"a run with no check ends unchecked, and run exits 1",
		() =>
			Effect.gen(function* () {
				const { succeeded, lines } = yield* command([
					"run",
					`${scripts}/presses-only.ts`,
				]);
				expect(succeeded).toBe(false);
				expect(lines[0]).toStartWith("unchecked  ");
			}),
		lasting
	);
});
