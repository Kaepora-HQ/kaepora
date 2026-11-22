import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	Cause,
	Effect,
	Exit,
	Record as Rec,
	String as Str,
} from "effect";
import { connect } from "../src/emulator/emulator.ts";
import type { ValueEntry } from "../src/emulator/values.ts";
import { attach } from "../src/emulator/values.ts";
import {
	ValueNotFound,
	ValueRefused,
	WriteUnsupported,
} from "../src/errors.ts";
import type { FakeValue } from "./fake-lichii.ts";
import { fakeLichii } from "./fake-lichii.ts";

const modes = ["Walk", "Swim", "Climb"];

const game = {
	"Fake.IsOpen": { type: "Bool", value: true },
	"Fake.Count": { type: "Int", value: -5 },
	"Fake.Total": { type: "UInt", value: 4_000_000_000 },
	"Fake.Speed": { type: "Float", value: 1.5 },
	"Fake.Mode": { type: "Enum", value: "Swim", choices: modes },
	"Fake.Label": { type: "String64", value: "Kokiri Forest" },
} satisfies Record<string, FakeValue>;

const entry = (name: keyof typeof game): ValueEntry => {
	const { type } = game[name];
	const hash = Bun.hash.murmur32v3(name);
	return type === "Enum"
		? { name, hash, type, choices: modes }
		: { name, hash, type };
};

const start = (options?: {
	readonly values?: Readonly<Record<string, FakeValue>>;
	readonly writes?: boolean;
	readonly mapCapacity?: number;
}) =>
	Effect.gen(function* () {
		const lichii = yield* fakeLichii({ values: game, ...options });
		const emulator = yield* connect({
			path: lichii.path,
			wire: { line: () => Effect.void },
			logFileTail: Effect.succeed([]),
		});
		return { lichii, values: yield* attach(emulator) };
	});

describe("saved values by name", () => {
	it.effect("a value read by name gives the game's value, for each type", () =>
		Effect.gen(function* () {
			const { values } = yield* start();
			const read = yield* Effect.forEach(
				Rec.keys(game),
				(name) => values.read(entry(name)),
				{ concurrency: 1 }
			);
			expect(read).toEqual([
				true,
				-5,
				4_000_000_000,
				1.5,
				"Swim",
				"Kokiri Forest",
			]);
		})
	);

	it.effect("set writes each value and the copy the game saves", () =>
		Effect.gen(function* () {
			const { lichii, values } = yield* start();
			const wanted = {
				"Fake.IsOpen": false,
				"Fake.Count": 12,
				"Fake.Total": 7,
				"Fake.Speed": -0.25,
				"Fake.Mode": "Climb",
			} as const;
			yield* Effect.forEach(
				Rec.toEntries(wanted),
				([name, value]) => values.write(entry(name), value),
				{ concurrency: 1 }
			);
			yield* values.write(entry("Fake.Label"), "Lost Woods");
			for (const [name, value] of Rec.toEntries(wanted)) {
				expect([
					yield* lichii.value(name),
					yield* lichii.savedCopy(name),
				]).toEqual([value, value]);
			}
			expect(yield* lichii.value("Fake.Label")).toBe("Lost Woods");
		})
	);

	it.effect("set on a Lichii without write_memory says it needs the fork", () =>
		Effect.gen(function* () {
			const { values } = yield* start({ writes: false });
			expect(yield* Effect.flip(values.write(entry("Fake.Count"), 1))).toEqual(
				WriteUnsupported.make({})
			);
		})
	);

	it.effect(
		"a name the running game does not hold fails with ValueNotFound",
		() =>
			Effect.gen(function* () {
				const { values } = yield* start();
				const missing: ValueEntry = {
					name: "Fake.Missing",
					hash: Bun.hash.murmur32v3("Fake.Missing"),
					type: "Int",
				};
				expect(yield* Effect.flip(values.read(missing))).toEqual(
					ValueNotFound.make({ value: "Fake.Missing" })
				);
			})
	);
	it.effect("a value past the first 64 KiB of its table's map is found", () =>
		Effect.gen(function* () {
			const flags = Rec.fromEntries(
				Arr.map(Arr.range(1, 5000), (n): [string, FakeValue] => [
					`Fake.Flag${n}`,
					{ type: "Bool", value: false },
				])
			);
			const { values } = yield* start({
				values: { ...flags, "Fake.Last": { type: "Bool", value: true } },
			});
			const last: ValueEntry = {
				name: "Fake.Last",
				hash: Bun.hash.murmur32v3("Fake.Last"),
				type: "Bool",
			};
			expect(yield* values.read(last)).toBe(true);
		})
	);

	it.effect("the same hash asked for as another type is not found", () =>
		Effect.gen(function* () {
			const { values } = yield* start();
			expect(yield* values.read(entry("Fake.Mode"))).toBe("Swim");
			const asInt: ValueEntry = {
				...entry("Fake.Count"),
				name: "Fake.Mode",
				hash: Bun.hash.murmur32v3("Fake.Mode"),
			};
			expect(yield* Effect.flip(values.read(asInt))).toEqual(
				ValueNotFound.make({ value: "Fake.Mode" })
			);
		})
	);

	it.effect(
		"a value that does not fit its entry is refused and nothing is written",
		() =>
			Effect.gen(function* () {
				const { lichii, values } = yield* start();
				const tooLong = Str.repeat(64)("x");
				const refusals = yield* Effect.forEach(
					[
						[entry("Fake.Count"), true],
						[entry("Fake.Count"), 2 ** 31],
						[entry("Fake.Mode"), "Run"],
						[entry("Fake.Label"), tooLong],
					] as const,
					([held, given]) => Effect.flip(values.write(held, given)),
					{ concurrency: 1 }
				);
				const int = "a whole number from -2147483648 to 2147483647";
				expect(refusals).toEqual([
					ValueRefused.make({ value: "Fake.Count", given: true, holds: int }),
					ValueRefused.make({
						value: "Fake.Count",
						given: 2 ** 31,
						holds: int,
					}),
					ValueRefused.make({
						value: "Fake.Mode",
						given: "Run",
						holds: "one of Walk, Swim, Climb",
					}),
					ValueRefused.make({
						value: "Fake.Label",
						given: tooLong,
						holds: "text of at most 63 bytes",
					}),
				]);
				expect(
					Arr.filter(
						yield* lichii.requests,
						(request) => request.method === "write_memory"
					)
				).toEqual([]);
			})
	);

	it.effect("a type whose table is empty holds no value", () =>
		Effect.gen(function* () {
			const { values } = yield* start({ values: {} });
			expect(yield* Effect.flip(values.read(entry("Fake.Count")))).toEqual(
				ValueNotFound.make({ value: "Fake.Count" })
			);
		})
	);

	it.effect("a table whose map is larger than any game's fails loudly", () =>
		Effect.gen(function* () {
			const { values } = yield* start({ mapCapacity: 2 ** 21 });
			const exit = yield* Effect.exit(values.read(entry("Fake.Count")));
			expect(Exit.isFailure(exit) && Cause.hasDies(exit.cause)).toBe(true);
		})
	);
});
