import { describe, expect, it } from "@voila.dev/effect-bun-test";
import { Array as Arr, Effect, Option } from "effect";
import { SaveUnreadable } from "../src/errors.ts";
import type { SaveFile } from "../src/save/codec.ts";
import { decode, encode, read, write } from "../src/save/codec.ts";

const hash = (name: string) => Bun.hash.murmur32v3(name);

const padded = (text: string, size: number) => {
	const bytes = new Uint8Array(size);
	bytes.set(new TextEncoder().encode(text));
	return bytes;
};

const floats = (...values: number[]) =>
	new Uint8Array(new Float32Array(values).buffer);

const words = (...values: number[]) =>
	new Uint8Array(new Uint32Array(values).buffer);

const kind = 0xa3_db_71_14;

const save: SaveFile = {
	version: 0x2d_80_eb,
	kind,
	records: [
		{ type: "Bool", hash: hash("Fake.IsOpen"), word: 1 },
		{ type: "Bool", hash: hash("Fake.IsShut"), word: 0 },
		{ type: "Int", hash: hash("Fake.Count"), word: 2 ** 32 - 5 },
		{ type: "IntArray", hash: hash("Fake.Counts"), bytes: words(2, 7, 9) },
		{ type: "Float", hash: hash("Fake.Speed"), word: 0x3f_c0_00_00 },
		{ type: "Enum", hash: hash("Fake.Mode"), word: hash("Swim") },
		{ type: "Vector3", hash: hash("Fake.Spot"), bytes: floats(1, -2, 3.5) },
		{
			type: "String64",
			hash: hash("Fake.Label"),
			bytes: padded("Kokiri Forest", 64),
		},
		{ type: "Binary", hash: hash("Fake.Picture"), bytes: words(4, 0xff_d8) },
		{ type: "UInt", hash: hash("Fake.Total"), word: 4_000_000_000 },
	],
	keys: [5n, 9n],
};

describe("the save file", () => {
	it.effect("a save decodes and encodes to the same bytes", () =>
		Effect.gen(function* () {
			const bytes = encode(save);
			const decoded = yield* decode("progress.sav", bytes);
			expect(decoded).toEqual(save);
			expect(encode(decoded)).toEqual(bytes);
		})
	);

	it("lays records out as the game does: every group's header, the data region, the key list last", () => {
		const bytes = encode({
			version: 0x2d_80_eb,
			kind,
			records: [
				{ type: "Bool", hash: 0x11, word: 1 },
				{ type: "String32", hash: 0x22, bytes: padded("Field", 32) },
			],
			keys: [3n],
		});
		const view = new DataView(bytes.buffer, bytes.byteOffset);
		const word = (at: number) => view.getUint32(at, true);
		const dataStart = 0x28 + 8 * (2 + 32 + 1);
		expect([word(0), word(4), word(8)]).toEqual([
			0x01_02_03_04,
			0x2d_80_eb,
			dataStart,
		]);
		expect([word(0x28), word(0x2c), word(0x30), word(0x34)]).toEqual([
			0x11, 1, 0, 1,
		]);
		const string32 = 0x28 + 8 * 15;
		expect([word(string32 - 8), word(string32 - 4)]).toEqual([0, 14]);
		expect([word(string32), word(string32 + 4)]).toEqual([0x22, dataStart]);
		expect([word(dataStart - 8), word(dataStart - 4)]).toEqual([
			kind,
			dataStart + 32,
		]);
		expect(view.getBigUint64(dataStart + 32, true)).toBe(3n);
		expect(view.getBigUint64(dataStart + 40, true)).toBe(0n);
		expect(bytes.length).toBe(dataStart + 48);
	});

	it.effect(
		"values written by hash read back with their types, and the key list stays sorted",
		() =>
			Effect.gen(function* () {
				const edited = yield* write(
					save,
					[
						{ hash: hash("Fake.IsShut"), value: true },
						{ hash: hash("Fake.Count"), value: 12 },
						{ hash: hash("Fake.Speed"), value: -0.25 },
						{ hash: hash("Fake.Mode"), value: "Climb" },
						{ hash: hash("Fake.Spot"), value: [4, 5, -6] },
						{ hash: hash("Fake.Label"), value: "Lost Woods" },
						{ hash: hash("Fake.Total"), value: 7 },
					],
					[
						{ key: 7n, set: true },
						{ key: 9n, set: false },
					]
				);
				const decoded = yield* decode("progress.sav", encode(edited));
				const values = Option.all(
					Arr.map(
						[
							"Fake.IsOpen",
							"Fake.IsShut",
							"Fake.Count",
							"Fake.Speed",
							"Fake.Mode",
							"Fake.Spot",
							"Fake.Label",
							"Fake.Total",
						],
						(name) => read(decoded, hash(name))
					)
				);
				expect(values).toEqual(
					Option.some([
						true,
						true,
						12,
						-0.25,
						hash("Climb"),
						[4, 5, -6],
						"Lost Woods",
						7,
					])
				);
				expect(decoded.keys).toEqual([5n, 7n]);
			})
	);

	it.effect(
		"a save cut short is refused, also one with no key list to end it, as a caption has none",
		() =>
			Effect.gen(function* () {
				const bytes = encode({ ...save, keys: undefined });
				const whole = yield* decode("caption.sav", bytes);
				expect(whole.records).toHaveLength(save.records.length);
				for (const cut of [bytes.length - 1, bytes.length - 4]) {
					const refused = yield* Effect.flip(
						decode("caption.sav", bytes.subarray(0, cut))
					);
					expect(refused).toBeInstanceOf(SaveUnreadable);
				}
			})
	);

	it.effect("a file that is not a save is refused as unreadable", () =>
		Effect.gen(function* () {
			const refused = yield* Effect.flip(
				decode("option.sav", new Uint8Array(40))
			);
			expect(refused).toBeInstanceOf(SaveUnreadable);
			expect(refused.path).toBe("option.sav");
		})
	);
});
