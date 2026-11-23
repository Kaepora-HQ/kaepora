/**
 * The game's save files (`progress.sav`, `caption.sav`, `option.sav`), as the game writes them:
 *
 * - `0x00` the magic `0x01020304`, `0x04` a version word, `0x08` where the data region starts, then zeros to `0x28`;
 * - from `0x28` to the data region, 8-byte records `(hash, value)`, grouped by type in `recordTypes` order. The Bool
 *   group comes first and has no header; every later group starts with `(0, its index)`, even an empty one;
 * - Bool, Int, Float, Enum and UInt records hold their value; every other record holds where its bytes start in the
 *   data region, which holds them back to back in record order;
 * - the last record is `(the file's SaveTypeHash, where the key list starts)`, or 0 for a file with no key list. The
 *   key list holds the per-actor keys that are set, sorted, as u64s ending in a zero, and ends the file;
 * - each record's bytes are as long as its type says: a fixed size, or a u32 count then that many elements, or for
 *   Binary a u32 length then that many bytes. Over 400 saves the game wrote, 62,000 records held to it (measured), so a
 *   file cut short, such as one the game is still writing, does not decode.
 *
 * Measured on saves the game wrote.
 */
import { Array as Arr, Effect, Option, Order } from "effect";
import { SaveUnreadable } from "../errors.ts";

export const recordTypes = [
	"Bool",
	"BoolArray",
	"Int",
	"IntArray",
	"Float",
	"FloatArray",
	"Enum",
	"EnumArray",
	"Vector2",
	"Vector2Array",
	"Vector3",
	"Vector3Array",
	"String16",
	"String16Array",
	"String32",
	"String32Array",
	"String64",
	"String64Array",
	"Binary",
	"BinaryArray",
	"UInt",
	"UIntArray",
	"Int64",
	"Int64Array",
	"UInt64",
	"UInt64Array",
	"WString16",
	"WString16Array",
	"WString32",
	"WString32Array",
	"WString64",
	"WString64Array",
] as const;
export type RecordType = (typeof recordTypes)[number];

type InlineType = "Bool" | "Int" | "Float" | "Enum" | "UInt";
type BlobType = Exclude<RecordType, InlineType>;

export type SaveRecord =
	| { readonly type: InlineType; readonly hash: number; readonly word: number }
	| {
			readonly type: BlobType;
			readonly hash: number;
			readonly bytes: Uint8Array;
	  };

export interface SaveFile {
	readonly keys: readonly bigint[] | undefined;
	readonly kind: number;
	readonly records: readonly SaveRecord[];
	readonly version: number;
}

export type Scalar =
	| boolean
	| number
	| string
	| readonly [number, number, number];

interface Codec<Raw> {
	readonly read: (raw: Raw) => Scalar;
	readonly write: (value: Scalar) => Option.Option<Raw>;
}

const magic = 0x01_02_03_04;
const tableStart = 0x28;
const keyGroup = recordTypes.length;

const viewOf = (bytes: Uint8Array) =>
	new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

const isIntIn = (value: Scalar, low: number, high: number): value is number =>
	typeof value === "number" &&
	Number.isInteger(value) &&
	value >= low &&
	value <= high;

const unsigned: Codec<number> = {
	read: (word) => word,
	write: (value) =>
		isIntIn(value, 0, 2 ** 32 - 1) ? Option.some(value) : Option.none(),
};

const words: Record<InlineType, Codec<number>> = {
	Bool: {
		read: (word) => word !== 0,
		write: (value) =>
			typeof value === "boolean" ? Option.some(value ? 1 : 0) : Option.none(),
	},
	Int: {
		read: (word) => (word < 2 ** 31 ? word : word - 2 ** 32),
		write: (value) =>
			isIntIn(value, -(2 ** 31), 2 ** 31 - 1)
				? Option.some(value < 0 ? value + 2 ** 32 : value)
				: Option.none(),
	},
	Float: {
		read: (word) =>
			new Float32Array(new Uint32Array([word]).buffer)[0] as number,
		write: (value) =>
			typeof value === "number" && Number.isFinite(Math.fround(value))
				? Option.some(
						new Uint32Array(new Float32Array([value]).buffer)[0] as number
					)
				: Option.none(),
	},
	/** The record holds the murmur3 of its choice's name. */
	Enum: {
		read: unsigned.read,
		write: (value) =>
			typeof value === "string"
				? Option.some(Bun.hash.murmur32v3(value))
				: unsigned.write(value),
	},
	UInt: unsigned,
};

const text = (size: number): Codec<Uint8Array> => ({
	read: (bytes) => {
		const end = bytes.indexOf(0);
		return new TextDecoder().decode(bytes.subarray(0, end === -1 ? size : end));
	},
	write: (value) => {
		const encoded =
			typeof value === "string" ? new TextEncoder().encode(value) : undefined;
		if (encoded === undefined || encoded.length >= size) {
			return Option.none();
		}
		const bytes = new Uint8Array(size);
		bytes.set(encoded);
		return Option.some(bytes);
	},
});

const blobs: Partial<Record<BlobType, Codec<Uint8Array>>> = {
	String16: text(16),
	String32: text(32),
	String64: text(64),
	Vector3: {
		read: (bytes) => {
			const view = viewOf(bytes);
			return [
				view.getFloat32(0, true),
				view.getFloat32(4, true),
				view.getFloat32(8, true),
			];
		},
		write: (value) =>
			typeof value === "object" &&
			Arr.every(value, (axis) => Number.isFinite(Math.fround(axis)))
				? Option.some(new Uint8Array(new Float32Array(value).buffer))
				: Option.none(),
	},
};

interface Table {
	readonly keysAt: number;
	readonly kind: number;
	readonly records: readonly {
		readonly type: RecordType;
		readonly hash: number;
		readonly value: number;
	}[];
}

const readTable = Effect.fnUntraced(function* (
	path: string,
	view: DataView,
	dataStart: number
) {
	const records: Table["records"][number][] = [];
	let group = 0;
	for (let at = tableStart; at < dataStart; at += 8) {
		const hash = view.getUint32(at, true);
		const value = view.getUint32(at + 4, true);
		if (group === keyGroup) {
			if (at + 8 !== dataStart) {
				return yield* SaveUnreadable.make({
					path,
					detail: `a record follows the last one at ${at}`,
				});
			}
			return { kind: hash, keysAt: value, records } satisfies Table;
		}
		if (hash !== 0) {
			records.push({ type: recordTypes[group] as RecordType, hash, value });
		} else if (value === group + 1) {
			group = value;
		} else {
			return yield* SaveUnreadable.make({
				path,
				detail: `group ${value} follows group ${group}`,
			});
		}
	}
	return yield* SaveUnreadable.make({ path, detail: "it has no last record" });
});

const readKeys = Effect.fnUntraced(function* (
	path: string,
	view: DataView,
	keysAt: number
) {
	const keys: bigint[] = [];
	let at = keysAt;
	while (at + 8 <= view.byteLength && view.getBigUint64(at, true) !== 0n) {
		keys.push(view.getBigUint64(at, true));
		at += 8;
	}
	if (at + 8 !== view.byteLength) {
		return yield* SaveUnreadable.make({
			path,
			detail: "its key list does not end the file",
		});
	}
	if (!Arr.every(keys, (key, i) => i === 0 || key > (keys[i - 1] as bigint))) {
		return yield* SaveUnreadable.make({
			path,
			detail: "its key list is not sorted",
		});
	}
	return keys;
});

const fixedSizes: Partial<Record<BlobType, number>> = {
	Vector2: 8,
	Vector3: 12,
	String16: 16,
	String32: 32,
	String64: 64,
	Int64: 8,
	UInt64: 8,
	WString16: 32,
	WString32: 64,
	WString64: 128,
};
const elementSizes: Partial<Record<BlobType, number>> = {
	IntArray: 4,
	UIntArray: 4,
	FloatArray: 4,
	EnumArray: 4,
	Int64Array: 8,
	UInt64Array: 8,
	Vector2Array: 8,
	Vector3Array: 12,
	String16Array: 16,
	String32Array: 32,
	String64Array: 64,
	WString16Array: 32,
	WString32Array: 64,
	WString64Array: 128,
};

/** How long a record's bytes are by its type, read from its count or length; none for a BinaryArray, whose is unknown. */
const sizeOf = (type: BlobType, bytes: Uint8Array): Option.Option<number> => {
	const fixed = fixedSizes[type];
	if (fixed !== undefined) {
		return Option.some(fixed);
	}
	if (bytes.length < 4) {
		return Option.some(4);
	}
	const count = viewOf(bytes).getUint32(0, true);
	const element = elementSizes[type];
	if (element !== undefined) {
		return Option.some(4 + count * element);
	}
	switch (type) {
		case "BoolArray":
			return Option.some(4 + Math.ceil(count / 32) * 4);
		case "Binary":
			return Option.some(4 + count);
		default:
			return Option.none();
	}
};

export const decode = Effect.fn("SaveFile.decode")(function* (
	path: string,
	bytes: Uint8Array
) {
	const view = viewOf(bytes);
	if (bytes.length < tableStart || view.getUint32(0, true) !== magic) {
		return yield* SaveUnreadable.make({
			path,
			detail: "it does not start with a save's header",
		});
	}
	const dataStart = view.getUint32(8, true);
	if (dataStart > bytes.length || (dataStart - tableStart) % 8 !== 0) {
		return yield* SaveUnreadable.make({
			path,
			detail: `its data region starts at ${dataStart}`,
		});
	}
	const table = yield* readTable(path, view, dataStart);
	const dataEnd = table.keysAt === 0 ? bytes.length : table.keysAt;
	const starts = [
		...Arr.flatMap(table.records, (record) =>
			record.type in words ? [] : [record.value]
		),
		dataEnd,
	];
	const inOrder = Arr.every(
		starts,
		(start, i) => start >= (starts[i - 1] ?? dataStart)
	);
	if (!inOrder || starts[0] !== dataStart || dataEnd > bytes.length) {
		return yield* SaveUnreadable.make({
			path,
			detail: "its data region does not hold its records in order",
		});
	}
	let blob = 0;
	const records = Arr.map(
		table.records,
		({ type, hash, value }): SaveRecord => {
			if (type in words) {
				return { type: type as InlineType, hash, word: value };
			}
			blob += 1;
			return {
				type: type as BlobType,
				hash,
				bytes: bytes.slice(value, starts[blob]),
			};
		}
	);
	const cut = Arr.findFirst(
		records,
		(record) =>
			"bytes" in record &&
			Option.exists(
				sizeOf(record.type, record.bytes),
				(size) => size !== record.bytes.length
			)
	);
	if (Option.isSome(cut)) {
		return yield* SaveUnreadable.make({
			path,
			detail: `its record ${cut.value.hash} is not as long as its type says, as in a file cut short`,
		});
	}
	const keys =
		table.keysAt === 0 ? undefined : yield* readKeys(path, view, table.keysAt);
	const file: SaveFile = {
		version: view.getUint32(4, true),
		kind: table.kind,
		records,
		keys,
	};
	return file;
});

export const encode = (save: SaveFile): Uint8Array => {
	const groups = Arr.map(recordTypes, (type) =>
		Arr.filter(save.records, (record) => record.type === type)
	);
	const groupHeaders = recordTypes.length - 1;
	const keyHeader = 1;
	const lastRecord = 1;
	const dataStart =
		tableStart +
		8 * (save.records.length + groupHeaders + keyHeader + lastRecord);
	const dataSize = Arr.reduce(save.records, 0, (size, record) =>
		"bytes" in record ? size + record.bytes.length : size
	);
	const keysAt = dataStart + dataSize;
	const keys = save.keys ?? [];
	const bytes = new Uint8Array(
		keysAt + (save.keys === undefined ? 0 : 8 * (keys.length + 1))
	);
	const view = viewOf(bytes);
	view.setUint32(0, magic, true);
	view.setUint32(4, save.version, true);
	view.setUint32(8, dataStart, true);
	let at = tableStart;
	let data = dataStart;
	const entry = (hash: number, value: number) => {
		view.setUint32(at, hash, true);
		view.setUint32(at + 4, value, true);
		at += 8;
	};
	for (const [index, group] of groups.entries()) {
		if (index > 0) {
			entry(0, index);
		}
		for (const record of group) {
			if ("bytes" in record) {
				entry(record.hash, data);
				bytes.set(record.bytes, data);
				data += record.bytes.length;
			} else {
				entry(record.hash, record.word);
			}
		}
	}
	entry(0, keyGroup);
	entry(save.kind, save.keys === undefined ? 0 : keysAt);
	for (const [index, key] of keys.entries()) {
		view.setBigUint64(keysAt + 8 * index, key, true);
	}
	return bytes;
};

export const read = (save: SaveFile, hash: number): Option.Option<Scalar> =>
	Arr.findFirst(save.records, (record) => record.hash === hash).pipe(
		Option.flatMap((record) =>
			"word" in record
				? Option.some(words[record.type].read(record.word))
				: Option.map(Option.fromNullishOr(blobs[record.type]), (codec) =>
						codec.read(record.bytes)
					)
		)
	);

const rewritten = (
	record: SaveRecord,
	value: Scalar
): Option.Option<SaveRecord> =>
	"word" in record
		? Option.map(words[record.type].write(value), (word) => ({
				...record,
				word,
			}))
		: Option.fromNullishOr(blobs[record.type]).pipe(
				Option.flatMap((codec) => codec.write(value)),
				Option.map((bytes) => ({ ...record, bytes }))
			);

export const write = Effect.fn("SaveFile.write")(function* (
	save: SaveFile,
	values: readonly { readonly hash: number; readonly value: Scalar }[],
	keys: readonly { readonly key: bigint; readonly set: boolean }[]
) {
	const wanted = new Map(Arr.map(values, (edit) => [edit.hash, edit.value]));
	const records: SaveRecord[] = [];
	for (const record of save.records) {
		const value = wanted.get(record.hash);
		const next =
			value === undefined ? Option.some(record) : rewritten(record, value);
		if (Option.isNone(next)) {
			return yield* Effect.die(
				new Error(
					`${String(value)} does not fit record 0x${record.hash.toString(16)}, a ${record.type}.`
				)
			);
		}
		wanted.delete(record.hash);
		records.push(next.value);
	}
	const [missing] = wanted.keys();
	if (missing !== undefined) {
		return yield* Effect.die(
			new Error(`The save has no record 0x${missing.toString(16)}.`)
		);
	}
	if (save.keys === undefined) {
		return keys.length === 0
			? { ...save, records }
			: yield* Effect.die(new Error("This save file keeps no per-actor keys."));
	}
	const set = new Set(save.keys);
	for (const edit of keys) {
		if (edit.set) {
			set.add(edit.key);
		} else {
			set.delete(edit.key);
		}
	}
	return { ...save, records, keys: Arr.sort([...set], Order.BigInt) };
});
