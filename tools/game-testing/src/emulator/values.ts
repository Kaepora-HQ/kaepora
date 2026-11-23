/**
 * Saved values by name in the running game's memory. The game's program loads at an address that differs by machine
 * and boot, so `attach` finds it first. The game data manager is `[[program + 0x4C7E180]]`. It holds one table per
 * type, 0x60 bytes apart from `manager + 0x8`, each with its entry count at +0x8, its entries at +0x10, a map of
 * (hash, index) pairs at +0x18 and the map's capacity at +0x24. A scalar's saved copy follows its current value.
 *
 * The layout is that of build `E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348`, the one build the patcher accepts.
 */
import { Array as Arr, Effect, Option } from "effect";
import type { Value } from "../contract/values.ts";
import type { GameValue } from "../data/game-data.ts";
import type { WriteUnsupported } from "../errors.ts";
import { ValueNotFound, ValueRefused, ValueUnreadable } from "../errors.ts";
import type { Emulator, EmulatorError } from "./emulator.ts";

const programBytes = Uint8Array.fromHex("010000005c70c4039070c403");
const programFrom = 0x80_00_00_00n;
const programTo = 0x82_00_00_00n;

const managerGlobal = 0x4c_7e_18_0n;

/**
 * The largest table, Bool, holds 11,292 values in the game data list. A map that claims more than 2^20 pairs means
 * the layout read is not this build's.
 */
const mapLimit = 2 ** 20;

/** A saved value as the game data list gives it: its name, its name's murmur3 hash, its type and an Enum's choices. */
export type ValueEntry = { readonly name: string; readonly hash: number } & (
	| { readonly type: "Bool" | "Int" | "UInt" | "Float" | "String64" }
	| { readonly type: "Enum"; readonly choices: readonly string[] }
);

export interface Values {
	/** Where the game's program starts in guest memory this boot. */
	readonly program: bigint;
	readonly read: (
		entry: ValueEntry
	) => Effect.Effect<Value, EmulatorError | ValueNotFound>;
	/**
	 * Bools by name, read together in a few requests: one span of the Bool table covers them. A name the running game
	 * does not hold is left out of the answer.
	 */
	readonly readFlags: (
		names: readonly string[]
	) => Effect.Effect<ReadonlyMap<string, boolean>, EmulatorError>;
	/** A Vector3 of the game data list by name, such as `PlayerStatus.CurrentPos`, which no save need hold. */
	readonly readPoint: (
		name: string
	) => Effect.Effect<
		readonly [number, number, number],
		EmulatorError | ValueNotFound
	>;
	/** Writes the value, and a scalar's saved copy, so the game saves what was set. */
	readonly write: (
		entry: ValueEntry,
		value: Value
	) => Effect.Effect<
		void,
		EmulatorError | ValueNotFound | ValueRefused | WriteUnsupported
	>;
}

/** Where a type's table sits in the manager, how long an entry is, and where its value sits in an entry. */
interface Layout {
	readonly at: number;
	readonly slot: number;
	readonly stride: number;
}

/**
 * A Vector3's layout, measured on the game: its table holds the list's 26 Vector3 values, and `PlayerStatus.CurrentPos`
 * read there follows Link as he walks.
 */
const vector3: Layout = { slot: 10, stride: 0x40, at: 0x24 };

/** How one type is held: its table's slot, the size of an entry and where the value sits in it, and its bytes. */
interface Codec extends Layout {
	/** Whether the saved copy follows the value. */
	readonly copied: boolean;
	readonly decode: (view: DataView) => Option.Option<Value>;
	readonly encode: (value: Value) => Option.Option<Uint8Array>;
	/** What a value of the type is, for the sentence that refuses one that does not fit. */
	readonly holds: string;
	readonly size: number;
}

const filled = (size: number, fill: (view: DataView) => void) => {
	const bytes = new Uint8Array(size);
	fill(new DataView(bytes.buffer));
	return bytes;
};

const viewOf = (bytes: Uint8Array) =>
	new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

const isNumberIn = (value: Value, low: number, high: number) =>
	Number.isInteger(value) && Number(value) >= low && Number(value) <= high;

const word = {
	stride: 0x28,
	at: 0x1c,
	size: 4,
	copied: true,
} as const;

const codecOf = (entry: ValueEntry): Codec => {
	switch (entry.type) {
		case "Bool":
			return {
				slot: 0,
				stride: 0x18,
				at: 0x16,
				size: 1,
				copied: true,
				holds: "true or false",
				decode: (view) => Option.some(view.getUint8(0) !== 0),
				encode: (value) =>
					typeof value === "boolean"
						? Option.some(filled(1, (view) => view.setUint8(0, value ? 1 : 0)))
						: Option.none(),
			};
		case "Int":
			return {
				...word,
				slot: 2,
				holds: "a whole number from -2147483648 to 2147483647",
				decode: (view) => Option.some(view.getInt32(0, true)),
				encode: (value) =>
					isNumberIn(value, -(2 ** 31), 2 ** 31 - 1)
						? Option.some(
								filled(4, (view) => view.setInt32(0, Number(value), true))
							)
						: Option.none(),
			};
		case "Float":
			return {
				...word,
				slot: 4,
				holds: "a number",
				decode: (view) => Option.some(view.getFloat32(0, true)),
				encode: (value) =>
					typeof value === "number" && Number.isFinite(Math.fround(value))
						? Option.some(filled(4, (view) => view.setFloat32(0, value, true)))
						: Option.none(),
			};
		case "Enum": {
			const { choices } = entry;
			return {
				...word,
				slot: 6,
				stride: 0x38,
				holds: `one of ${Arr.join(choices, ", ")}`,
				decode: (view) => {
					const held = view.getUint32(0, true);
					return Arr.findFirst(
						choices,
						(choice) => Bun.hash.murmur32v3(choice) === held
					);
				},
				encode: (value) =>
					typeof value === "string" && Arr.contains(choices, value)
						? Option.some(
								filled(4, (view) =>
									view.setUint32(0, Bun.hash.murmur32v3(value), true)
								)
							)
						: Option.none(),
			};
		}
		case "String64":
			return {
				slot: 16,
				stride: 0x1_08,
				at: 0x74,
				size: 64,
				copied: false,
				holds: "text of at most 63 bytes",
				decode: (view) => {
					const bytes = new Uint8Array(view.buffer, view.byteOffset, 64);
					const end = bytes.indexOf(0);
					return Option.some(
						new TextDecoder().decode(bytes.subarray(0, end === -1 ? 64 : end))
					);
				},
				encode: (value) => {
					const text =
						typeof value === "string"
							? new TextEncoder().encode(value)
							: undefined;
					if (text === undefined || text.length >= 64) {
						return Option.none();
					}
					const bytes = new Uint8Array(64);
					bytes.set(text);
					return Option.some(bytes);
				},
			};
		case "UInt":
			return {
				...word,
				slot: 20,
				holds: "a whole number from 0 to 4294967295",
				decode: (view) => Option.some(view.getUint32(0, true)),
				encode: (value) =>
					isNumberIn(value, 0, 2 ** 32 - 1)
						? Option.some(
								filled(4, (view) => view.setUint32(0, Number(value), true))
							)
						: Option.none(),
			};
		default:
			return entry satisfies never;
	}
};

/**
 * The value's bytes as this entry holds them, or `ValueRefused` when it does not fit, as with a number for a Bool. A
 * check or a wait refuses an expected value with it before it reads.
 */
export const encodeValue = (entry: ValueEntry, value: Value) => {
	const codec = codecOf(entry);
	return codec.encode(value).pipe(
		Effect.fromOption,
		Effect.mapError(() =>
			ValueRefused.make({ value: entry.name, given: value, holds: codec.holds })
		)
	);
};

const readable = ["Bool", "Int", "UInt", "Float", "Enum", "String64"] as const;

/** How the game's memory holds a value of the game data list, or `ValueUnreadable` for a type it is not read for. */
export const entryOf = (value: GameValue) => {
	const type = Arr.findFirst(readable, (each) => each === value.type);
	if (Option.isNone(type)) {
		return Effect.fail(
			ValueUnreadable.make({ value: value.name, type: value.type })
		);
	}
	const entry: ValueEntry =
		type.value === "Enum"
			? {
					name: value.name,
					hash: value.hash,
					type: "Enum",
					choices: value.choices,
				}
			: { name: value.name, hash: value.hash, type: type.value };
	return Effect.succeed(entry);
};

export const attach = Effect.fn("Values.attach")(function* (
	emulator: Emulator
) {
	const hits = yield* emulator.searchMemory({
		pattern: programBytes,
		from: programFrom,
		to: programTo,
		maxHits: 2,
	});
	const [program, ...others] = hits;
	if (program === undefined || others.length > 0) {
		return yield* Effect.die(
			new Error(
				`The game's program was found ${hits.length} times in guest memory, not once. Lichii runs another build of the game, or none yet.`
			)
		);
	}
	const global = program + managerGlobal;
	const indexes = new Map<string, { manager: bigint; index: number }>();
	/** Each Bool's index by its hash, for the manager the map was read from. */
	let flagMap:
		| { readonly manager: bigint; readonly byHash: Map<number, number> }
		| undefined;

	const u64 = (address: bigint) =>
		Effect.map(emulator.readMemory(address, 8), (bytes) =>
			viewOf(bytes).getBigUint64(0, true)
		);

	const indexOf = Effect.fnUntraced(function* (
		entry: {
			readonly name: string;
			readonly hash: number;
			readonly type: string;
		},
		manager: bigint,
		head: DataView
	) {
		const key = `${entry.type} ${entry.hash}`;
		const known = indexes.get(key);
		if (known?.manager === manager) {
			return known.index;
		}
		const count = head.getUint32(0x8, true);
		const capacity = head.getUint32(0x24, true);
		if (count === 0 || capacity === 0) {
			return yield* ValueNotFound.make({ value: entry.name });
		}
		if (capacity > mapLimit) {
			return yield* Effect.die(
				new Error(
					`The ${entry.type} table's map claims ${capacity} pairs, more than any table of this build has.`
				)
			);
		}
		const pairs = yield* emulator.readMemory(
			head.getBigUint64(0x18, true),
			capacity * 8
		);
		const map = viewOf(pairs);
		const index = Arr.findFirst(
			Arr.range(0, capacity - 1),
			(pair) =>
				map.getUint32(pair * 8, true) === entry.hash &&
				map.getUint32(pair * 8 + 4, true) < count
		).pipe(Option.map((pair) => map.getUint32(pair * 8 + 4, true)));
		if (Option.isNone(index)) {
			return yield* ValueNotFound.make({ value: entry.name });
		}
		indexes.set(key, { manager, index: index.value });
		return index.value;
	});

	const addressOf = Effect.fnUntraced(function* (
		entry: {
			readonly name: string;
			readonly hash: number;
			readonly type: string;
		},
		codec: Layout
	) {
		const slot = yield* u64(global);
		const manager = yield* u64(slot);
		const table = manager + 0x8n + 0x60n * BigInt(codec.slot);
		const tableHead = yield* emulator.readMemory(table, 0x28);
		const head = viewOf(tableHead);
		const index = yield* indexOf(entry, manager, head);
		return (
			head.getBigUint64(0x10, true) + BigInt(index * codec.stride + codec.at)
		);
	});

	const values: Values = {
		program,
		read: Effect.fn("Values.read")(function* (entry) {
			const codec = codecOf(entry);
			const address = yield* addressOf(entry, codec);
			const bytes = yield* emulator.readMemory(address, codec.size);
			const value = codec.decode(viewOf(bytes));
			if (Option.isNone(value)) {
				return yield* Effect.die(
					new Error(
						`${entry.name} holds a value that none of its choices hashes to.`
					)
				);
			}
			return value.value;
		}),
		readFlags: Effect.fn("Values.readFlags")(function* (names) {
			const flag = codecOf({ name: "", hash: 0, type: "Bool" });
			const slot = yield* u64(global);
			const manager = yield* u64(slot);
			const tableHead = yield* emulator.readMemory(manager + 0x8n, 0x28);
			const head = viewOf(tableHead);
			const count = head.getUint32(0x8, true);
			const capacity = head.getUint32(0x24, true);
			if (count === 0 || capacity === 0 || capacity > mapLimit) {
				return new Map<string, boolean>();
			}
			if (flagMap?.manager !== manager) {
				const pairs = yield* emulator.readMemory(
					head.getBigUint64(0x18, true),
					capacity * 8
				);
				const map = viewOf(pairs);
				const byHash = new Map<number, number>();
				for (const pair of Arr.range(0, capacity - 1)) {
					const index = map.getUint32(pair * 8 + 4, true);
					if (index < count) {
						byHash.set(map.getUint32(pair * 8, true), index);
					}
				}
				flagMap = { manager, byHash };
			}
			const { byHash } = flagMap;
			const held = Arr.flatMapNullishOr(names, (name) => {
				const index = byHash.get(Bun.hash.murmur32v3(name));
				return index === undefined ? undefined : ([name, index] as const);
			});
			if (!Arr.isReadonlyArrayNonEmpty(held)) {
				return new Map<string, boolean>();
			}
			const first = Math.min(...Arr.map(held, ([, index]) => index));
			const last = Math.max(...Arr.map(held, ([, index]) => index));
			const span = yield* emulator.readMemory(
				head.getBigUint64(0x10, true) + BigInt(first * flag.stride),
				(last - first + 1) * flag.stride
			);
			return new Map(
				Arr.map(held, ([name, index]) => [
					name,
					span[(index - first) * flag.stride + flag.at] !== 0,
				])
			);
		}),
		readPoint: Effect.fn("Values.readPoint")(function* (name) {
			const address = yield* addressOf(
				{ name, hash: Bun.hash.murmur32v3(name), type: "Vector3" },
				vector3
			);
			const bytes = yield* emulator.readMemory(address, 12);
			const view = viewOf(bytes);
			return [
				view.getFloat32(0, true),
				view.getFloat32(4, true),
				view.getFloat32(8, true),
			] as const;
		}),
		write: Effect.fn("Values.write")(function* (entry, value) {
			const bytes = yield* encodeValue(entry, value);
			const codec = codecOf(entry);
			const address = yield* addressOf(entry, codec);
			yield* emulator.writeMemory(
				address,
				codec.copied ? new Uint8Array([...bytes, ...bytes]) : bytes
			);
		}),
	};
	return values;
});
