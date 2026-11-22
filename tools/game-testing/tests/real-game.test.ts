/**
 * These need the game: `GAME_TESTING_GAME_FILE`, `GAME_TESTING_PATCHER` and the `new-game-autosave` base's three
 * files under `$XDG_DATA_HOME/game-testing/bases/`.
 */
import { BunServices } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import { Array as Arr, Effect, FileSystem, Layer, Record as Rec } from "effect";
import { UserDirectories } from "effect/persistence";
import { StartState } from "../src/contract/state.ts";
import type { Saved } from "../src/contract/values.ts";
import { GameData } from "../src/data/game-data.ts";
import { decode, encode } from "../src/save/codec.ts";
import { StartStates } from "../src/save/state.ts";
import { OwnTables } from "../src/save/tables.ts";

const base = "new-game-autosave";

const platform = Layer.mergeAll(BunServices.layer, UserDirectories.layer);
const layer = StartStates.layer.pipe(
	Layer.provideMerge([GameData.layer, OwnTables.layer]),
	Layer.provideMerge(platform)
);

/** CI has no game, so these run only where one is set. */
const noGame = Bun.env.GAME_TESTING_GAME_FILE === undefined;

describe("on the real game", () => {
	it.effect.skipIf(noGame)(
		"a new game's saves the game wrote decode and encode to the same bytes",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const directories = yield* UserDirectories.UserDirectories;
				const folder = `${yield* directories.data("game-testing")}/bases/${base}`;
				for (const file of ["progress.sav", "caption.sav", "option.sav"]) {
					const bytes = yield* fs.readFile(`${folder}/${file}`);
					expect(encode(yield* decode(file, bytes))).toEqual(bytes);
				}
			}).pipe(Effect.provide(platform))
	);

	it.effect.skipIf(noGame)(
		"lacks the Bow gives the 17 values, each at a new game's value",
		() =>
			Effect.gen(function* () {
				const states = yield* StartStates;
				const { edits } = yield* states.plan(
					StartState.make({ base, lacks: ["Item_Range_Bow"] })
				);
				const quivers = ["Small", "Medium", "Large"];
				const owned = (saved: string): [string, Saved][] => [
					[`IsGet.${saved}`, false],
					[`HavePlayedGetEvent.${saved}`, false],
					[`PouchData.${saved}.StockNum`, 0],
				];
				expect(
					Rec.fromEntries(
						Arr.flatMapNullishOr(edits, (edit) =>
							edit._tag === "value" && edit.from !== "base"
								? ([edit.name, edit.to] as const)
								: undefined
						)
					)
				).toEqual(
					Rec.fromEntries<[string, Saved]>([
						...owned("Weapon_Range_Bow"),
						["PouchData.Item_Arrow_Normal.StockNum", 0],
						...Arr.flatMap(quivers, (size) => owned(`Item_Quiver_${size}`)),
						["PouchContentMaxNum.Arrow", 0],
						["CurrentExpansionActor_Arrow", ""],
						["PlayerStatus.AssignableItem.Child", "Empty"],
						["PlayerStatus.AssignableItem.Adult", "Empty"],
					])
				);
			}).pipe(Effect.provide(layer)),
		{ timeout: 60_000 }
	);
});
