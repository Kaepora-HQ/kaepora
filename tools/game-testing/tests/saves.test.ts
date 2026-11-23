import { BunFileSystem } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import { Effect, FileSystem, Option } from "effect";
import { watchSaves } from "../src/host/saves.ts";
import { decode, encode, read } from "../src/save/codec.ts";

/** A save file whose one Int holds `count`, so two counts are two different writes. */
const file = (count: number) =>
	encode({
		version: 0x2d_80_eb,
		kind: 0,
		records: [
			{
				type: "Bool",
				hash: Bun.hash.murmur32v3("CaptionData.IsAutoSave"),
				word: 1,
			},
			{ type: "Int", hash: Bun.hash.murmur32v3("Fake.Count"), word: count },
		],
		keys: undefined,
	});

describe("the saves a run keeps", () => {
	it.effect(
		"a slot is kept once both its files are of one new write, never a new progress.sav beside an old caption.sav",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const folder = yield* fs.makeTempDirectoryScoped();
				const built = { progress: file(0), caption: file(0), option: file(0) };
				const kept: (readonly [unknown, unknown])[] = [];
				const count = (bytes: Uint8Array) =>
					Effect.map(Effect.orDie(decode("save", bytes)), (save) =>
						Option.getOrUndefined(read(save, Bun.hash.murmur32v3("Fake.Count")))
					);
				const look = watchSaves(fs, folder, built, (save) =>
					Effect.gen(function* () {
						kept.push([
							yield* count(save.progress),
							yield* count(save.caption),
						]);
					})
				);
				const write = (part: "progress" | "caption", written: number) =>
					Effect.andThen(
						fs.makeDirectory(`${folder}/slot_01`, { recursive: true }),
						fs.writeFile(`${folder}/slot_01/${part}.sav`, file(written))
					);
				yield* write("progress", 1);
				yield* write("caption", 1);
				yield* look();
				yield* write("progress", 2);
				yield* look();
				yield* write("caption", 2);
				yield* look();
				expect(kept).toEqual([
					[1, 1],
					[2, 2],
				]);
			}).pipe(Effect.scoped, Effect.provide(BunFileSystem.layer))
	);
});
