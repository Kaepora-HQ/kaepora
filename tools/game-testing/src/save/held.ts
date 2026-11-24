/** What a save holds that both the start states and the bases read: the values no string names, the age and angles. */
import { Option } from "effect";
import type { Age } from "../contract/state.ts";
import { valueHash } from "../data/game-data.ts";
import type { SaveFile } from "./codec.ts";
import { read } from "./codec.ts";

/** Values of the save no string of the game names, found by comparing saves the game wrote. */
export const unnamed = {
	/** The scene's `DungeonParam` entry, empty outside one. */
	dungeon: "0x4a6bdb7d",
	/** Follows `PlayerStatus.SavePos`. */
	lastPosition: "0x8773f02d",
	/** The caption's `<folder>/<scene>`. */
	captionLabel: "0x0cccc6cd",
} as const;

export const degrees = (angle: number) => (angle * 180) / Math.PI;
export const radians = (angle: number) => (angle * Math.PI) / 180;

/** Link's age in a progress.sav; none in a file that holds no `Sequence_IsChildhood`. */
export const ageIn = (save: SaveFile): Option.Option<Age> =>
	Option.map(read(save, valueHash("Sequence_IsChildhood")), (childhood) =>
		childhood === true ? "child" : "adult"
	);
