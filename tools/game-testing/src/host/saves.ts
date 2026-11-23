/**
 * The saves a place's game writes during a run. Each slot is looked at after each step, and after each press of a step
 * that presses until something holds. A slot is kept once both its files are of one new write: each differs from what
 * the slot held when last kept, and each decodes as a whole save. A file the game is still writing does not decode, and
 * the game writes both files at each save, so a new `progress.sav` beside the old `caption.sav` waits for the next look.
 */
import type { FileSystem } from "effect";
import { Array as Arr, Effect, Option, Order, String as Str } from "effect";
import { decode, read } from "../save/codec.ts";
import type { Saves } from "../save/state.ts";
import { sha256 } from "../sha256.ts";
import { builtSlot } from "./boot.ts";
import type { Recorder } from "./runs.ts";

interface Held {
	readonly caption: string;
	readonly progress: string;
}

/**
 * The look that keeps each save the game wrote in `folder` since the last look, in the order the game wrote them, with
 * the presses of the running step made by then. The save built for the run is in its slot from the start, and is kept
 * only once the game writes over it.
 */
export const watchSaves = (
	fs: FileSystem.FileSystem,
	folder: string,
	built: Saves,
	keep: Recorder["save"]
) => {
	const seen = new Map<string, Held>([
		[
			builtSlot,
			{ progress: sha256(built.progress), caption: sha256(built.caption) },
		],
	]);
	const slotOf = Effect.fnUntraced(function* (slot: string) {
		const files = yield* Effect.all(
			{
				progress: fs.readFile(`${folder}/${slot}/progress.sav`),
				caption: fs.readFile(`${folder}/${slot}/caption.sav`),
				written: fs.stat(`${folder}/${slot}/progress.sav`),
			},
			{ concurrency: 1 }
		).pipe(Effect.option);
		if (Option.isNone(files)) {
			return Option.none();
		}
		const { progress, caption, written } = files.value;
		const held = { progress: sha256(progress), caption: sha256(caption) };
		const last = seen.get(slot);
		if (
			last !== undefined &&
			(last.progress === held.progress || last.caption === held.caption)
		) {
			return Option.none();
		}
		const shown = yield* Effect.option(decode("caption.sav", caption));
		const whole = yield* Effect.option(decode("progress.sav", progress));
		if (Option.isNone(shown) || Option.isNone(whole)) {
			return Option.none();
		}
		const autosave = read(
			shown.value,
			Bun.hash.murmur32v3("CaptionData.IsAutoSave")
		);
		return Option.some({
			slot,
			held,
			progress,
			caption,
			kind: Option.contains(autosave, true)
				? ("autosave" as const)
				: ("menu" as const),
			written: Option.match(written.mtime, {
				onNone: () => 0,
				onSome: (at) => at.getTime(),
			}),
		});
	});
	return (afterPress?: number) =>
		Effect.gen(function* () {
			const slots = yield* Effect.orDie(fs.readDirectory(folder));
			const looked = yield* Effect.forEach(
				Arr.filter(slots, Str.startsWith("slot_")),
				slotOf,
				{ concurrency: 1 }
			);
			const changed = Arr.sort(
				Arr.getSomes(looked),
				Order.mapInput(
					Order.Number,
					(each: { readonly written: number }) => each.written
				)
			);
			for (const { slot, held, kind, progress, caption } of changed) {
				seen.set(slot, held);
				yield* keep({
					kind,
					slot,
					progress,
					caption,
					...(afterPress === undefined ? {} : { afterPress }),
				});
			}
		});
};
