import { BunFileSystem } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import { ConfigProvider, Effect, FileSystem, Layer } from "effect";
import { UserDirectories } from "effect/persistence";
import { GameData } from "../src/data/game-data.ts";
import { DumpFailed, StateRefused, UnknownName } from "../src/errors.ts";
import type { FakeGame } from "./fake-game.ts";
import { fakeDump } from "./fake-game.ts";

const unmeasured = {
	path: "romfs/Fake/New.byml.zs",
	unchecked: "the game has no copy of it to measure it by",
};

const game: FakeGame = {
	key: "f00d",
	mods: {
		"too-large": {
			lines: [
				{ entry: 2048, needs: 4096, path: "romfs/Fake/Big.byml.zs" },
				unmeasured,
			],
			code: 3,
		},
		unmeasured: { lines: [unmeasured], code: 0 },
	},
	values: {
		"IsGet.Weapon_Range_Bow": { type: "Bool", value: false },
		"IsGet.Weapon_Range_SlingShot": { type: "Bool", value: false },
		"PouchData.Weapon_Range_Bow.StockNum": { type: "Int", value: 0 },
		"PlayerStatus.LiftGrade": {
			type: "Enum",
			value: "None",
			choices: ["None", "Low", "Middle", "High"],
		},
	},
};

const open = (options?: { readonly fail?: string }) =>
	Effect.gen(function* () {
		const fs = yield* FileSystem.FileSystem;
		const home = yield* fs.makeTempDirectoryScoped();
		const nup = `${home}/game.nup`;
		yield* fs.writeFileString(nup, "");
		const dump = yield* fakeDump(game, options);
		const gameData = GameData.layer.pipe(
			Layer.provide([
				dump.layer,
				UserDirectories.layer,
				ConfigProvider.layer(
					ConfigProvider.fromUnknown({
						HOME: home,
						XDG_CACHE_HOME: `${home}/cache`,
						GAME_TESTING_GAME_FILE: nup,
						GAME_TESTING_PATCHER: "kaepora-dump",
					})
				),
			])
		);
		return { dump, gameData };
	}).pipe(Effect.provide(BunFileSystem.layer));

const lookUp = (name: string) =>
	Effect.gen(function* () {
		const data = yield* GameData;
		return yield* data.value(name);
	});

describe("the game's tables", () => {
	it.effect(
		"a value by name gives its type, its default and an enum's choices",
		() =>
			Effect.gen(function* () {
				const { gameData } = yield* open();
				const value = yield* Effect.provide(
					lookUp("PlayerStatus.LiftGrade"),
					gameData
				);
				expect(value).toEqual({
					name: "PlayerStatus.LiftGrade",
					named: true,
					hash: Bun.hash.murmur32v3("PlayerStatus.LiftGrade"),
					type: "Enum",
					default: "None",
					file: "progress",
					choices: ["None", "Low", "Middle", "High"],
				});
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect("an unknown name gives its nearest names", () =>
		Effect.gen(function* () {
			const { gameData } = yield* open();
			const refused = yield* Effect.provide(
				Effect.flip(lookUp("IsGet.Weapon_Range_Slingshot")),
				gameData
			);
			expect(refused).toBeInstanceOf(UnknownName);
			expect(refused.suggestions[0]).toBe("IsGet.Weapon_Range_SlingShot");
		}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect("a cached dump is not dumped again", () =>
		Effect.gen(function* () {
			const { dump, gameData } = yield* open();
			yield* Effect.scoped(Layer.build(gameData));
			const first = yield* dump.subcommands;
			yield* Effect.scoped(Layer.build(gameData));
			const both = yield* dump.subcommands;
			expect(first).toContain("files");
			expect(both.slice(first.length)).toEqual(["key"]);
		}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect("a failing kaepora-dump fails with its own error", () =>
		Effect.gen(function* () {
			const { gameData } = yield* open({
				fail: "the game file is not a container",
			});
			const failed = yield* Effect.flip(Effect.scoped(Layer.build(gameData)));
			expect(failed).toEqual(
				DumpFailed.make({
					exitCode: 1,
					stderr: "the game file is not a container\n",
				})
			);
		}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a mod with a file the game would drop is refused, naming the file, and unmeasured files come back",
		() =>
			Effect.gen(function* () {
				const { gameData } = yield* open();
				const [refused, unchecked] = yield* Effect.provide(
					Effect.gen(function* () {
						const data = yield* GameData;
						return [
							yield* Effect.flip(data.checkMod("too-large")),
							yield* data.checkMod("unmeasured"),
						] as const;
					}),
					gameData
				);
				expect(refused).toEqual(
					StateRefused.make({
						reason: "mod-too-large",
						detail:
							"romfs/Fake/Big.byml.zs needs 4096 bytes in the size table, which gives it 2048.",
					})
				);
				expect(unchecked).toEqual([
					{
						path: "romfs/Fake/New.byml.zs",
						reason: "the game has no copy of it to measure it by",
					},
				]);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);
});
