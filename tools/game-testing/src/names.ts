/**
 * On a machine with the game, `game-testing names` writes `generated/names.d.ts`, which fills
 * `Register` from the dump of the game's tables, so a script's names and value types are checked in the editor and by
 * the check `run` makes before a game starts. The generated file holds names read from Nintendo's files, so it is
 * ignored by git and never reaches CI. Without it every name is a `string` and every value a `Value`, and the game
 * machine refuses an unknown name when the command reaches it.
 */
import type { Value } from "./contract/values.ts";

/** `generated/names.d.ts` adds `values`, `items`, `quests`, `events`, `landings`, `scenes` and `checks`. */
// biome-ignore lint/suspicious/noEmptyInterface: augmented by the generated declaration file.
export interface Register {}

type Registered<Key extends string, Fallback> = Register extends {
	readonly [K in Key]: infer Names;
}
	? Names
	: Fallback;

/** Every saved value, by its name in the game data list, with the type its record holds. */
export type Values = Registered<"values", Readonly<Record<string, Value>>>;

/** A value no string in the game's files names, by its murmur3 hash, such as `0xe753508b`. */
export type RawHash = `0x${string}`;

/** A saved value's name, such as `IsGet.Weapon_Range_Bow` or `PlayerStatus.CurrentRupee`, or its raw hash. */
export type ValueName = (keyof Values & string) | RawHash;

/** What reading `Name` gives: `boolean` for a Bool, `number` for an Int or Float, an enum's choices as a union. */
export type ValueOf<Name extends ValueName> = Name extends keyof Values
	? Values[Name]
	: Value;

/** A remake item by the name plans and `remake/items.tsv` use, such as `Item_Range_Bow`. */
export type ItemName = Registered<"items", string>;

/** A quest of `RSDB/Challenge` with its steps, such as `GatheringTokenGold` with `CollectToken`. */
export type Quests = Registered<"quests", Readonly<Record<string, string>>>;

/** An event setting of `RSDB/EventSetting`. */
export type EventName = Registered<"events", string>;

/** A landing of `StartPos`, such as `KokiriForest/Mido_House_Front`. */
export type LandingName = Registered<"landings", string>;

/** A scene of `RSDB/GameBancInfo`, such as `ForestTemple`. */
export type SceneName = Registered<"scenes", string>;

/** A check by its spoiler log name, such as `KF Mido Top Left Chest`, from Kaepora's tables under `remake/`. */
export type CheckName = Registered<"checks", string>;
