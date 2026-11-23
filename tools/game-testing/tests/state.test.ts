import { BunFileSystem } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	ConfigProvider,
	Effect,
	FileSystem,
	Layer,
	Option,
	Path,
	Record as Rec,
} from "effect";
import { UserDirectories } from "effect/persistence";
import type { PlannedEdit } from "../src/contract/plan.ts";
import type { PlayedSpot, Spot } from "../src/contract/spots.ts";
import type { Mod } from "../src/contract/state.ts";
import { StartState } from "../src/contract/state.ts";
import type { Saved } from "../src/contract/values.ts";
import { GameData, valueHash } from "../src/data/game-data.ts";
import {
	ModUnreadable,
	StateRefused,
	UnknownName,
	ValueRefused,
} from "../src/errors.ts";
import type { Scalar } from "../src/save/codec.ts";
import { decode, encode, read, write } from "../src/save/codec.ts";
import { chestSpots, keepPlayed, Spots } from "../src/save/spots.ts";
import { StartStates } from "../src/save/state.ts";
import type { Check } from "../src/save/tables.ts";
import { OwnTables } from "../src/save/tables.ts";
import type { FakeGame, FakeValue } from "./fake-game.ts";
import { fakeDump, fakeSave, modList, startList } from "./fake-game.ts";

const actor = (name: string) =>
	`Work/Actor/${name}.engine__actor__ActorParam.gyml`;

const owned = (saved: string): Record<string, FakeValue> => ({
	[`IsGet.${saved}`]: { type: "Bool", value: false },
	[`HavePlayedGetEvent.${saved}`]: { type: "Bool", value: false },
	[`PouchData.${saved}.StockNum`]: { type: "Int", value: 0 },
});

const text = (value = ""): FakeValue => ({ type: "String64", value });
const point: FakeValue = { type: "Vector3", value: [0, 0, 0] };
const flag: FakeValue = { type: "Bool", value: false };
const buttons = ["Empty", "Bow", "SlingShot", "LightArrow", "Bomb"];

const chest = "5005";
const door = "1001";
const pot = "2002";
const elsewhere = "3003";
const root = "9001";
const cover = "7007";
const grottoChest = "7100";
const rock = "7017";
const forestTemple = "Banc/ForestTemple/ForestTemple_Static.bcett.byml.zs";
const field = "Banc/FakeField/FakeField_Adult_Static.bcett.byml.zs";

/** The hash a chest record of the root would have, which the list holds for another value. */
const rootRecord = `0x${Bun.hash.murmur32v3(`IsTreasureBoxOpen.${root}`).toString(16)}`;

const quiver = (name: string, limit: number) => ({
	__RowId: name,
	PouchCategory: "KeyItem",
	MaxNumGameData: "Arrow",
	ExpansionMaxNumActor: actor("Item_Arrow_Normal"),
	AfterExpansionMaxNum: limit,
});

/** Modelled on the game's tables: the Bow, its arrows and quivers, a magic arrow, bombs, a sword, a temple, a field. */
const game: FakeGame = {
	key: "5eed",
	values: {
		...owned("Weapon_Range_Bow"),
		...owned("Item_Arrow_Normal"),
		...owned("Item_Arrow_Light"),
		...owned("Item_Quiver_Small"),
		...owned("Item_Quiver_Medium"),
		...owned("Item_Quiver_Large"),
		...owned("Item_Bomb"),
		...owned("Item_BombBag_Small"),
		...owned("Weapon_Sword_Kokiri"),
		"PouchContentMaxNum.Arrow": { type: "Int", value: 0 },
		CurrentExpansionActor_Arrow: text(),
		"PouchContentMaxNum.Bomb": { type: "Int", value: 0 },
		CurrentExpansionActor_Bomb: text(),
		"PlayerStatus.AssignableItem.Child": {
			type: "Enum",
			value: "Empty",
			choices: buttons,
		},
		"PlayerStatus.AssignableItem.Adult": {
			type: "Enum",
			value: "Empty",
			choices: buttons,
		},
		"PlayerStatus.EquipWeapon": text(),
		"PlayerStatus.EquipShield": text(),
		"PlayerStatus.AnotherEraEquipWeapon": text(),
		"PlayerStatus.AnotherEraEquipShield": text(),
		Sequence_IsChildhood: { type: "Bool", value: true },
		"PlayerStatus.CurrentRupee": { type: "Int", value: 0 },
		Sequence_CurrentBanc: text(),
		"0x4a6bdb7d": { type: "String32", value: "" },
		DungeonRoom: { type: "Int", value: -1 },
		"PlayerStatus.SavePos": point,
		"0x8773f02d": point,
		"PlayerStatus.SavePosRadY": { type: "Float", value: 0 },
		"IsVisitLocation.ForestTemple": flag,
		"IsVisitLocation.FakeField": flag,
		Player_Pos_For_Map: point,
		Player_Rot_For_Map: { type: "Float", value: 0 },
		"CaptionData.LocationName": { ...text(), file: 2 },
		"0x0cccc6cd": { ...text(), file: 2 },
		"CaptionData.Pos": { ...point, file: 2 },
		"CaptionData.Dir": { type: "Float", value: 0, file: 2 },
		"CaptionData.IsAutoSave": { type: "Bool", value: true, file: 2 },
		Step_GatheringTokenGold: {
			type: "Enum",
			value: "NotReady",
			choices: ["NotReady", "Ready", "CollectToken", "Finish", "Complete"],
		},
		"HavePlayedEvent.Fake_Event_0010": flag,
		[`IsTreasureBoxOpen.${chest}`]: flag,
		[`IsTreasureBoxOpen.${grottoChest}`]: flag,
		[`IsChainLockDoorOpen.${door}`]: flag,
		[rootRecord]: flag,
		Fake_Room_Cleared: flag,
		Fake_Child_Flag: { type: "Int", value: 0 },
		Fake_Other_Room: flag,
		FakeField_Gate_Open: flag,
		Fake_Lake_Gate: flag,
		Fake_Reward10: flag,
		Fake_Reward20: flag,
	},
	keys: [pot, "2000", elsewhere, cover],
	actorInfo: [
		{ __RowId: "Item_Range_Bow", ActorName: "Weapon_Range_Bow" },
		{ __RowId: "Shot_Arrow", ActorName: "Item_Arrow_Normal" },
		{ __RowId: "Item_Sword_Kokiri", ActorName: "Weapon_Sword_Kokiri" },
	],
	pouch: [
		{ __RowId: "Item_Range_Bow", PouchCategory: "Bow" },
		{
			__RowId: "Weapon_Range_Bow",
			PouchCategory: "Bow",
			AssignableItem: "Bow",
			ShootableActor: actor("Shot_Arrow"),
		},
		{
			__RowId: "Item_Arrow_Light",
			PouchCategory: "Bow",
			AssignableItem: "LightArrow",
			ShootableActor: actor("Shot_Arrow"),
			ExpansionAvility: "MPMaxUP",
		},
		{
			__RowId: "Shot_Arrow",
			PouchCategory: "Arrow",
			MaxNumGameData: "Arrow",
			SmallBagActor: actor("Item_Quiver_Small"),
		},
		{
			__RowId: "Item_Arrow_Normal",
			PouchCategory: "Arrow",
			MaxNumGameData: "Arrow",
			SmallBagActor: actor("Item_Quiver_Small"),
			PouchStockable: true,
		},
		quiver("Item_Quiver_Small", 30),
		quiver("Item_Quiver_Medium", 40),
		quiver("Item_Quiver_Large", 50),
		{
			__RowId: "Item_Bomb",
			PouchCategory: "Material",
			MaxNumGameData: "Bomb",
			SmallBagActor: actor("Item_BombBag_Small"),
			AssignableItem: "Bomb",
			PouchStockable: true,
		},
		{
			__RowId: "Item_BombBag_Small",
			PouchCategory: "KeyItem",
			MaxNumGameData: "Bomb",
			ExpansionMaxNumActor: actor("Item_Bomb"),
			AfterExpansionMaxNum: 20,
		},
		{ __RowId: "Item_Sword_Kokiri", PouchCategory: "Weapon" },
	],
	quests: ["GatheringTokenGold"],
	scenes: [
		{ name: "ForestTemple", folder: "ForestTemple", rooms: 31 },
		{ name: "FakeField_Adult", folder: "FakeField" },
	],
	landings: {
		ForestTemple: {
			"": { Rot: [Math.PI, 0, Math.PI], Trans: [1, 2, 3] },
		},
		FakeField_Adult: {
			Gate: { Rot: [0, 0, 0], Trans: [40, 50, 40] },
		},
	},
	placements: {
		[forestTemple]: {
			Actors: [
				{
					Gyaml: "Obj_TBox_Fake",
					Hash: chest,
					Translate: [7, 8, 9],
					Dynamic: { DefaultDungeonRoomIndex: 17 },
				},
				{
					Gyaml: "Obj_Door_Fake",
					Hash: door,
					Translate: [14, 8, 2],
					Dynamic: { DefaultDungeonRoomIndex: 17 },
				},
				{
					Gyaml: "Obj_Pot_Fake",
					Hash: pot,
					Translate: [3, 8, 6],
					Dynamic: { DefaultDungeonRoomIndex: 17 },
				},
				{
					Gyaml: "Obj_Pot_Fake",
					Hash: elsewhere,
					Translate: [80, 10, 4],
					Dynamic: { DefaultDungeonRoomIndex: 3 },
				},
			],
			AiGroups: [
				{
					Logic: "Logic/Module/Fake/Fake_Room_Module.logic.module.ain",
					References: [{ Reference: chest }],
				},
				{
					Logic: "Logic/Module/Fake/Fake_Other_Module.logic.module.ain",
					References: [{ Reference: elsewhere }],
				},
			],
		},
		[field]: {
			Actors: [
				{ Gyaml: "Land_Fake_Root", Hash: root, Translate: [10, 50, 10] },
				{
					Gyaml: "Land_Fake_HidingHoleCover_A_01",
					Hash: cover,
					Translate: [100, 50, 100],
				},
				{
					Gyaml: "DestinationAnchor",
					Hash: "7008",
					Name: "HidingHole_FieldEntranceGo",
					Translate: [100, 50, 100],
				},
				{
					Gyaml: "DestinationAnchor",
					Hash: "7009",
					Name: "HidingHole_FieldExitGo",
					Translate: [100, 30, 100],
				},
				{
					Gyaml: "Obj_TBox_Fake",
					Hash: grottoChest,
					Translate: [112, 30, 100],
					Rotate: [0, -Math.PI / 2, 0],
				},
				{ Gyaml: "Obj_LiftRock_B_01", Hash: rock, Translate: [200, 50, 100] },
				{
					Gyaml: "DestinationAnchor",
					Hash: "7018",
					Name: "HidingHole_FieldEntranceGo",
					Translate: [200, 50, 100],
				},
				{
					Gyaml: "DestinationAnchor",
					Hash: "7019",
					Name: "HidingHole_FieldExitGo",
					Translate: [200, 30, 100],
				},
				{ Gyaml: "Obj_TBox_Fake", Hash: "7200", Translate: [212, 30, 100] },
			],
			AiGroups: [
				{
					Logic: "Logic/Module/Fake/Fake_Field_Module.logic.module.ain",
					References: [{ Reference: root }],
				},
			],
		},
	},
	modules: {
		Fake_Room_Module: ["Fake_Room_Cleared", "Fake_Child_Module", "Bool"],
		Fake_Child_Module: ["Fake_Child_Flag"],
		Fake_Other_Module: ["Fake_Other_Room"],
		Fake_Field_Module: [
			"Sequence_IsChildhood",
			"FakeField_Gate_Open",
			"Fake_Lake_Gate",
			"Step_GatheringTokenGold",
		],
	},
	flows: { Fake_Reward_Flow: ["Fake_Reward10", "Fake_Reward20"] },
};

const checks = new Map<string, Check>([
	[
		"Forest Temple Bow Chest",
		{
			name: "Forest Temple Bow Chest",
			placements: [chest],
			flows: [],
			stock: [],
		},
	],
	[
		"FF Fake Grotto Chest",
		{
			name: "FF Fake Grotto Chest",
			placements: [grottoChest],
			flows: [],
			stock: [],
		},
	],
	[
		"FF Fake Rock Grotto Chest",
		{
			name: "FF Fake Rock Grotto Chest",
			placements: ["7200"],
			flows: [],
			stock: [],
		},
	],
	[
		"Forest Temple Pot Chest",
		{
			name: "Forest Temple Pot Chest",
			placements: [pot],
			flows: [],
			stock: [],
		},
	],
	[
		"Forest Temple Two Chests",
		{
			name: "Forest Temple Two Chests",
			placements: [chest, pot],
			flows: [],
			stock: [],
		},
	],
	[
		"Fake Missing Chest",
		{ name: "Fake Missing Chest", placements: [], flows: [], stock: [] },
	],
	[
		"Fake 20 Token Reward",
		{
			name: "Fake 20 Token Reward",
			placements: [],
			flows: ["Fake_Reward_Flow"],
			stock: [],
		},
	],
]);

const rewardFlowValues = /Fake_Reward10.*Fake_Reward20/u;

const found = (
	name: string,
	scene: string,
	position: Spot["position"],
	facing: number
): Spot => ({
	name,
	scene,
	position,
	facing,
	found: "save",
	evidence: "a save the game wrote",
	note: "",
});

const spots: readonly Spot[] = [
	found("Fake Field Gate", "FakeField_Adult", [41, 50, 42], 90),
	found("Fake Field Pond", "FakeField_Adult", [122, 30, 100], 0),
	found("Fake Field Stump", "FakeField_Adult", [60, 30, 80], 270),
	{
		...found("Forest Temple Pot Chest", "ForestTemple", [4, 8, 6], 270),
		room: 17,
	},
	found("Fake Two Ages", "FakeField_Child", [1, 30, 1], 0),
	found("Fake Two Ages", "FakeField_Adult", [2, 30, 2], 0),
];

const chests = [
	"Forest Temple Bow Chest",
	"Forest Temple Pot Chest",
	"FF Fake Grotto Chest",
	"Fake Missing Chest",
];

/** The start states of the fake game, from a base of its defaults with `holding` and `keys` written over them. */
const startStates = (
	holding: Readonly<Record<string, Scalar>> = {},
	keys: readonly string[] = [pot]
) =>
	Effect.gen(function* () {
		const fs = yield* FileSystem.FileSystem;
		const home = yield* fs.makeTempDirectoryScoped();
		const nup = `${home}/game.nup`;
		yield* fs.writeFileString(nup, "");
		const progress = yield* decode("progress.sav", yield* fakeSave(game, 0));
		const held = encode(
			yield* write(
				progress,
				Arr.map(Rec.toEntries(holding), ([name, value]) => ({
					hash: valueHash(name),
					value,
				})),
				Arr.map(keys, (key) => ({ key: BigInt(key), set: true }))
			)
		);
		const caption = yield* fakeSave(game, 2);
		const option = new Uint8Array([1, 2, 3]);
		const base = `${home}/data/game-testing/bases/test-base`;
		yield* fs.makeDirectory(base, { recursive: true });
		yield* fs.writeFile(`${base}/progress.sav`, held);
		yield* fs.writeFile(`${base}/caption.sav`, caption);
		yield* fs.writeFile(`${base}/option.sav`, option);
		const sha256 = (bytes: Uint8Array) =>
			new Bun.CryptoHasher("sha256").update(bytes).digest("hex");
		const dump = yield* fakeDump(game);
		const config = ConfigProvider.layer(
			ConfigProvider.fromUnknown({
				HOME: home,
				XDG_CACHE_HOME: `${home}/cache`,
				XDG_DATA_HOME: `${home}/data`,
				GAME_TESTING_GAME_FILE: nup,
				GAME_TESTING_PATCHER: "kaepora-dump",
			})
		);
		const own = Layer.succeed(
			OwnTables,
			OwnTables.of({
				spots,
				bases: new Map([
					[
						"test-base",
						{
							name: "test-base",
							hour: "day",
							age: "child",
							progress: sha256(held),
							caption: sha256(caption),
							option: sha256(option),
							about: "the fake game's defaults",
						},
					],
				]),
				tiers: [],
				checks,
				chests,
			})
		);
		return StartStates.layer.pipe(
			Layer.provideMerge(Spots.layer),
			Layer.provideMerge([GameData.layer, own]),
			Layer.provide([dump.layer, UserDirectories.layer, config, Path.layer])
		);
	}).pipe(Effect.provide(BunFileSystem.layer));

type Words = Omit<ConstructorParameters<typeof StartState>[0], "base">;

const plan = (
	words: Words,
	holding?: Readonly<Record<string, Scalar>>,
	keys?: readonly string[],
	mod: Mod | "none" = "none",
	recorded: readonly Spot[] = []
) =>
	Effect.gen(function* () {
		const layer = yield* startStates(holding, keys);
		return yield* Effect.provide(
			Effect.gen(function* () {
				const kept = yield* Spots;
				yield* Effect.forEach(recorded, kept.keep, {
					concurrency: 1,
					discard: true,
				});
				const states = yield* StartStates;
				return yield* states.plan(
					StartState.make({ base: "test-base", ...words }),
					mod
				);
			}),
			[layer, BunFileSystem.layer]
		);
	}).pipe(Effect.provide(BunFileSystem.layer));

/** What each word's edit writes, by the value's name or `key <hash>`; the base's own edit is tested on its own. */
const writes = (edits: readonly PlannedEdit[]) =>
	Rec.fromEntries(
		Arr.map(
			Arr.filter(edits, (edit) => edit.from._tag !== "rule"),
			(edit): [string, Saved] =>
				edit._tag === "key"
					? [`key ${edit.key}`, edit.to]
					: [edit.name, edit.to]
		)
	);

/** A base holding the Bow on a button, every quiver, 50 arrows and the Light Arrows. */
const archer = {
	"IsGet.Weapon_Range_Bow": true,
	"IsGet.Item_Quiver_Small": true,
	"IsGet.Item_Quiver_Medium": true,
	"IsGet.Item_Quiver_Large": true,
	"IsGet.Item_Arrow_Normal": true,
	"PouchData.Item_Arrow_Normal.StockNum": 50,
	"PouchContentMaxNum.Arrow": 50,
	CurrentExpansionActor_Arrow: "Item_Quiver_Large",
	"IsGet.Item_Arrow_Light": true,
	"PlayerStatus.AssignableItem.Child": "Bow",
	"PlayerStatus.AssignableItem.Adult": "LightArrow",
};

describe("the start state", () => {
	it.effect("the Bow in withoutItems gives the 17 values", () =>
		Effect.gen(function* () {
			const { edits } = yield* plan(
				{ withoutItems: ["Item_Range_Bow"] },
				{
					"PlayerStatus.AssignableItem.Child": "Bow",
					"PlayerStatus.AssignableItem.Adult": "SlingShot",
				}
			);
			expect(writes(edits)).toEqual({
				"IsGet.Weapon_Range_Bow": false,
				"HavePlayedGetEvent.Weapon_Range_Bow": false,
				"PouchData.Weapon_Range_Bow.StockNum": 0,
				"PouchData.Item_Arrow_Normal.StockNum": 0,
				"IsGet.Item_Quiver_Small": false,
				"HavePlayedGetEvent.Item_Quiver_Small": false,
				"PouchData.Item_Quiver_Small.StockNum": 0,
				"IsGet.Item_Quiver_Medium": false,
				"HavePlayedGetEvent.Item_Quiver_Medium": false,
				"PouchData.Item_Quiver_Medium.StockNum": 0,
				"IsGet.Item_Quiver_Large": false,
				"HavePlayedGetEvent.Item_Quiver_Large": false,
				"PouchData.Item_Quiver_Large.StockNum": 0,
				"PouchContentMaxNum.Arrow": 0,
				CurrentExpansionActor_Arrow: "",
				"PlayerStatus.AssignableItem.Child": "Empty",
				"PlayerStatus.AssignableItem.Adult": "SlingShot",
			});
		})
	);

	it.effect(
		"the Light Arrows in withoutItems go alone, and the Bow's quivers and arrows stay",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan(
					{ withoutItems: ["Item_Arrow_Light"] },
					archer
				);
				expect(writes(edits)).toEqual({
					"IsGet.Item_Arrow_Light": false,
					"HavePlayedGetEvent.Item_Arrow_Light": false,
					"PouchData.Item_Arrow_Light.StockNum": 0,
					"PlayerStatus.AssignableItem.Child": "Bow",
					"PlayerStatus.AssignableItem.Adult": "Empty",
				});
			})
	);

	it.effect(
		"the large quiver in withoutItems leaves the highest quiver the base still holds",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan(
					{ withoutItems: ["Item_Quiver_Large"] },
					{
						...archer,
						"IsGet.Item_Quiver_Medium": false,
						"PouchData.Item_Arrow_Normal.StockNum": 40,
					}
				);
				expect(writes(edits)).toMatchObject({
					"IsGet.Item_Quiver_Large": false,
					"PouchContentMaxNum.Arrow": 30,
					CurrentExpansionActor_Arrow: "Item_Quiver_Small",
					"PouchData.Item_Arrow_Normal.StockNum": 30,
				});
				expect(writes(edits)).not.toHaveProperty("IsGet.Item_Quiver_Medium");
			})
	);

	it.effect("the large quiver in withItems comes alone, full", () =>
		Effect.gen(function* () {
			const { edits } = yield* plan(
				{ withItems: ["Item_Quiver_Large"] },
				{ "IsGet.Item_Quiver_Small": true, "PouchContentMaxNum.Arrow": 30 }
			);
			expect(writes(edits)).toEqual({
				"IsGet.Item_Quiver_Large": true,
				"HavePlayedGetEvent.Item_Quiver_Large": true,
				"PouchData.Item_Quiver_Large.StockNum": 1,
				"PouchContentMaxNum.Arrow": 50,
				CurrentExpansionActor_Arrow: "Item_Quiver_Large",
				"IsGet.Item_Arrow_Normal": true,
				"HavePlayedGetEvent.Item_Arrow_Normal": true,
				"PouchData.Item_Arrow_Normal.StockNum": 50,
			});
		})
	);

	it.effect(
		"a count written for arrows wins over the quiver's fill, in either order",
		() =>
			Effect.gen(function* () {
				const arrows = { item: "Item_Arrow_Normal", count: 17 };
				const first = yield* plan({ withItems: [arrows, "Item_Quiver_Small"] });
				const second = yield* plan({
					withItems: ["Item_Quiver_Small", arrows],
				});
				expect(
					writes(first.edits)["PouchData.Item_Arrow_Normal.StockNum"]
				).toBe(17);
				expect(
					writes(second.edits)["PouchData.Item_Arrow_Normal.StockNum"]
				).toBe(17);
			})
	);

	it.effect(
		"the Bow in withItems brings the smallest quiver, full of arrows",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan({ withItems: ["Item_Range_Bow"] });
				expect(writes(edits)).toEqual({
					"IsGet.Weapon_Range_Bow": true,
					"HavePlayedGetEvent.Weapon_Range_Bow": true,
					"PouchData.Weapon_Range_Bow.StockNum": 1,
					"IsGet.Item_Quiver_Small": true,
					"HavePlayedGetEvent.Item_Quiver_Small": true,
					"PouchData.Item_Quiver_Small.StockNum": 1,
					"PouchContentMaxNum.Arrow": 30,
					CurrentExpansionActor_Arrow: "Item_Quiver_Small",
					"IsGet.Item_Arrow_Normal": true,
					"HavePlayedGetEvent.Item_Arrow_Normal": true,
					"PouchData.Item_Arrow_Normal.StockNum": 30,
				});
			})
	);

	it.effect("the Kokiri Sword in withoutItems leaves its slot", () =>
		Effect.gen(function* () {
			const { edits } = yield* plan(
				{ withoutItems: ["Item_Sword_Kokiri"] },
				{
					"IsGet.Weapon_Sword_Kokiri": true,
					"PlayerStatus.EquipWeapon": "Weapon_Sword_Kokiri",
				}
			);
			expect(writes(edits)).toMatchObject({
				"IsGet.Weapon_Sword_Kokiri": false,
				"PlayerStatus.EquipWeapon": "",
			});
		})
	);

	it.effect(
		"a word that cannot be honoured whole is refused: a count on an item with none, bombs with no bag, an unheld item equipped",
		() =>
			Effect.gen(function* () {
				const words: Words[] = [
					{ withItems: [{ item: "Item_Range_Bow", count: 5 }] },
					{ withItems: [{ item: "Item_Bomb", count: 20 }] },
					{ equipped: ["Item_Sword_Kokiri"] },
				];
				const refusals = yield* Effect.forEach(
					words,
					(each) => Effect.flip(plan(each)),
					{ concurrency: 1 }
				);
				expect(refusals).toMatchObject([
					{ reason: "incomplete", detail: expect.stringContaining("no count") },
					{
						reason: "incomplete",
						detail: expect.stringContaining("Item_BombBag_Small"),
					},
					{
						reason: "incomplete",
						detail: expect.stringContaining("withItems"),
					},
				]);
			})
	);

	it.effect("an item in both withItems and withoutItems is refused", () =>
		Effect.gen(function* () {
			const refused = yield* Effect.flip(
				plan({
					withItems: [{ item: "Item_Arrow_Normal", count: 1 }],
					withoutItems: ["Item_Arrow_Normal"],
				})
			);
			expect(refused).toBeInstanceOf(StateRefused);
			expect(refused).toMatchObject({ reason: "contradiction" });
		})
	);

	it.effect(
		"a room in reset gets back its chest, door and key records, and what its modules name",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan({ reset: ["ForestTemple/17"] });
				expect(writes(edits)).toEqual({
					[`IsTreasureBoxOpen.${chest}`]: false,
					[`IsChainLockDoorOpen.${door}`]: false,
					[`key ${pot}`]: false,
					"key 2000": false,
					Fake_Room_Cleared: false,
					Fake_Child_Flag: 0,
				});
			})
	);

	it.effect(
		"a whole field in reset keeps the age a word wrote, puts back the field's own and lists what it left alone",
		() =>
			Effect.gen(function* () {
				const planned = yield* plan(
					{ age: "adult", reset: ["FakeField_Adult"] },
					{
						FakeField_Gate_Open: true,
						Fake_Lake_Gate: true,
						[rootRecord]: true,
					},
					[cover]
				);
				expect(writes(planned.edits)).toEqual({
					Sequence_IsChildhood: false,
					"PlayerStatus.EquipWeapon": "",
					"PlayerStatus.AnotherEraEquipWeapon": "",
					"PlayerStatus.EquipShield": "",
					"PlayerStatus.AnotherEraEquipShield": "",
					FakeField_Gate_Open: false,
					[`IsTreasureBoxOpen.${grottoChest}`]: false,
					[`key ${cover}`]: false,
				});
				expect(Arr.map(planned.leftAlone, (left) => left.name)).toEqual([
					"Fake_Lake_Gate",
					"Step_GatheringTokenGold",
				]);
			})
	);

	it.effect(
		"a check in reset with no record of its own is refused, naming what its flow holds",
		() =>
			Effect.gen(function* () {
				const refused = yield* Effect.flip(
					plan({ reset: ["Fake 20 Token Reward"] })
				);
				expect(refused).toMatchObject({
					reason: "incomplete",
					detail: expect.stringMatching(rewardFlowValues),
				});
			})
	);

	it.effect("a room the scene does not have is refused", () =>
		Effect.gen(function* () {
			const refused = yield* Effect.flip(plan({ reset: ["ForestTemple/99"] }));
			expect(refused).toBeInstanceOf(UnknownName);
			expect(refused).toMatchObject({ kind: "room", asked: "ForestTemple/99" });
		})
	);

	it.effect(
		"a location that is a landing places Link in its scene, room, spot and facing, and names the place in the caption",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan({ location: "ForestTemple" });
				const position = [1, 2, 3] as const;
				expect(writes(edits)).toEqual({
					Sequence_CurrentBanc: "ForestTemple",
					"0x4a6bdb7d": "ForestTemple",
					DungeonRoom: 0,
					"PlayerStatus.SavePos": position,
					"0x8773f02d": position,
					"PlayerStatus.SavePosRadY": Math.PI,
					"IsVisitLocation.ForestTemple": true,
					"CaptionData.LocationName": "ForestTemple",
					"0x0cccc6cd": "ForestTemple/ForestTemple",
					"CaptionData.Pos": position,
					"CaptionData.Dir": 0,
				});
			})
	);

	it.effect(
		"a location that is a check stands Link before its actor, facing it, and one that is a spot stands him where the table says",
		() =>
			Effect.gen(function* () {
				const before = yield* plan({ location: "Forest Temple Bow Chest" });
				expect(writes(before.edits)).toMatchObject({
					Sequence_CurrentBanc: "ForestTemple",
					DungeonRoom: 17,
					"PlayerStatus.SavePos": [7, 8.1, 10],
					"PlayerStatus.SavePosRadY": Math.PI,
				});
				const spot = yield* plan({ age: "adult", location: "Fake Field Gate" });
				expect(writes(spot.edits)).toMatchObject({
					Sequence_CurrentBanc: "FakeField_Adult",
					"PlayerStatus.SavePos": [41, 50, 42],
					"PlayerStatus.SavePosRadY": Math.PI / 2,
					Player_Pos_For_Map: [41, 50, 42],
				});
			})
	);

	it.effect(
		"a facing past half a turn is saved as the game holds it, between minus and plus half a turn",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan({
					age: "adult",
					location: "Fake Field Stump",
				});
				expect(writes(edits)).toMatchObject({
					"PlayerStatus.SavePosRadY": -Math.PI / 2,
				});
			})
	);

	it.effect(
		"a start inside a closed grotto is refused, and inside an open one it is not",
		() =>
			Effect.gen(function* () {
				const shut = yield* Effect.flip(
					plan({ age: "adult", location: "FF Fake Grotto Chest" })
				);
				expect(shut).toMatchObject({
					reason: "shut",
					detail: expect.stringContaining(cover),
				});
				const open = yield* plan(
					{ age: "adult", location: "FF Fake Grotto Chest" },
					{},
					[cover]
				);
				expect(writes(open.edits).Sequence_CurrentBanc).toBe("FakeField_Adult");
			})
	);

	it.effect(
		"a start inside a grotto under a silver rock left in place is refused",
		() =>
			Effect.gen(function* () {
				const shut = yield* Effect.flip(
					plan({ age: "adult", location: "FF Fake Rock Grotto Chest" })
				);
				expect(shut).toMatchObject({
					reason: "shut",
					detail: expect.stringContaining(rock),
				});
			})
	);

	it.effect(
		"a start beside a closed grotto, as low as its floor, is not refused",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan({
					age: "adult",
					location: "Fake Field Pond",
				});
				expect(writes(edits)["PlayerStatus.SavePos"]).toEqual([122, 30, 100]);
			})
	);

	it.effect("a child in an adult's scene is refused", () =>
		Effect.gen(function* () {
			const refused = yield* Effect.flip(plan({ location: "Fake Field Gate" }));
			expect(refused).toMatchObject({ reason: "contradiction" });
		})
	);

	it.effect(
		"age swaps the sword and shield the save keeps for the other age",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan(
					{ age: "adult" },
					{
						"PlayerStatus.EquipWeapon": "Weapon_Sword_Kokiri",
						"PlayerStatus.EquipShield": "Weapon_Shield_Deku",
					}
				);
				expect(writes(edits)).toEqual({
					Sequence_IsChildhood: false,
					"PlayerStatus.EquipWeapon": "",
					"PlayerStatus.AnotherEraEquipWeapon": "Weapon_Sword_Kokiri",
					"PlayerStatus.EquipShield": "",
					"PlayerStatus.AnotherEraEquipShield": "Weapon_Shield_Deku",
				});
			})
	);

	it.effect(
		"equipped puts a held item in its slot, or on a button for its age",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan(
					{
						withItems: ["Item_Range_Bow"],
						equipped: ["Item_Sword_Kokiri", "Item_Range_Bow"],
					},
					{ "IsGet.Weapon_Sword_Kokiri": true }
				);
				expect(writes(edits)).toMatchObject({
					"PlayerStatus.EquipWeapon": "Weapon_Sword_Kokiri",
					"PlayerStatus.AssignableItem.Child": "Bow",
				});
			})
	);

	it.effect("story writes a quest's step and marks an event played", () =>
		Effect.gen(function* () {
			const { edits } = yield* plan({
				story: {
					quests: { GatheringTokenGold: "CollectToken" },
					events: ["Fake_Event_0010"],
				},
			});
			expect(writes(edits)).toEqual({
				Step_GatheringTokenGold: "CollectToken",
				"HavePlayedEvent.Fake_Event_0010": true,
			});
		})
	);

	it.effect(
		"set wins over what a word derives, two written words that disagree are refused, and a misfit is refused",
		() =>
			Effect.gen(function* () {
				const { edits } = yield* plan({
					withoutItems: ["Item_Range_Bow"],
					set: { "PouchContentMaxNum.Arrow": 40 },
				});
				expect(writes(edits)["PouchContentMaxNum.Arrow"]).toBe(40);
				const disagree = yield* Effect.flip(
					plan({ age: "adult", set: { Sequence_IsChildhood: true } })
				);
				expect(disagree).toMatchObject({ reason: "contradiction" });
				const misfit = yield* Effect.flip(
					plan({ set: { "PlayerStatus.CurrentRupee": true } })
				);
				expect(misfit).toBeInstanceOf(ValueRefused);
			})
	);

	it.effect(
		"a mod's start value is written where the base holds the game's own, left alone where play moved it, and a word wins",
		() =>
			Effect.gen(function* () {
				const mod: Mod = {
					name: "test-mod",
					files: [
						{
							path: startList,
							bytes: modList(game, {
								"HavePlayedEvent.Fake_Event_0010": true,
								Fake_Child_Flag: 1,
								"PlayerStatus.CurrentRupee": 99,
							}),
						},
					],
				};
				const planned = yield* plan(
					{ set: { "PlayerStatus.CurrentRupee": 7 } },
					{ Fake_Child_Flag: 5 },
					undefined,
					mod
				);
				expect(
					Arr.map(
						Arr.filter(planned.edits, (edit) => edit.from._tag !== "rule"),
						(edit) => [
							edit.from,
							edit._tag === "value" ? edit.name : "",
							edit.to,
						]
					)
				).toEqual([
					[
						{ _tag: "mod", mod: "test-mod" },
						"HavePlayedEvent.Fake_Event_0010",
						true,
					],
					[
						{ _tag: "word", word: "set", named: "PlayerStatus.CurrentRupee" },
						"PlayerStatus.CurrentRupee",
						7,
					],
				]);
				expect(planned.leftAlone).toEqual([
					{ name: "Fake_Child_Flag", from: { _tag: "mod", mod: "test-mod" } },
				]);
			})
	);

	it.effect(
		"a value the mod left alone that a word then writes is written, not left alone",
		() =>
			Effect.gen(function* () {
				const mod: Mod = {
					name: "test-mod",
					files: [
						{ path: startList, bytes: modList(game, { Fake_Child_Flag: 1 }) },
					],
				};
				const planned = yield* plan(
					{ set: { Fake_Child_Flag: 3 } },
					{ Fake_Child_Flag: 5 },
					undefined,
					mod
				);
				expect(writes(planned.edits)).toEqual({ Fake_Child_Flag: 3 });
				expect(planned.leftAlone).toEqual([]);
			})
	);

	it.effect(
		"a mod whose game data list cannot be read is refused with a sentence that names the mod",
		() =>
			Effect.gen(function* () {
				const refused = (bytes: Uint8Array) =>
					Effect.flip(
						plan({}, undefined, undefined, {
							name: "test-mod",
							files: [{ path: startList, bytes }],
						})
					);
				const notAList = yield* refused(
					new TextEncoder().encode('{ "Data": 3 }')
				);
				expect(notAList).toBeInstanceOf(ModUnreadable);
				expect(notAList.message).toContain(`test-mod/${startList}`);

				const notAFile = yield* refused(new TextEncoder().encode("not zstd"));
				expect(notAFile).toBeInstanceOf(ModUnreadable);
				expect(notAFile.message).toContain(`test-mod/${startList}`);
			})
	);

	it.effect(
		"a value the mod adds to the game's list is planned under the mod for the game to start, and the save builds",
		() =>
			Effect.gen(function* () {
				const layer = yield* startStates();
				const mod: Mod = {
					name: "test-mod",
					files: [
						{
							path: startList,
							bytes: modList(
								{ ...game, values: { ...game.values, Fake_Added: flag } },
								{ Fake_Added: true }
							),
						},
					],
				};
				const saves = yield* Effect.provide(
					Effect.gen(function* () {
						const states = yield* StartStates;
						const planned = yield* states.plan(
							StartState.make({ base: "test-base" }),
							mod
						);
						expect(planned.edits).toContainEqual({
							_tag: "added",
							name: `0x${Bun.hash.murmur32v3("Fake_Added").toString(16)}`,
							file: "progress",
							type: "Bool",
							to: true,
							from: { _tag: "mod", mod: "test-mod" },
						});
						return yield* states.build(planned);
					}),
					[layer, BunFileSystem.layer]
				);
				const progress = yield* decode("progress.sav", saves.progress);
				expect(read(progress, Bun.hash.murmur32v3("Fake_Added"))).toEqual(
					Option.none()
				);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect("reset puts a place back to its mod's start values", () =>
		Effect.gen(function* () {
			const mod: Mod = {
				name: "test-mod",
				files: [
					{ path: startList, bytes: modList(game, { Fake_Child_Flag: 1 }) },
				],
			};
			const planned = yield* plan(
				{ reset: ["ForestTemple/17"] },
				{ Fake_Child_Flag: 5 },
				undefined,
				mod
			);
			expect(writes(planned.edits)).toMatchObject({ Fake_Child_Flag: 1 });
			expect(planned.leftAlone).toEqual([]);
		})
	);

	it.effect(
		"build writes the plan into the base's saves, and the save is not an autosave",
		() =>
			Effect.gen(function* () {
				const layer = yield* startStates();
				const saves = yield* Effect.provide(
					Effect.gen(function* () {
						const states = yield* StartStates;
						const planned = yield* states.plan(
							StartState.make({
								base: "test-base",
								location: "ForestTemple",
								reset: ["ForestTemple/17"],
								set: { "PlayerStatus.CurrentRupee": 99 },
							}),
							"none"
						);
						return yield* states.build(planned);
					}),
					[layer, BunFileSystem.layer]
				);
				const progress = yield* decode("progress.sav", saves.progress);
				const caption = yield* decode("caption.sav", saves.caption);
				const value = (file: typeof progress, name: string) =>
					read(file, Bun.hash.murmur32v3(name));
				expect(value(progress, "PlayerStatus.CurrentRupee")).toEqual(
					Option.some(99)
				);
				expect(value(progress, "Sequence_CurrentBanc")).toEqual(
					Option.some("ForestTemple")
				);
				expect(value(caption, "CaptionData.LocationName")).toEqual(
					Option.some("ForestTemple")
				);
				expect(value(caption, "CaptionData.IsAutoSave")).toEqual(
					Option.some(false)
				);
				expect(progress.keys).toEqual([]);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);
});

describe("the spots", () => {
	it.effect(
		"a location that is a check with a spot stands Link on the spot, and the plan says it was found",
		() =>
			Effect.gen(function* () {
				const planned = yield* plan({ location: "Forest Temple Pot Chest" });
				expect(writes(planned.edits)).toMatchObject({
					"PlayerStatus.SavePos": [4, 8, 6],
					DungeonRoom: 17,
				});
				expect(planned.location).toMatchObject({
					scene: "ForestTemple",
					kind: "found",
				});
			})
	);

	it.effect(
		"a spot recorded on this machine wins over the tracked one at once",
		() =>
			Effect.gen(function* () {
				const recorded: Spot = {
					...found("Forest Temple Pot Chest", "ForestTemple", [5, 8, 6], 270),
					room: 17,
					found: "play",
				};
				const planned = yield* plan(
					{ location: "Forest Temple Pot Chest" },
					{},
					undefined,
					"none",
					[recorded]
				);
				expect(writes(planned.edits)["PlayerStatus.SavePos"]).toEqual([
					5, 8, 6,
				]);
				expect(planned.location?.kind).toBe("found");
			})
	);

	it.effect(
		"a location that is a check with no place of its own and no spot is refused with how to record one",
		() =>
			Effect.gen(function* () {
				const refused = yield* Effect.flip(
					plan({ location: "Fake 20 Token Reward" })
				);
				expect(refused.message).toContain("no place of its own");
				expect(refused.message).toContain("game-testing record");
				expect(refused).toMatchObject({ reason: "no-place" });
			})
	);

	it.effect(
		"a chest opened in play keeps the spot Link stands on, facing the chest he opened, in its room",
		() =>
			Effect.gen(function* () {
				const layer = yield* startStates();
				const results = yield* Effect.provide(
					Effect.gen(function* () {
						const kept = yield* Spots;
						const keep = (played: PlayedSpot) =>
							Effect.gen(function* () {
								const data = yield* GameData;
								const own = yield* OwnTables;
								return yield* keepPlayed(data, own, kept, played);
							});
						const opened = {
							name: "Forest Temple Two Chests",
							scene: "ForestTemple",
							room: 3,
							facing: 0,
						};
						const near = yield* keep({
							...opened,
							position: [3, 8, 7],
							chest: pot,
						});
						const far = yield* Effect.flip(
							keep({ ...opened, position: [3, 8, 11], chest: pot })
						);
						const states = yield* StartStates;
						const planned = yield* states.plan(
							StartState.make({
								base: "test-base",
								location: "Forest Temple Two Chests",
							}),
							"none"
						);
						return { near, far, planned };
					}),
					[layer, BunFileSystem.layer]
				);
				expect(results.near).toMatchObject({
					room: 17,
					position: [3, 8, 7],
					facing: 180,
					found: "play",
				});
				expect(results.far.message).toContain("5 units");
				expect(writes(results.planned.edits)["PlayerStatus.SavePos"]).toEqual([
					3, 8, 7,
				]);
				expect(results.planned.location?.kind).toBe("found");
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"a spot kept by name needs a name the tool knows or one new on purpose, and keeps a dungeon's room",
		() =>
			Effect.gen(function* () {
				const layer = yield* startStates();
				const results = yield* Effect.provide(
					Effect.gen(function* () {
						const kept = yield* Spots;
						const keep = (played: PlayedSpot) =>
							Effect.gen(function* () {
								const data = yield* GameData;
								const own = yield* OwnTables;
								return yield* keepPlayed(data, own, kept, played);
							});
						const here = {
							scene: "ForestTemple",
							room: 3,
							position: [9, 8, 9] as const,
							facing: 45,
						};
						const typo = yield* Effect.flip(
							keep({ ...here, name: "Forest Temple Bow Chets" })
						);
						const landing = yield* Effect.flip(
							keep({ ...here, name: "ForestTemple", new: true })
						);
						const known = yield* keep({
							...here,
							name: "Fake 20 Token Reward",
						});
						const fresh = yield* keep({
							...here,
							name: "Fake Statue",
							new: true,
						});
						return { typo, landing, known, fresh };
					}),
					[layer, BunFileSystem.layer]
				);
				expect(results.typo.message).toContain("Forest Temple Bow Chest");
				expect(results.typo.message).toContain("--new");
				expect(results.landing.message).toContain("landing");
				expect(results.known).toMatchObject({ room: 3, facing: 45 });
				expect(results.fresh).toMatchObject({ name: "Fake Statue", room: 3 });
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect(
		"spots moved into git leave the recorded table, and one recorded again since stays",
		() =>
			Effect.gen(function* () {
				const layer = yield* startStates();
				const left = yield* Effect.provide(
					Effect.gen(function* () {
						const kept = yield* Spots;
						const first = {
							...found("Fake Statue", "ForestTemple", [1, 8, 1], 0),
							found: "play" as const,
						};
						yield* kept.keep(first);
						const moved = yield* kept.recorded;
						yield* kept.keep({ ...first, position: [2, 8, 2] });
						yield* kept.forget(moved);
						return yield* kept.recorded;
					}),
					[layer, BunFileSystem.layer]
				);
				expect(Arr.map(left, (spot) => spot.position)).toEqual([[2, 8, 2]]);
			}).pipe(Effect.provide(BunFileSystem.layer))
	);

	it.effect("the list gives every chest its best spot, or none", () =>
		Effect.gen(function* () {
			const layer = yield* startStates();
			const listed = yield* Effect.provide(
				Effect.gen(function* () {
					const kept = yield* Spots;
					yield* kept.keep({
						...found(
							"FF Fake Grotto Chest",
							"FakeField_Adult",
							[110, 30, 100],
							90
						),
						found: "play",
					});
					return yield* chestSpots(yield* GameData, yield* OwnTables, kept);
				}),
				[layer, BunFileSystem.layer]
			);
			expect(
				Arr.map(listed, (listedChest) => [
					listedChest.name,
					listedChest.area,
					Arr.map(listedChest.spots, (spot) => `${spot.age} ${spot.kind}`),
				])
			).toEqual([
				["Forest Temple Bow Chest", "ForestTemple", ["either worked out"]],
				["Forest Temple Pot Chest", "ForestTemple", ["either found"]],
				["FF Fake Grotto Chest", "FakeField", ["adult found"]],
				["Fake Missing Chest", "", []],
			]);
		}).pipe(Effect.provide(BunFileSystem.layer))
	);
});
