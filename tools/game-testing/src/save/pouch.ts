import { Array as Arr, Effect, Option, Order, String as Str } from "effect";
import type { Age } from "../contract/state.ts";
import type { Saved } from "../contract/values.ts";
import type { PouchRow } from "../data/dump.ts";
import type { GameData, GameValue, Item } from "../data/game-data.ts";
import { StateRefused } from "../errors.ts";

/**
 * How strongly an edit holds: the record a person's word names outweighs what the library derives from it, which
 * outweighs a fresh place's reset.
 */
export type Weight = "fresh" | "derived" | "written";

export interface Writer {
	/** What the plan writes so far, else what the base holds. */
	readonly current: (value: GameValue) => Saved;
	/** What the base holds. */
	readonly held: (value: GameValue) => Saved;
	readonly put: (
		from: string,
		name: string,
		to: (value: GameValue) => Saved,
		weight: Weight
	) => Effect.Effect<void, StateRefused>;
}

const equipmentSlots = ["Weapon", "Shield", "Armor", "Boot", "HeadArmor"];
/** The slots whose item the save also keeps for the other age. */
const twoAgeSlots = ["Weapon", "Shield"];
const grades = ["None", "Low", "Middle", "High"];

/** An item's fields: its saved actor's own pouch row under its own, as a weapon's ammunition sits on the weapon. */
export const rowOf = (
	items: ReadonlyMap<string, Item>,
	item: Item
): PouchRow => {
	const actorRow = items.get(item.savedActor);
	return actorRow === undefined || actorRow.name === item.name
		? item.row
		: { ...actorRow.row, ...item.row };
};

/** The button of each age that can hold an item. */
const buttonsOf = (row: PouchRow) =>
	row.AssignableItem === undefined
		? []
		: Arr.map(["Child", "Adult"], (age) => ({
				value: `PlayerStatus.AssignableItem.${age}`,
				holds: row.AssignableItem as string,
			}));

/** The slots an item is worn in: its own, and for a sword or shield the one the save keeps for the other age. */
const slotsOf = (item: Item, row: PouchRow) => {
	if (row.PouchSpecialDeal === "Ocarina") {
		return [{ value: "PlayerStatus.EquipOcarina", holds: item.savedActor }];
	}
	if (
		row.ExpansionAvility === "LiftGradeUP" &&
		row.AdultEquipment !== undefined
	) {
		return [
			{ value: "PlayerStatus.EquipHandArmor", holds: row.AdultEquipment },
		];
	}
	const category = row.PouchCategory;
	if (category === undefined || !Arr.contains(equipmentSlots, category)) {
		return [];
	}
	return [
		{ value: `PlayerStatus.Equip${category}`, holds: item.savedActor },
		...(Arr.contains(twoAgeSlots, category)
			? [
					{
						value: `PlayerStatus.AnotherEraEquip${category}`,
						holds: item.savedActor,
					},
				]
			: []),
	];
};

/** Where `equip` puts an item: its slot, or else the button of Link's age. */
export const slotOf = (item: Item, row: PouchRow, age: Age) =>
	Option.orElse(Arr.head(slotsOf(item, row)), () =>
		Arr.findFirst(buttonsOf(row), (button) =>
			Str.endsWith(age === "child" ? "Child" : "Adult")(button.value)
		)
	);

const records = (saved: string) => [
	`IsGet.${saved}`,
	`HavePlayedGetEvent.${saved}`,
	`PouchData.${saved}.StockNum`,
];

/** A tier family, lowest first, and what beside the members' records it sets. */
interface Family {
	readonly kind: "container" | "ability" | "plain";
	readonly members: readonly Item[];
}

/** What `withItems` and `withoutItems` write for an item, through the pouch table, the tiers of ours and what the base holds. */
export const makePouch = (
	data: GameData["Service"],
	tiers: readonly (readonly string[])[],
	items: ReadonlyMap<string, Item>,
	writer: Writer
) => {
	const all = [...items.values()];
	const savedOf = (name: string) => items.get(name)?.savedActor ?? name;

	/** Whether the plan so far, or else the base, holds an item. */
	const holds = (saved: string) =>
		Effect.map(data.find(`IsGet.${saved}`), (found) =>
			Option.exists(found, (value) => writer.current(value) === true)
		);

	const ranked = Effect.fnUntraced(function* (
		members: readonly Item[],
		rank: (row: PouchRow) => number
	) {
		const recorded = yield* Effect.filter(
			members,
			(member) =>
				Effect.map(data.find(`IsGet.${member.savedActor}`), Option.isSome),
			{ concurrency: 1 }
		);
		return Arr.sort(
			Arr.dedupeWith(recorded, (a, b) => a.savedActor === b.savedActor),
			Order.mapInput(Order.Number, (item: Item) => rank(item.row))
		);
	});

	const containersOf = (kind: string) =>
		ranked(
			Arr.filter(
				all,
				(other) =>
					other.row.MaxNumGameData === kind &&
					other.row.ExpansionMaxNumActor !== undefined
			),
			(other) => other.AfterExpansionMaxNum ?? 0
		);

	const familyOf = Effect.fnUntraced(function* (item: Item, row: PouchRow) {
		const kind = row.MaxNumGameData;
		if (row.ExpansionMaxNumActor !== undefined && kind !== undefined) {
			const members = yield* containersOf(kind);
			const family: Family = { kind: "container", members };
			return family;
		}
		const ability = row.ExpansionAvility;
		if (ability === "LiftGradeUP" || ability === "DiveDepthUP") {
			const members = yield* ranked(
				Arr.filter(all, (other) => other.row.ExpansionAvility === ability),
				(other) =>
					ability === "LiftGradeUP"
						? grades.indexOf(other.LiftableGrade ?? "None")
						: (other.DiveDepth ?? 0)
			);
			const family: Family = { kind: "ability", members };
			return family;
		}
		if (row.PouchSpecialDeal === "Ocarina") {
			const members = yield* ranked(
				Arr.filter(all, (other) => other.row.PouchSpecialDeal === "Ocarina"),
				(other) => other.OcarinaTypeValue ?? 0
			);
			const family: Family = { kind: "plain", members };
			return family;
		}
		const group = Arr.findFirst(tiers, (tier) => Arr.contains(tier, item.name));
		return Option.getOrUndefined(
			Option.map(
				group,
				(names): Family => ({
					kind: "plain",
					members: Arr.flatMapNullishOr(names, (name) => items.get(name)),
				})
			)
		);
	});

	/** The highest member the plan or the base holds, other than the item itself. */
	const highestHeld = Effect.fnUntraced(function* (family: Family, item: Item) {
		const others = Arr.filter(
			family.members,
			(member) => member.savedActor !== item.savedActor
		);
		const held = yield* Effect.filter(
			others,
			(member) => holds(member.savedActor),
			{
				concurrency: 1,
			}
		);
		return Option.getOrUndefined(Arr.last(held));
	});

	/**
	 * The weapon's ammunition, when the weapon is what holds it: a weapon that shoots and adds no ability, such as the
	 * Bow, and not a magic arrow, which shoots the same arrows; nor the ammunition itself.
	 */
	const ammunitionOf = (item: Item, row: PouchRow) => {
		const shot =
			row.ShootableActor === undefined
				? undefined
				: items.get(row.ShootableActor);
		const kind = shot?.row.MaxNumGameData;
		return shot === undefined ||
			kind === undefined ||
			row.ExpansionAvility !== undefined ||
			shot.savedActor === item.savedActor
			? Option.none()
			: Option.some({ shot, kind, bag: shot.row.SmallBagActor });
	};

	/** The record that counts what a container fills: its stock, or for rupees the purse. */
	const countOf = Effect.fnUntraced(function* (actor: string, kind: string) {
		const stock = `PouchData.${savedOf(actor)}.StockNum`;
		const found = yield* data.find(stock);
		return Option.isSome(found) ? stock : `PlayerStatus.Current${kind}`;
	});

	const limits = (kind: string) =>
		[`PouchContentMaxNum.${kind}`, `CurrentExpansionActor_${kind}`] as const;

	const abilityRank = (row: PouchRow) =>
		row.ExpansionAvility === "LiftGradeUP"
			? grades.indexOf(row.LiftableGrade ?? "None")
			: (row.DiveDepth ?? 0);

	const ability = (row: PouchRow) =>
		row.ExpansionAvility === "LiftGradeUP"
			? { name: "PlayerStatus.LiftGrade", holds: row.LiftableGrade }
			: { name: "PlayerStatus.DiveDepth", holds: row.DiveDepth };

	/** An owned item's records: got, its get played, one of it. */
	const ownedRecords: readonly Saved[] = [true, true, 1];

	const own = (from: string, saved: string, owned: boolean, weight: Weight) =>
		Effect.forEach(
			records(saved),
			(name, i) =>
				writer.put(
					from,
					name,
					(value) => (owned ? (ownedRecords[i] as Saved) : value.default),
					weight
				),
			{ concurrency: 1, discard: true }
		);

	/** An ability's grade falls to the best tier kept, or rises to the best tier held. */
	const grade = Effect.fnUntraced(function* (
		from: string,
		item: Item,
		row: PouchRow,
		family: Family,
		owned: boolean
	) {
		const kept = yield* highestHeld(family, item);
		const candidates = Arr.flatMapNullishOr(
			[kept?.row, owned ? row : undefined],
			(each) => each
		);
		const best = Arr.last(
			Arr.sort(candidates, Order.mapInput(Order.Number, abilityRank))
		);
		const level = Option.getOrUndefined(
			Option.flatMap(best, (each) => Option.fromNullishOr(ability(each).holds))
		);
		yield* writer.put(
			from,
			ability(row).name,
			(value) => level ?? value.default,
			"derived"
		);
	});

	/** A slot or button that holds the item is emptied; a button holding another item is listed as it stands. */
	const unhold = Effect.fnUntraced(function* (
		from: string,
		item: Item,
		row: PouchRow
	) {
		for (const slot of slotsOf(item, row)) {
			const found = yield* data.find(slot.value);
			if (
				Option.exists(found, (value) => writer.current(value) === slot.holds)
			) {
				yield* writer.put(
					from,
					slot.value,
					(value) => value.default,
					"written"
				);
			}
		}
		for (const button of buttonsOf(row)) {
			const found = yield* data.find(button.value);
			const holding = Option.exists(
				found,
				(value) => writer.current(value) === button.holds
			);
			yield* writer.put(
				from,
				button.value,
				(value) => (holding ? value.default : writer.current(value)),
				holding ? "written" : "derived"
			);
		}
	});

	const withoutContainer = Effect.fnUntraced(function* (
		from: string,
		item: Item,
		row: PouchRow,
		family: Family
	) {
		const kind = row.MaxNumGameData as string;
		const kept = yield* highestHeld(family, item);
		const limit = kept?.row.AfterExpansionMaxNum;
		const [maximum, current] = limits(kind);
		yield* writer.put(
			from,
			maximum,
			(value) => limit ?? value.default,
			"derived"
		);
		yield* writer.put(
			from,
			current,
			(value) => kept?.savedActor ?? value.default,
			"derived"
		);
		for (const filled of row.ExpansionMaxNumActor ?? []) {
			const counted = yield* countOf(filled, kind);
			yield* writer.put(
				from,
				counted,
				(value) => {
					const count = writer.current(value);
					return limit === undefined || typeof count !== "number"
						? value.default
						: Math.min(count, limit);
				},
				"derived"
			);
		}
	});

	/** A weapon that holds its ammunition takes it, every container of it, and its limit. */
	const withoutAmmunition = Effect.fnUntraced(function* (
		from: string,
		item: Item,
		row: PouchRow
	) {
		const ammunition = Option.getOrUndefined(ammunitionOf(item, row));
		if (ammunition === undefined) {
			return;
		}
		yield* writer.put(
			from,
			`PouchData.${ammunition.shot.savedActor}.StockNum`,
			(value) => value.default,
			"derived"
		);
		const containers = yield* containersOf(ammunition.kind);
		for (const container of containers) {
			yield* own(from, container.savedActor, false, "derived");
		}
		for (const name of limits(ammunition.kind)) {
			yield* writer.put(from, name, (value) => value.default, "derived");
		}
	});

	/** A weapon that holds its ammunition comes with its smallest container, unless the plan or base holds one. */
	const hasAmmunition = Effect.fnUntraced(function* (
		from: string,
		item: Item,
		row: PouchRow,
		asked: readonly string[]
	) {
		const ammunition = Option.getOrUndefined(ammunitionOf(item, row));
		const bag =
			ammunition?.bag === undefined ? undefined : items.get(ammunition.bag);
		if (ammunition === undefined || bag === undefined) {
			return;
		}
		const containers = yield* containersOf(ammunition.kind);
		const held = yield* Effect.filter(
			containers,
			(container) => holds(container.savedActor),
			{ concurrency: 1 }
		);
		const named = Arr.some(containers, (container) =>
			Arr.contains(asked, container.name)
		);
		if (held.length === 0 && !named) {
			yield* withItem(bag, undefined, asked, from, "derived");
		}
	});

	const withoutItem = Effect.fn("StartStates.withoutItem")(function* (
		item: Item,
		from: string
	) {
		const row = rowOf(items, item);
		yield* own(from, item.savedActor, false, "written");
		yield* unhold(from, item, row);
		const family = yield* familyOf(item, row);
		if (family?.kind === "container") {
			yield* withoutContainer(from, item, row, family);
		}
		if (family?.kind === "ability") {
			yield* grade(from, item, row, family, false);
		}
		if (family === undefined) {
			yield* withoutAmmunition(from, item, row);
		}
		for (const equipment of [row.ChildEquipment, row.AdultEquipment]) {
			if (equipment !== undefined && equipment !== item.savedActor) {
				yield* own(from, equipment, false, "derived");
			}
		}
	});

	const hasContainer = Effect.fnUntraced(function* (
		from: string,
		item: Item,
		row: PouchRow,
		family: Family
	) {
		const kind = row.MaxNumGameData as string;
		const kept = yield* highestHeld(family, item);
		const top =
			kept !== undefined &&
			(kept.row.AfterExpansionMaxNum ?? 0) > (row.AfterExpansionMaxNum ?? 0)
				? kept
				: item;
		const limit = top.row.AfterExpansionMaxNum;
		const [maximum, current] = limits(kind);
		yield* writer.put(
			from,
			maximum,
			(value) => limit ?? value.default,
			"derived"
		);
		yield* writer.put(from, current, () => top.savedActor, "derived");
		for (const filled of row.ExpansionMaxNumActor ?? []) {
			if (filled !== row.ActorFullOnlyIfIsGetWhenGetBag) {
				const saved = savedOf(filled);
				yield* writer.put(from, `IsGet.${saved}`, () => true, "derived");
				yield* writer.put(
					from,
					`HavePlayedGetEvent.${saved}`,
					() => true,
					"derived"
				);
				const counted = yield* countOf(filled, kind);
				yield* writer.put(
					from,
					counted,
					(value) => limit ?? value.default,
					"derived"
				);
			}
		}
	});

	const withItem: (
		item: Item,
		count: number | undefined,
		asked: readonly string[],
		from: string,
		weight: Weight
	) => Effect.Effect<void, StateRefused> = Effect.fn("StartStates.withItem")(
		function* (
			item: Item,
			count: number | undefined,
			asked: readonly string[],
			from: string,
			weight: Weight
		) {
			const row = rowOf(items, item);
			if (count !== undefined && row.PouchStockable !== true) {
				return yield* StateRefused.make({
					reason: "incomplete",
					detail: `${item.name} keeps no count; name it without one.`,
				});
			}
			const [got, played, stock] = records(item.savedActor) as [
				string,
				string,
				string,
			];
			yield* writer.put(from, got, () => true, weight);
			yield* writer.put(from, played, () => true, weight);
			yield* writer.put(
				from,
				stock,
				() => count ?? 1,
				count === undefined ? "derived" : weight
			);
			const family = yield* familyOf(item, row);
			if (family?.kind === "container") {
				yield* hasContainer(from, item, row, family);
			}
			if (family?.kind === "ability") {
				yield* grade(from, item, row, family, true);
			}
			if (family === undefined) {
				yield* hasAmmunition(from, item, row, asked);
			}
		}
	);

	/** The limit and the container a count of ammunition needs, when the item is ammunition. */
	const holderOf = (item: Item) => {
		const row = rowOf(items, item);
		return row.MaxNumGameData === undefined ||
			row.ExpansionMaxNumActor !== undefined
			? Option.none()
			: Option.some({
					limit: limits(row.MaxNumGameData)[0],
					bag: row.SmallBagActor,
				});
	};

	return { withoutItem, withItem, holderOf };
};
