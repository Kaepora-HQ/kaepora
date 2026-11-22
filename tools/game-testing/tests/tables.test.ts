import { BunFileSystem } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import { Effect, Layer } from "effect";
import { OwnTables } from "../src/save/tables.ts";

describe("our own tables", () => {
	it.effect(
		"name spots, bases, the Hookshot's tiers and every kind of check by its spoiler log name",
		() =>
			Effect.gen(function* () {
				const tables = yield* OwnTables;
				expect(tables.spots.get("Deku Tree Boss Room")).toEqual({
					name: "Deku Tree Boss Room",
					scene: "InsideTheDekuTree",
					room: 10,
					position: [15.383, 46.99, 67.636],
					facing: 80.1,
				});
				expect(tables.bases.get("early-child")?.hour).toBe("night");
				expect(tables.tiers).toContainEqual(["Item_Hookshot", "Item_Longshot"]);
				expect(
					tables.checks.get("Forest Temple Bow Chest")?.placements
				).toEqual(["11082303891540711688"]);
				expect(tables.checks.get("LW Skull Kid")?.flows).toEqual([
					"Npc_SkullKid_Child",
				]);
				expect(tables.checks.get("KF Shop Item 1")?.stock).toEqual([
					{ keeper: "Npc_Kokiri_001_Shop_Child_ShortStock", slot: 6 },
					{ keeper: "Npc_Kokiri_001_Shop_Child", slot: 6 },
					{ keeper: "Npc_Kokiri_001_Shop_Adult", slot: 6 },
				]);
			}).pipe(
				Effect.provide(OwnTables.layer.pipe(Layer.provide(BunFileSystem.layer)))
			)
	);
});
