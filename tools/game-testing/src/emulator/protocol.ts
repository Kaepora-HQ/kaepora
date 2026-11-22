/**
 * Lichii's debug server: one JSON object per line over a loopback TCP socket, answered in order. A request is
 * `{id, method, params}`, and its answer `{id, result}` or `{id, error}` with the error as one sentence; a line that
 * is not a request is answered with a null id. Each result schema names only the fields the library reads.
 */
import { Schema, SchemaGetter } from "effect";

export const Request = Schema.fromJsonString(
	Schema.Struct({
		id: Schema.Int,
		method: Schema.String,
		params: Schema.Record(Schema.String, Schema.Unknown),
	})
);

export const Reply = Schema.fromJsonString(
	Schema.Union([
		Schema.Struct({ id: Schema.NullOr(Schema.Int), error: Schema.String }),
		Schema.Struct({ id: Schema.Int, result: Schema.Unknown }),
	])
);

/** A guest address as the server prints and takes it, such as `0x8027c000`. */
export const hex = (address: bigint) => `0x${address.toString(16)}`;

export const Address = Schema.String.check(
	Schema.isPattern(/^0x[0-9a-f]+$/u)
).pipe(
	Schema.decodeTo(Schema.BigInt, {
		decode: SchemaGetter.transform((text: string) => BigInt(text)),
		encode: SchemaGetter.transform(hex),
	})
);

/** `status`. `presents` counts the frames shown since boot, 0 until the first. */
export const Status = Schema.Struct({
	build: Schema.String,
	presents: Schema.Int,
});
export type Status = typeof Status.Type;

export const Screenshot = Schema.Struct({
	png_base64: Schema.Uint8ArrayFromBase64,
});

/** `search_memory`: the hits, and where to go on when the search ran out of time or hits before the range's end. */
export const Search = Schema.Union([
	Schema.Struct({
		hits: Schema.Array(Address),
		complete: Schema.Literal(true),
	}),
	Schema.Struct({
		hits: Schema.Array(Address),
		complete: Schema.Literal(false),
		next: Address,
	}),
]);

/** `read_memory` of `size` bytes, which the answer holds exactly. */
export const Read = (size: number) =>
	Schema.Struct({
		hex: Schema.Uint8ArrayFromHex.check(
			Schema.makeFilter((bytes) =>
				bytes.length === size
					? undefined
					: `${size} bytes were asked for and ${bytes.length} came back`
			)
		),
	});

export const LogTail = Schema.Struct({ lines: Schema.Array(Schema.String) });

/** A call whose result the library does not read. */
export const Done = Schema.Unknown;
