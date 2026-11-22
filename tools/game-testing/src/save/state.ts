import {
	Array as Arr,
	Context,
	Effect,
	Equal,
	FileSystem,
	Layer,
	Option,
	Order,
	Record as Rec,
	String as Str,
} from "effect";
import { UserDirectories } from "effect/persistence";
import type { PlannedEdit, StatePlan } from "../contract/rpc.ts";
import type { Age, StartState } from "../contract/state.ts";
import type { Saved, Value } from "../contract/values.ts";
import type {
	GameValue,
	Item,
	Placement,
	Point,
	Scene,
} from "../data/game-data.ts";
import { GameData, nearest, valueHash } from "../data/game-data.ts";
import { encodeValue } from "../emulator/values.ts";
import {
	SaveUnreadable,
	StateRefused,
	UnknownName,
	ValueRefused,
} from "../errors.ts";
import type { SaveFile } from "./codec.ts";
import { decode, encode, read, write } from "./codec.ts";
import type { Weight, Writer } from "./pouch.ts";
import { makePouch, rowOf, slotOf } from "./pouch.ts";
import type { Base, Check, Stock } from "./tables.ts";
import { OwnTables } from "./tables.ts";

export interface Plan extends StatePlan {
	readonly base: Base;
}

export interface Saves {
	readonly caption: Uint8Array;
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
	fresh: 0,
	derived: 1,
	written: 2,
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

const sha256 = (bytes: Uint8Array) =>
	new Bun.CryptoHasher("sha256").update(bytes).digest("hex");

/** Whether a name carries a place's name, as the game spells it in either case. */
const carries = (name: string, place: readonly string[]) =>
	Arr.some(place, (part) =>
		Str.includes(Str.toLowerCase(part))(Str.toLowerCase(name))
	);

export class StartStates extends Context.Service<
	StartStates,
	{
		readonly plan: (
			state: StartState
		) => Effect.Effect<
			Plan,
			UnknownName | StateRefused | ValueRefused | SaveUnreadable
		>;
		readonly build: (plan: Plan) => Effect.Effect<Saves, SaveUnreadable>;
	}
>()("game-testing/save/state/StartStates") {
	static readonly layer = Layer.effect(
		StartStates,
		Effect.gen(function* () {
			const data = yield* GameData;
			const own = yield* OwnTables;
			const fs = yield* FileSystem.FileSystem;
			const directories = yield* UserDirectories.UserDirectories;
			const home = yield* directories.data("game-testing").pipe(Effect.orDie);
			const items = new Map(Arr.map(data.items, (item) => [item.name, item]));
			const questNames = yield* data.names("quest");
			const quests = new Set(questNames);

			const load = Effect.fn("StartStates.load")(function* (
				base: Base,
				file: "progress" | "caption"
			) {
				const path = `${home}/bases/${base.name}/${file}.sav`;
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
				return yield* decode(path, bytes);
			});

			const plan = Effect.fn("StartStates.plan")(function* (state: StartState) {
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
				const words = makeWords({ data, own, items, quests, saves });
				yield* words.notAutosave();
				yield* words.age(state.age);
				yield* words.items(state.has ?? [], state.lacks ?? []);
				yield* words.equip(state.equip ?? [], state.age, state.lacks ?? []);
				yield* Effect.forEach(state.fresh ?? [], words.fresh, {
					concurrency: 1,
					discard: true,
				});
				yield* words.story(state.story);
				if (state.at !== undefined) {
					yield* words.at(state.at, state.age);
				}
				yield* words.set(state.set ?? {});
				yield* words.counts(state.has ?? []);
				const planned: Plan = { base, ...words.result() };
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
}

/** The words of one state, writing into one plan. */
const makeWords = ({ data, own, items, quests, saves }: Inputs) => {
	const planned = new Map<
		string,
		{ readonly edit: PlannedEdit; readonly weight: Weight }
	>();
	const leftAlone = new Map<
		string,
		{ readonly name: string; readonly from: string }
	>();

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
			const name = edit._tag === "value" ? edit.name : `key ${edit.key}`;
			return yield* StateRefused.make({
				reason: "contradiction",
				detail: `${old.edit.from} writes ${name} as ${String(old.edit.to)}, and ${edit.from} as ${String(edit.to)}.`,
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
	const keyHeld = (key: bigint) => {
		const edit = planned.get(`key ${key}`)?.edit;
		return edit === undefined
			? Arr.contains(saves.progress.keys ?? [], key)
			: edit.to === true;
	};

	/** Writes a value a start state's files keep; a name that names none writes nothing. */
	const put = Effect.fn("StartStates.put")(function* (
		from: string,
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
	const putKey = Effect.fnUntraced(function* (from: string, key: bigint) {
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
				"fresh"
			);
		}
	});
	/** A chest's or door's record, only where the game's files name it so for this placement. */
	const putRecord = Effect.fnUntraced(function* (from: string, name: string) {
		const found = yield* data.find(name);
		if (Option.exists(found, (value) => value.named)) {
			yield* put(from, name, (value) => value.default, "fresh");
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

	/** The values a fresh place's files name that a start state writes, steps aside. */
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

	const freshPlace = Effect.fnUntraced(function* (
		from: string,
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
				yield* put(from, name, (value) => value.default, "fresh");
			} else {
				leftAlone.set(name, { name, from });
			}
		}
		for (const each of stock) {
			yield* put(
				from,
				`Shop_SoldNum.${each.keeper}.${each.slot}`,
				(value) => value.default,
				"fresh"
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

	const freshCheck = Effect.fnUntraced(function* (from: string, check: Check) {
		const each = yield* Effect.forEach(
			check.placements,
			(hash) => data.placements({ hash }),
			{ concurrency: 1 }
		);
		const placements = Arr.flatten(each);
		const edits = planned.size;
		yield* freshPlace(from, placements, [], () => false, check.stock);
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
	const shut = (spot: Standing, scene: Scene) =>
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

	/** The names a fresh place reset, which are not left alone. */
	const resetByFresh = () =>
		new Set(
			Arr.flatMapNullishOr([...planned.values()], (each) =>
				each.weight === "fresh" && each.edit._tag === "value"
					? each.edit.name
					: undefined
			)
		);

	return {
		result: (): StatePlan => ({
			edits: Arr.map([...planned.values()], (each) => each.edit),
			leftAlone: Arr.sort(
				Arr.filter(
					[...leftAlone.values()],
					(left) => !resetByFresh().has(left.name)
				),
				Order.mapInput(
					Order.String,
					(left: { readonly name: string }) => left.name
				)
			),
		}),

		/** The game writes over an autosave, so the built save is not one. */
		notAutosave: () =>
			put("base", "CaptionData.IsAutoSave", () => false, "derived"),

		age: Effect.fn("StartStates.age")(function* (age: Age | undefined) {
			if (age === undefined) {
				return;
			}
			const from = `age ${age}`;
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
			has: StartState["has"] & {},
			lacks: readonly string[]
		) {
			const asked = Arr.map(has, (holding) =>
				typeof holding === "string" ? holding : holding.item
			);
			const both = Arr.intersection(asked, lacks);
			if (Arr.isReadonlyArrayNonEmpty(both)) {
				return yield* StateRefused.make({
					reason: "contradiction",
					detail: `${Arr.join(both, ", ")} is both had and lacked.`,
				});
			}
			for (const name of lacks) {
				const item = yield* data.item(name);
				yield* pouch.lacks(item, `lacks ${name}`);
			}
			for (const holding of has) {
				const name = typeof holding === "string" ? holding : holding.item;
				const count = typeof holding === "string" ? undefined : holding.count;
				const item = yield* data.item(name);
				yield* pouch.has(item, count, asked, `has ${name}`, "written");
			}
		}),

		equip: Effect.fn("StartStates.equip")(function* (
			names: readonly string[],
			age: Age | undefined,
			lacks: readonly string[]
		) {
			for (const name of names) {
				if (Arr.contains(lacks, name)) {
					return yield* StateRefused.make({
						reason: "contradiction",
						detail: `${name} is both equipped and lacked.`,
					});
				}
				const item = yield* data.item(name);
				const got = yield* data.find(`IsGet.${item.savedActor}`);
				if (!Option.exists(got, (value) => current(value) === true)) {
					return yield* StateRefused.make({
						reason: "incomplete",
						detail: `${name} is equipped but not held; add it to has.`,
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
				yield* put(`equip ${name}`, slot.value, () => slot.holds, "written");
			}
		}),

		fresh: Effect.fn("StartStates.fresh")(function* (subject: string) {
			const from = `fresh ${subject}`;
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
				return yield* freshCheck(from, check);
			}
			if (inRoom?.scene !== undefined) {
				const roomScene = yield* sceneOf(inRoom.scene);
				const room = yield* roomOf(roomScene, Number(inRoom.room));
				const placements = yield* data.placements({
					scene: roomScene.name,
					room,
				});
				const named = yield* namedBy(placements, []);
				return yield* freshPlace(
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
				return yield* freshPlace(
					from,
					placements,
					named,
					(value) => rooms.has(value) || carries(value, name),
					placedStock(placements)
				);
			}
			if (Arr.isReadonlyArrayNonEmpty(placed)) {
				const named = yield* namedBy(placed, [subject]);
				return yield* freshPlace(
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
				yield* put(`story ${quest}`, `Step_${quest}`, () => step, "written");
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
				yield* put(`story ${event}`, name, () => true, "written");
			}
		}),

		at: Effect.fn("StartStates.at")(function* (
			at: StartState["at"] & {},
			age: Age | undefined
		) {
			const spot = yield* spotOf(data, own, at, ageOf(age));
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
			const from = `at ${typeof at === "string" ? at : spot.scene}`;
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
				yield* put(`set ${name}`, name, () => value, "written");
			}
		}),

		/** A count of ammunition needs something to hold it, as many as it holds. */
		counts: Effect.fn("StartStates.counts")(function* (
			has: StartState["has"] & {}
		) {
			for (const holding of has) {
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
						detail: `${holding.count} of ${holding.item} need room for them, and ${holder.limit} is ${limit}; add has ${holder.bag ?? "a container"}.`,
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

interface Standing {
	readonly facing: number;
	readonly position: Point;
	readonly room: number | undefined;
	readonly scene: string;
}

const spotOf = Effect.fn("StartStates.spot")(function* (
	data: GameData["Service"],
	own: OwnTables["Service"],
	at: StartState["at"] & {},
	age: Age
) {
	if (typeof at !== "string") {
		const exact: Standing = {
			scene: at.scene,
			room: at.room,
			position: at.position,
			facing: at.facing,
		};
		return exact;
	}
	const found: Standing[] = [];
	const spot = own.spots.get(at);
	if (spot !== undefined) {
		found.push(spot);
	}
	const check = own.checks.get(at);
	if (check !== undefined && Arr.isReadonlyArrayNonEmpty(check.placements)) {
		const each = yield* Effect.forEach(
			check.placements,
			(hash) => data.placements({ hash }),
			{
				concurrency: 1,
			}
		);
		const other = age === "child" ? "_Adult" : "_Child";
		const forAge = Arr.filter(
			Arr.flatten(each),
			(placement) => !Str.includes(other)(placement.scene)
		);
		const [subject, ...others] = forAge;
		if (subject === undefined || others.length > 0) {
			return yield* StateRefused.make({
				reason: "ambiguous",
				detail: `${at} is placed ${forAge.length} times for ${age === "child" ? "a child" : "an adult"}, so Link's spot before it is not one.`,
			});
		}
		found.push({
			scene: subject.scene,
			room: subject.room,
			position: [
				subject.position[0] + before * Math.sin(subject.yaw),
				subject.position[1] + lift,
				subject.position[2] + before * Math.cos(subject.yaw),
			],
			facing: degrees(subject.yaw) + 180,
		});
	}
	const landing = Option.getOrUndefined(data.landing(at));
	if (landing !== undefined) {
		found.push({
			scene: landing.scene,
			room: undefined,
			position: landing.position,
			facing: degrees(landing.yaw),
		});
	}
	const [only, ...more] = found;
	if (only === undefined) {
		const landings = yield* data.names("landing");
		return yield* UnknownName.make({
			kind: "spot",
			asked: at,
			suggestions: nearest(at, [
				...own.spots.keys(),
				...own.checks.keys(),
				...landings,
			]),
		});
	}
	if (more.length > 0) {
		return yield* StateRefused.make({
			reason: "ambiguous",
			detail: `${at} names ${found.length} spots: a row of tables/spots.tsv, a check or a landing.`,
		});
	}
	return only;
});
