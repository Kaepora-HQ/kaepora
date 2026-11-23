import {
	Array as Arr,
	Context,
	Effect,
	Equal,
	FileSystem,
	Layer,
	Option,
	Order,
	Path,
	Record as Rec,
	String as Str,
} from "effect";
import { UserDirectories } from "effect/persistence";
import type { Origin, PlannedEdit, StatePlan } from "../contract/plan.ts";
import { originText } from "../contract/plan.ts";
import type { StartSpot } from "../contract/spots.ts";
import { kindOf } from "../contract/spots.ts";
import type { Age, Mod, StartState } from "../contract/state.ts";
import type { Saved, Value } from "../contract/values.ts";
import type {
	GameValue,
	Item,
	Placement,
	Point,
	Scene,
	StartValue,
} from "../data/game-data.ts";
import { GameData, nearest, valueHash } from "../data/game-data.ts";
import { encodeValue } from "../emulator/values.ts";
import type { DumpFailed } from "../errors.ts";
import {
	ModUnreadable,
	SaveUnreadable,
	StateRefused,
	UnknownName,
	ValueRefused,
} from "../errors.ts";
import { readMod } from "../mod.ts";
import { sha256 } from "../sha256.ts";
import type { SaveFile } from "./codec.ts";
import { decode, encode, read, write } from "./codec.ts";
import type { Weight, Writer } from "./pouch.ts";
import { makePouch, rowOf, slotOf } from "./pouch.ts";
import { aboutSpot, placedFor, Spots, workedOutAbout } from "./spots.ts";
import type { Base, Check, Stock } from "./tables.ts";
import { OwnTables } from "./tables.ts";

export interface Plan extends StatePlan {
	readonly base: Base;
	/** The mod the place gets alone, if any. */
	readonly mod: Option.Option<Mod>;
}

/** What the state's words write, before the base and the mod are named beside them. */
type Written = Omit<StatePlan, "base">;

/** A place's save: the state written into the base's `progress.sav` and `caption.sav`, and the base's `option.sav`. */
export interface Saves {
	readonly caption: Uint8Array;
	readonly option: Uint8Array;
	readonly progress: Uint8Array;
}

/** Values of the save no string of the game names, found by comparing saves the game wrote. */
const unnamed = {
	/** The scene's `DungeonParam` entry, empty outside one. */
	dungeon: "0x4a6bdb7d",
	/** Follows `PlayerStatus.SavePos`. */
	lastPosition: "0x8773f02d",
	/** The caption's `<folder>/<scene>`. */
	captionLabel: "0x0cccc6cd",
} as const;

/** How far before a check's actor Link stands: near enough that A reaches a small chest. */
const before = 1;
const lift = 0.1;

/**
 * A grotto is a hole, the anchor named `HidingHole_FieldEntranceGo`, over a floor, the anchor named
 * `HidingHole_FieldExitGo` at the same x and z. Link stands inside it within 20 units across of the hole and 6 of the
 * floor's height: every grotto check stands 9 to 18.3 across and within 4.9 of its floor, and the nearest start outside
 * one, a chest in Mido's house, 20.7 across and 7.9 above. A cover standing on the hole shuts it while the cover's
 * per-actor key is not set: a storms or bomb cover, a rock to break or a silver rock to lift.
 */
const grotto = {
	hole: "HidingHole_FieldEntranceGo",
	floor: "HidingHole_FieldExitGo",
	across: 20,
	height: 6,
};
const onAnchor = 1;
const rocks = new Set([
	"Obj_BrokenRock_A_01",
	"Obj_BrokenRock_A_02",
	"Obj_LiftRock_B_01",
]);
const isCover = (actor: string) =>
	(Str.includes("HidingHoleCover")(actor) &&
		!Str.startsWith("FldObj_AirWall")(actor)) ||
	rocks.has(actor);
const across = (a: Point, b: Point) => Math.hypot(a[0] - b[0], a[2] - b[2]);

const sceneRoom = /^(?<scene>.+)\/(?<room>\d+)$/u;
const weights: Readonly<Record<Weight, number>> = {
	mod: 0,
	reset: 1,
	derived: 2,
	written: 3,
};

const degrees = (angle: number) => (angle * 180) / Math.PI;
const radians = (angle: number) => (angle * Math.PI) / 180;
/** The caption's and the map's direction, in degrees: the facing turned half round. */
const direction = (facing: number) => (((facing % 360) + 360) % 360) - 180;
/** Link's facing as the game holds it once loaded, in radians above minus half a turn and up to half a turn. */
const savedFacing = (facing: number) => {
	const turned = ((facing % 360) + 360) % 360;
	return radians(turned > 180 ? turned - 360 : turned);
};

/** Whether a name carries a place's name, as the game spells it in either case. */
const carries = (name: string, place: readonly string[]) =>
	Arr.some(place, (part) =>
		Str.includes(Str.toLowerCase(part))(Str.toLowerCase(name))
	);

export class StartStates extends Context.Service<
	StartStates,
	{
		/** `mod` is the mod the place gets alone, `none` for none; absent, the machine's own mod that skips. */
		readonly plan: (
			state: StartState,
			mod: Mod | "none" | undefined
		) => Effect.Effect<
			Plan,
			| UnknownName
			| StateRefused
			| ValueRefused
			| SaveUnreadable
			| ModUnreadable
			| DumpFailed
		>;
		readonly build: (plan: Plan) => Effect.Effect<Saves, SaveUnreadable>;
	}
>()("game-testing/save/state/StartStates") {
	static readonly layer = Layer.effect(
		StartStates,
		Effect.gen(function* () {
			const data = yield* GameData;
			const own = yield* OwnTables;
			const spots = yield* Spots;
			const fs = yield* FileSystem.FileSystem;
			const paths = yield* Path.Path;
			const directories = yield* UserDirectories.UserDirectories;
			const home = yield* directories.data("game-testing").pipe(Effect.orDie);
			const skips = `${home}/skips`;
			const items = new Map(Arr.map(data.items, (item) => [item.name, item]));
			const questNames = yield* data.names("quest");
			const quests = new Set(questNames);

			/** A file of the base, checked against the SHA-256 `tables/bases.tsv` gives it. */
			const bytesOf = Effect.fnUntraced(function* (
				base: Base,
				file: "progress" | "caption" | "option"
			) {
				const path = `${home}/bases/${base.name}/${file}.sav`;
				if (base[file] === "") {
					return yield* SaveUnreadable.make({
						path,
						detail: `tables/bases.tsv gives ${base.name} no ${file}.sav`,
					});
				}
				const bytes = yield* fs
					.readFile(path)
					.pipe(
						Effect.mapError(() =>
							SaveUnreadable.make({ path, detail: "there is no such file" })
						)
					);
				if (sha256(bytes) !== base[file]) {
					return yield* SaveUnreadable.make({
						path,
						detail: `its SHA-256 is ${sha256(bytes)}, not the ${base[file]} that tables/bases.tsv gives`,
					});
				}
				return bytes;
			});
			const load = Effect.fn("StartStates.load")(function* (
				base: Base,
				file: "progress" | "caption"
			) {
				const bytes = yield* bytesOf(base, file);
				return yield* decode(`${home}/bases/${base.name}/${file}.sav`, bytes);
			});

			/** The mod a start names: the caller's, none, or the machine's own that skips. */
			const modOf = (mod: Mod | "none" | undefined) => {
				if (mod === "none") {
					return Effect.succeedNone;
				}
				return mod === undefined
					? readMod(fs, paths, skips).pipe(
							Effect.mapError(() =>
								ModUnreadable.make({ folder: skips, reason: "no-skips" })
							),
							Effect.asSome
						)
					: Effect.succeedSome(mod);
			};

			const plan = Effect.fn("StartStates.plan")(function* (
				state: StartState,
				asked: Mod | "none" | undefined
			) {
				const base = own.bases.get(state.base);
				if (base === undefined) {
					return yield* UnknownName.make({
						kind: "base",
						asked: state.base,
						suggestions: nearest(state.base, [...own.bases.keys()]),
					});
				}
				const saves = {
					progress: yield* load(base, "progress"),
					caption: yield* load(base, "caption"),
				};
				const mod = yield* modOf(asked);
				const starts =
					Option.isNone(mod) || state.startValues === "base"
						? []
						: yield* data.startValues(mod.value);
				const words = makeWords({
					data,
					own,
					spots,
					items,
					quests,
					saves,
					starts,
				});
				yield* words.notAutosave();
				if (Option.isSome(mod)) {
					yield* words.startValues(mod.value.name, starts);
				}
				yield* words.age(state.age);
				yield* words.items(state.withItems ?? [], state.withoutItems ?? []);
				yield* words.equipped(
					state.equipped ?? [],
					state.age,
					state.withoutItems ?? []
				);
				yield* Effect.forEach(state.reset ?? [], words.reset, {
					concurrency: 1,
					discard: true,
				});
				yield* words.story(state.story);
				if (state.location !== undefined) {
					yield* words.location(state.location, state.age);
				}
				yield* words.set(state.set ?? {});
				yield* words.counts(state.withItems ?? []);
				const planned: Plan = { base, mod, ...words.result() };
				return planned;
			});

			const build = Effect.fn("StartStates.build")(function* (planned: Plan) {
				const written = Effect.fnUntraced(function* (
					file: "progress" | "caption",
					save: SaveFile
				) {
					const values = Arr.flatMapNullishOr(planned.edits, (edit) =>
						edit._tag === "value" && edit.file === file
							? { hash: valueHash(edit.name), value: edit.to }
							: undefined
					);
					const keys =
						file === "progress"
							? Arr.flatMapNullishOr(planned.edits, (edit) =>
									edit._tag === "key"
										? { key: BigInt(edit.key), set: edit.to }
										: undefined
								)
							: [];
					const edited = yield* write(save, values, keys);
					return encode(edited);
				});
				const progress = yield* load(planned.base, "progress");
				const caption = yield* load(planned.base, "caption");
				const saves: Saves = {
					progress: yield* written("progress", progress),
					caption: yield* written("caption", caption),
					option: yield* bytesOf(planned.base, "option"),
				};
				return saves;
			});

			return StartStates.of({ plan, build });
		})
	);
}

interface Inputs {
	readonly data: GameData["Service"];
	readonly items: ReadonlyMap<string, Item>;
	readonly own: OwnTables["Service"];
	readonly quests: ReadonlySet<string>;
	readonly saves: { readonly progress: SaveFile; readonly caption: SaveFile };
	readonly spots: Spots["Service"];
	/** What the mod's new game starts with, where the game's own starts otherwise. */
	readonly starts: readonly StartValue[];
}

/** The words of one state, writing into one plan. */
const makeWords = ({
	data,
	own,
	spots,
	items,
	quests,
	saves,
	starts,
}: Inputs) => {
	const planned = new Map<
		string,
		{ readonly edit: PlannedEdit; readonly weight: Weight }
	>();
	const leftAlone = new Map<
		string,
		{ readonly name: string; readonly from: Origin }
	>();
	let standing: StartSpot | undefined;

	/** A record keeps its strongest edit; two words that write it two ways are refused. */
	const place = Effect.fnUntraced(function* (
		id: string,
		edit: PlannedEdit,
		weight: Weight
	) {
		const old = planned.get(id);
		if (old !== undefined && weights[old.weight] > weights[weight]) {
			return;
		}
		if (
			old?.weight === "written" &&
			weight === "written" &&
			!Equal.equals(old.edit.to, edit.to)
		) {
			const name = edit._tag === "key" ? `key ${edit.key}` : edit.name;
			return yield* StateRefused.make({
				reason: "contradiction",
				detail: `${originText(old.edit.from)} writes ${name} as ${String(old.edit.to)}, and ${originText(edit.from)} as ${String(edit.to)}.`,
			});
		}
		planned.delete(id);
		planned.set(id, { edit, weight });
	});

	const notOfThisGame = (name: string) => () =>
		new Error(
			`The base save's ${name} does not match the game file's game data list.`
		);

	const held = (value: GameValue): Saved => {
		const save = value.file === "caption" ? saves.caption : saves.progress;
		const raw = Option.getOrThrowWith(
			read(save, value.hash),
			notOfThisGame(value.name)
		);
		if (value.type !== "Enum" || typeof raw !== "number") {
			return raw;
		}
		return Option.getOrThrowWith(
			Arr.findFirst(
				value.choices,
				(choice) => Bun.hash.murmur32v3(choice) === raw
			),
			notOfThisGame(value.name)
		);
	};
	const current = (value: GameValue): Saved => {
		const edit = planned.get(`value ${value.hash}`)?.edit;
		return edit === undefined ? held(value) : edit.to;
	};
	const started = new Map(
		Arr.map(starts, (start) => [start.value.hash, start.to])
	);
	/** What a new game holds: what the mod starts it with, or else the game's own default. */
	const newGame = (value: GameValue) =>
		started.get(value.hash) ?? value.default;
	const keyHeld = (key: bigint) => {
		const edit = planned.get(`key ${key}`)?.edit;
		return edit === undefined
			? Arr.contains(saves.progress.keys ?? [], key)
			: edit.to === true;
	};

	/** Writes a value a start state's files keep; a name that names none writes nothing. */
	const put = Effect.fn("StartStates.put")(function* (
		from: Origin,
		name: string,
		to: (value: GameValue) => Saved,
		weight: Weight
	) {
		const found = yield* data.find(name);
		if (Option.isNone(found)) {
			return;
		}
		const { value } = found;
		if (value.file !== "progress" && value.file !== "caption") {
			return;
		}
		yield* place(
			`value ${value.hash}`,
			{
				_tag: "value",
				name: value.name,
				file: value.file,
				to: to(value),
				base: held(value),
				from,
			},
			weight
		);
	});
	const putKey = Effect.fnUntraced(function* (from: Origin, key: bigint) {
		if (yield* data.isKey(key)) {
			yield* place(
				`key ${key}`,
				{
					_tag: "key",
					key: key.toString(),
					to: false,
					base: Arr.contains(saves.progress.keys ?? [], key),
					from,
				},
				"reset"
			);
		}
	});
	/** A chest's or door's record, only where the game's files name it so for this placement. */
	const putRecord = Effect.fnUntraced(function* (from: Origin, name: string) {
		const found = yield* data.find(name);
		if (Option.exists(found, (value) => value.named)) {
			yield* put(from, name, newGame, "reset");
		}
	});

	const writer: Writer = { held, current, put };
	const pouch = makePouch(data, own.tiers, items, writer);

	const childhood = Option.getOrThrowWith(
		read(saves.progress, Bun.hash.murmur32v3("Sequence_IsChildhood")),
		notOfThisGame("Sequence_IsChildhood")
	);
	const baseAge: Age = childhood === true ? "child" : "adult";
	const ageOf = (age: Age | undefined): Age => age ?? baseAge;

	/** The values a reset place's files name that a start state writes, steps aside. */
	const kept = Effect.fnUntraced(function* (names: readonly string[]) {
		const found = yield* Effect.filter(
			names,
			(name) =>
				Effect.map(data.find(name), (value) =>
					Option.exists(value, (each) => each.file === "progress")
				),
			{ concurrency: 1 }
		);
		return found;
	});
	/** The saved values the placements' modules and flows, and the flows named, name. */
	const namedBy = (
		placements: readonly Placement[],
		flows: readonly string[]
	) => Effect.flatMap(data.named(placements, flows), kept);
	const isStep = (name: string) =>
		Str.startsWith("Step_")(name) &&
		quests.has(Str.slice("Step_".length)(name));

	const resetPlace = Effect.fnUntraced(function* (
		from: Origin,
		placements: readonly Placement[],
		named: readonly string[],
		isOwn: (name: string) => boolean,
		stock: readonly Stock[]
	) {
		for (const placement of placements) {
			yield* putRecord(from, `IsTreasureBoxOpen.${placement.hash}`);
			yield* putRecord(from, `IsChainLockDoorOpen.${placement.hash}`);
			yield* putKey(from, BigInt(placement.hash));
			// A Gold Skulltula records its second key, its hash with bit 1 flipped, too.
			// biome-ignore lint/suspicious/noBitwiseOperators: the key is a bit of the hash flipped.
			yield* putKey(from, BigInt(placement.hash) ^ 2n);
		}
		for (const name of named) {
			if (isOwn(name) && !isStep(name)) {
				yield* put(from, name, newGame, "reset");
			} else {
				leftAlone.set(name, { name, from });
			}
		}
		for (const each of stock) {
			yield* put(
				from,
				`Shop_SoldNum.${each.keeper}.${each.slot}`,
				newGame,
				"reset"
			);
		}
	});

	const placedStock = (placements: readonly Placement[]) => {
		const actors = new Set(Arr.map(placements, (placement) => placement.actor));
		return Arr.filter(
			Arr.flatMap([...own.checks.values()], (check) => check.stock),
			(each) => actors.has(each.keeper)
		);
	};

	const resetCheck = Effect.fnUntraced(function* (from: Origin, check: Check) {
		const each = yield* Effect.forEach(
			check.placements,
			(hash) => data.placements({ hash }),
			{ concurrency: 1 }
		);
		const placements = Arr.flatten(each);
		const edits = planned.size;
		yield* resetPlace(from, placements, [], () => false, check.stock);
		if (planned.size === edits) {
			const named = yield* namedBy([], check.flows);
			return yield* StateRefused.make({
				reason: "incomplete",
				detail: `${check.name} has no record of its own in the tables${
					named.length === 0 ? "" : `; its flows name ${Arr.join(named, ", ")}`
				}. Name the one it needs with set.`,
			});
		}
	});

	const sceneOf = Effect.fnUntraced(function* (name: string) {
		const scene = Option.getOrUndefined(data.scene(name));
		if (scene === undefined) {
			const scenes = yield* data.names("scene");
			return yield* UnknownName.make({
				kind: "scene",
				asked: name,
				suggestions: nearest(name, scenes),
			});
		}
		return scene;
	});
	const roomOf = (scene: Scene, room: number) =>
		room < scene.rooms
			? Effect.succeed(room)
			: UnknownName.make({
					kind: "room",
					asked: `${scene.name}/${room}`,
					suggestions:
						scene.rooms === 0
							? [scene.name]
							: [`${scene.name}/0 to ${scene.name}/${scene.rooms - 1}`],
				});

	/** Refuses a start inside a grotto the plan or the base leaves shut. */
	const shut = (spot: StartSpot, scene: Scene) =>
		Effect.gen(function* () {
			const placements = yield* data.placements({ scene: scene.name });
			const named = (name: string) =>
				Arr.filter(placements, (placement) => placement.name === name);
			const floors = named(grotto.floor);
			for (const hole of named(grotto.hole)) {
				const floor = Arr.findFirst(
					floors,
					(each) => across(each.position, hole.position) <= onAnchor
				);
				const inside =
					across(spot.position, hole.position) <= grotto.across &&
					Option.exists(
						floor,
						(each) =>
							Math.abs(spot.position[1] - each.position[1]) <= grotto.height
					);
				const cover = Arr.findFirst(
					placements,
					(placement) =>
						isCover(placement.actor) &&
						across(placement.position, hole.position) <= onAnchor
				);
				if (
					inside &&
					Option.isSome(cover) &&
					!keyHeld(BigInt(cover.value.hash))
				) {
					return yield* StateRefused.make({
						reason: "shut",
						detail: `Link would stand inside the grotto under ${cover.value.actor} ${cover.value.hash}, which is shut. A save opens it with that cover's per-actor key set, which no word sets yet.`,
					});
				}
			}
		});

	/** The names a reset place put back, which are not left alone. */
	/** The values the plan writes, which it never says it left alone. */
	const written = () =>
		new Set(
			Arr.flatMapNullishOr([...planned.values()], (each) =>
				each.edit._tag === "key" ? undefined : each.edit.name
			)
		);

	return {
		result: (): Written => ({
			...(standing === undefined ? {} : { location: standing }),
			edits: Arr.map([...planned.values()], (each) => each.edit),
			leftAlone: Arr.sort(
				Arr.filter(
					[...leftAlone.values()],
					(left) => !written().has(left.name)
				),
				Order.mapInput(
					Order.String,
					(left: { readonly name: string }) => left.name
				)
			),
		}),

		/** The game writes over an autosave, so the built save is not one. */
		notAutosave: () =>
			put(
				{ _tag: "rule", rule: "the game writes over an autosave" },
				"CaptionData.IsAutoSave",
				() => false,
				"derived"
			),

		/**
		 * Each value the mod's new game starts with, where the base holds the game's own start; one that play moved in
		 * the base is left alone.
		 */
		startValues: Effect.fn("StartStates.startValues")(function* (
			mod: string,
			values: readonly StartValue[]
		) {
			const from: Origin = { _tag: "mod", mod };
			for (const { value, to, added } of values) {
				const { file } = value;
				if (file !== "progress" && file !== "caption") {
					continue;
				}
				if (added) {
					yield* place(
						`value ${value.hash}`,
						{
							_tag: "added",
							name: value.name,
							file,
							type: value.type,
							...(value.type === "Enum" ? { choices: value.choices } : {}),
							to,
							from,
						},
						"mod"
					);
					continue;
				}
				const base = held(value);
				if (Equal.equals(base, value.default)) {
					yield* put(from, value.name, () => to, "mod");
				} else if (!Equal.equals(base, to)) {
					leftAlone.set(value.name, { name: value.name, from });
				}
			}
		}),

		age: Effect.fn("StartStates.age")(function* (age: Age | undefined) {
			if (age === undefined) {
				return;
			}
			const from: Origin = { _tag: "word", word: "age", named: age };
			yield* put(
				from,
				"Sequence_IsChildhood",
				() => age === "child",
				"written"
			);
			if (baseAge === age) {
				return;
			}
			for (const slot of ["Weapon", "Shield"]) {
				const now = `PlayerStatus.Equip${slot}`;
				const other = `PlayerStatus.AnotherEraEquip${slot}`;
				const [nowValue, otherValue] = yield* Effect.all(
					[data.value(now), data.value(other)],
					{
						concurrency: 1,
					}
				).pipe(Effect.orDie);
				yield* put(from, now, () => held(otherValue), "derived");
				yield* put(from, other, () => held(nowValue), "derived");
			}
		}),

		items: Effect.fn("StartStates.items")(function* (
			withItems: StartState["withItems"] & {},
			withoutItems: readonly string[]
		) {
			const asked = Arr.map(withItems, (holding) =>
				typeof holding === "string" ? holding : holding.item
			);
			const both = Arr.intersection(asked, withoutItems);
			if (Arr.isReadonlyArrayNonEmpty(both)) {
				return yield* StateRefused.make({
					reason: "contradiction",
					detail: `${Arr.join(both, ", ")} is in both withItems and withoutItems.`,
				});
			}
			for (const name of withoutItems) {
				const item = yield* data.item(name);
				yield* pouch.withoutItem(item, {
					_tag: "word",
					word: "withoutItems",
					named: name,
				});
			}
			for (const holding of withItems) {
				const name = typeof holding === "string" ? holding : holding.item;
				const count = typeof holding === "string" ? undefined : holding.count;
				const item = yield* data.item(name);
				yield* pouch.withItem(
					item,
					count,
					asked,
					{ _tag: "word", word: "withItems", named: name },
					"written"
				);
			}
		}),

		equipped: Effect.fn("StartStates.equipped")(function* (
			names: readonly string[],
			age: Age | undefined,
			withoutItems: readonly string[]
		) {
			for (const name of names) {
				if (Arr.contains(withoutItems, name)) {
					return yield* StateRefused.make({
						reason: "contradiction",
						detail: `${name} is both equipped and in withoutItems.`,
					});
				}
				const item = yield* data.item(name);
				const got = yield* data.find(`IsGet.${item.savedActor}`);
				if (!Option.exists(got, (value) => current(value) === true)) {
					return yield* StateRefused.make({
						reason: "incomplete",
						detail: `${name} is equipped but not held; add it to withItems.`,
					});
				}
				const slot = Option.getOrUndefined(
					slotOf(item, rowOf(items, item), ageOf(age))
				);
				if (slot === undefined) {
					return yield* StateRefused.make({
						reason: "no-slot",
						detail: `${name} goes in no slot and on no button.`,
					});
				}
				yield* put(
					{ _tag: "word", word: "equipped", named: name },
					slot.value,
					() => slot.holds,
					"written"
				);
			}
		}),

		reset: Effect.fn("StartStates.reset")(function* (subject: string) {
			const from: Origin = { _tag: "word", word: "reset", named: subject };
			const check = own.checks.get(subject);
			const inRoom = sceneRoom.exec(subject)?.groups;
			const scene = Option.getOrUndefined(data.scene(subject));
			const placed = yield* data.placements({ actor: subject });
			const kinds = [
				check,
				inRoom,
				scene,
				Arr.isReadonlyArrayNonEmpty(placed) ? placed : undefined,
			];
			const found = Arr.filter(kinds, (kind) => kind !== undefined).length;
			if (found > 1) {
				return yield* StateRefused.make({
					reason: "ambiguous",
					detail: `${subject} names ${found} places: a check, a room, a scene or a placed actor.`,
				});
			}
			if (check !== undefined) {
				return yield* resetCheck(from, check);
			}
			if (inRoom?.scene !== undefined) {
				const roomScene = yield* sceneOf(inRoom.scene);
				const room = yield* roomOf(roomScene, Number(inRoom.room));
				const placements = yield* data.placements({
					scene: roomScene.name,
					room,
				});
				const named = yield* namedBy(placements, []);
				return yield* resetPlace(
					from,
					placements,
					named,
					() => true,
					placedStock(placements)
				);
			}
			if (scene !== undefined) {
				const placements = yield* data.placements({ scene: scene.name });
				const named = yield* namedBy(placements, []);
				const roomValues = yield* namedBy(
					Arr.filter(placements, (placement) => placement.room !== undefined),
					[]
				);
				const rooms = new Set(roomValues);
				const name = [scene.name, scene.folder];
				return yield* resetPlace(
					from,
					placements,
					named,
					(value) => rooms.has(value) || carries(value, name),
					placedStock(placements)
				);
			}
			if (Arr.isReadonlyArrayNonEmpty(placed)) {
				const named = yield* namedBy(placed, [subject]);
				return yield* resetPlace(
					from,
					placed,
					named,
					(value) => carries(value, [subject]),
					[]
				);
			}
			const scenes = yield* data.names("scene");
			return yield* UnknownName.make({
				kind: "subject",
				asked: subject,
				suggestions: nearest(subject, [...scenes, ...own.checks.keys()]),
			});
		}),

		story: Effect.fn("StartStates.story")(function* (
			story: StartState["story"]
		) {
			for (const [quest, step] of Rec.toEntries(story?.quests ?? {})) {
				const found = yield* data.find(`Step_${quest}`);
				if (Option.isNone(found)) {
					return yield* UnknownName.make({
						kind: "quest",
						asked: quest,
						suggestions: nearest(quest, [...quests]),
					});
				}
				if (!Arr.contains(found.value.choices, step)) {
					return yield* UnknownName.make({
						kind: "step",
						asked: step,
						suggestions: nearest(step, found.value.choices),
					});
				}
				yield* put(
					{ _tag: "word", word: "story", named: quest },
					`Step_${quest}`,
					() => step,
					"written"
				);
			}
			for (const event of story?.events ?? []) {
				const name = `HavePlayedEvent.${event}`;
				const found = yield* data.find(name);
				if (Option.isNone(found)) {
					const values = yield* data.names("value");
					return yield* UnknownName.make({
						kind: "event",
						asked: event,
						suggestions: nearest(
							event,
							Arr.flatMapNullishOr(values, (value) =>
								Str.startsWith("HavePlayedEvent.")(value)
									? Str.slice("HavePlayedEvent.".length)(value)
									: undefined
							)
						),
					});
				}
				yield* put(
					{ _tag: "word", word: "story", named: event },
					name,
					() => true,
					"written"
				);
			}
		}),

		location: Effect.fn("StartStates.location")(function* (
			location: StartState["location"] & {},
			age: Age | undefined
		) {
			const spot = yield* spotOf(data, own, spots, location, ageOf(age));
			const scene = yield* sceneOf(spot.scene);
			if (spot.room !== undefined) {
				yield* roomOf(scene, spot.room);
			}
			const other = ageOf(age) === "child" ? "_Adult" : "_Child";
			if (Str.includes(other)(scene.name)) {
				return yield* StateRefused.make({
					reason: "contradiction",
					detail: `A ${ageOf(age)} cannot stand in ${scene.name}.`,
				});
			}
			yield* shut(spot, scene);
			standing = spot;
			const from: Origin =
				typeof location === "string"
					? { _tag: "spot", name: location, kind: spot.kind }
					: { _tag: "word", word: "location", named: spot.scene };
			const facing = direction(spot.facing);
			const writes: readonly (readonly [string, Saved])[] = [
				["Sequence_CurrentBanc", scene.name],
				[unnamed.dungeon, scene.dungeon],
				["DungeonRoom", spot.room ?? (scene.dungeon === "" ? -1 : 0)],
				["PlayerStatus.SavePos", spot.position],
				[unnamed.lastPosition, spot.position],
				["PlayerStatus.SavePosRadY", savedFacing(spot.facing)],
				[`IsVisitLocation.${scene.folder}`, true],
				...(scene.outdoors
					? ([
							["Player_Pos_For_Map", spot.position],
							["Player_Rot_For_Map", facing],
						] as const)
					: []),
				["CaptionData.LocationName", scene.folder],
				[unnamed.captionLabel, `${scene.folder}/${scene.name}`],
				["CaptionData.Pos", spot.position],
				["CaptionData.Dir", facing],
			];
			for (const [name, value] of writes) {
				yield* put(from, name, () => value, "derived");
			}
		}),

		set: Effect.fn("StartStates.set")(function* (
			values: Readonly<Record<string, Value>>
		) {
			for (const [name, value] of Rec.toEntries(values)) {
				const entry = yield* data.value(name);
				if (entry.file !== "progress" && entry.file !== "caption") {
					return yield* StateRefused.make({
						reason: "unsaved",
						detail: `${name} is kept in neither progress.sav nor caption.sav, so only a running game can set it.`,
					});
				}
				yield* fits(entry, value);
				yield* put(
					{ _tag: "word", word: "set", named: name },
					name,
					() => value,
					"written"
				);
			}
		}),

		/** A count of ammunition needs something to hold it, as many as it holds. */
		counts: Effect.fn("StartStates.counts")(function* (
			withItems: StartState["withItems"] & {}
		) {
			for (const holding of withItems) {
				if (typeof holding === "string") {
					continue;
				}
				const item = yield* data.item(holding.item);
				const holder = Option.getOrUndefined(pouch.holderOf(item));
				if (holder === undefined) {
					continue;
				}
				const found = yield* data.find(holder.limit);
				const limit = Option.match(found, { onNone: () => 0, onSome: current });
				if (typeof limit === "number" && holding.count > limit) {
					return yield* StateRefused.make({
						reason: "incomplete",
						detail: `${holding.count} of ${holding.item} need room for them, and ${holder.limit} is ${limit}; add ${holder.bag ?? "a container"} to withItems.`,
					});
				}
			}
		}),
	};
};

/** Refuses a value its entry cannot hold, by the rule the running game's values follow. */
const fits = (entry: GameValue, value: Value) => {
	switch (entry.type) {
		case "Enum":
			return encodeValue({ ...entry, type: "Enum" }, value);
		case "Bool":
		case "Int":
		case "UInt":
		case "Float":
		case "String64":
			return encodeValue(
				{ name: entry.name, hash: entry.hash, type: entry.type },
				value
			);
		default:
			return Effect.fail(
				ValueRefused.make({
					value: entry.name,
					given: value,
					holds: `a ${entry.type}, which set does not write`,
				})
			);
	}
};

const spotOf = Effect.fn("StartStates.spot")(function* (
	data: GameData["Service"],
	own: OwnTables["Service"],
	spots: Spots["Service"],
	location: StartState["location"] & {},
	age: Age
) {
	if (typeof location !== "string") {
		const given: StartSpot = {
			...location,
			kind: "given",
			about: "where the state places Link",
		};
		return given;
	}
	const found: StartSpot[] = [];
	const check = own.checks.get(location);
	const kept = yield* spots.all;
	const named = Arr.filter(kept, (each) => each.name === location);
	// A spot of the other age stands only for a name no check has, and the scene's age then refuses it.
	const forAge = yield* spots.find(location, age);
	const spot = Option.orElse(forAge, () =>
		check === undefined ? Arr.head(named) : Option.none()
	);
	if (Option.isSome(spot)) {
		found.push({
			scene: spot.value.scene,
			...(spot.value.room === undefined ? {} : { room: spot.value.room }),
			position: spot.value.position,
			facing: spot.value.facing,
			kind: kindOf(spot.value),
			about: aboutSpot(spot.value),
		});
	} else if (check !== undefined) {
		if (!Arr.isReadonlyArrayNonEmpty(check.placements)) {
			return yield* StateRefused.make({
				reason: "no-place",
				detail: `${location} has no place of its own: no actor in the game's files stands for it, so no spot before it can be worked out. Add a row for it to tables/spots.tsv, with where Link stands to be given it or to find it.`,
			});
		}
		const placed = yield* placedFor(data, check, age);
		const [subject, ...others] = placed;
		if (subject === undefined || others.length > 0) {
			return yield* StateRefused.make({
				reason: "ambiguous",
				detail: `${location} is placed ${placed.length} times for ${age === "child" ? "a child" : "an adult"}, so Link's spot before it is not one.`,
			});
		}
		found.push({
			...workedOut(subject),
			kind: "worked out",
			about: workedOutAbout,
		});
	}
	const landing = Option.getOrUndefined(data.landing(location));
	if (landing !== undefined) {
		found.push({
			scene: landing.scene,
			position: landing.position,
			facing: degrees(landing.yaw),
			kind: "landing",
			about: `the game's landing ${landing.name}`,
		});
	}
	const [only, ...more] = found;
	if (only === undefined) {
		const landings = yield* data.names("landing");
		return yield* UnknownName.make({
			kind: "spot",
			asked: location,
			suggestions: nearest(location, [
				...Arr.map(kept, (each) => each.name),
				...own.checks.keys(),
				...landings,
			]),
		});
	}
	if (more.length > 0) {
		return yield* StateRefused.make({
			reason: "ambiguous",
			detail: `${location} names ${found.length} spots: a spot or a check, and a landing.`,
		});
	}
	return only;
});

/** One unit before a placement's actor, on the side it faces, facing it. */
const workedOut = (subject: Placement) => ({
	scene: subject.scene,
	...(subject.room === undefined ? {} : { room: subject.room }),
	position: [
		subject.position[0] + before * Math.sin(subject.yaw),
		subject.position[1] + lift,
		subject.position[2] + before * Math.cos(subject.yaw),
	] as const,
	facing: (degrees(subject.yaw) + 180) % 360,
});
