/**
 * A fake Lichii: a debug server on a real Unix socket that speaks the wire protocol, with a small game behind it, as a
 * place's relay passes the real one through. Like the real server it answers one request at a time across every
 * connection, and an input holds it while its frames pass; this one presents no new frames. The game's memory holds the program's first bytes, the game data manager and
 * a table per type, laid out as the running game lays them out. The names are made up.
 */
import {
	Array as Arr,
	Deferred,
	Effect,
	Exit,
	Option,
	Record as Rec,
	String as Str,
} from "effect";
import type { Value } from "../src/contract/values.ts";

/** A saved value the fake game holds. An Enum's value is one of its choices. */
export type FakeValue =
	| { readonly type: "Bool"; readonly value: boolean }
	| { readonly type: "Int" | "UInt" | "Float"; readonly value: number }
	| {
			readonly type: "Enum";
			readonly value: string;
			readonly choices: readonly string[];
	  }
	| { readonly type: "String64"; readonly value: string };

export interface Request {
	readonly method: string;
	readonly params: Readonly<Record<string, unknown>>;
}

export interface FakeLichii {
	/** Closes every connection, and keeps listening. */
	readonly drop: Effect.Effect<void>;
	/** Closes every connection and stops listening, as Lichii does when it exits. */
	readonly exit: Effect.Effect<void>;
	/** Answers nothing until `release`. */
	readonly hold: Effect.Effect<void>;
	/** The next answer carries another request's id. */
	readonly misnumber: Effect.Effect<void>;
	readonly path: string;
	/** Plays an event, such as a talk, or ends it, as the game's event manager says. */
	readonly playing: (event: boolean) => Effect.Effect<void>;
	/** Sets a value as the game would. */
	readonly put: (name: string, value: Value) => Effect.Effect<void>;
	/** Waits until a request for `method` has arrived. */
	readonly received: (method: string) => Effect.Effect<void>;
	readonly release: Effect.Effect<void>;
	readonly requests: Effect.Effect<readonly Request[]>;
	/** The copy after a value that the game saves. */
	readonly savedCopy: (name: string) => Effect.Effect<Value>;
	/** The next memory read is answered one byte short. */
	readonly shortRead: Effect.Effect<void>;
	/** Every search ends its page where it began, as when its budget runs out before it scans a byte. */
	readonly stuckSearch: Effect.Effect<void>;
	readonly value: (name: string) => Effect.Effect<Value>;
}

const programStart = 0x80_27_c0_00n;
const programBytes = "010000005c70c4039070c403";
const managerGlobal = 0x4c_7e_18_0n;
/** `[[program + eventGlobal]] + 0x68` is the event the game plays, 0 when none. */
const eventGlobal = 0x4c_7e_09_0n;

/** Each type's table slot in the manager, its entry size, and where its value sits in an entry. */
const tables = {
	Bool: { slot: 0, stride: 0x18, at: 0x16, size: 1 },
	Int: { slot: 2, stride: 0x28, at: 0x1c, size: 4 },
	Float: { slot: 4, stride: 0x28, at: 0x1c, size: 4 },
	Enum: { slot: 6, stride: 0x38, at: 0x1c, size: 4 },
	String64: { slot: 16, stride: 0x1_08, at: 0x74, size: 64 },
	UInt: { slot: 20, stride: 0x28, at: 0x1c, size: 4 },
} as const;

/** The most bytes one read returns, and the keys a step of input may have, as the real server has them. */
const readLimit = 64 * 1024;
const stepKeys = ["buttons", "left_stick", "right_stick", "frames"];

/** A search scans at most this much per call, so a long range comes back in pages. */
const searchPage = 0x1_00_00_00n;

/** A value's hash: its name's murmur3, or the hash itself for a name such as `0x578d41e1`. */
const hash = (text: string) =>
	Str.startsWith("0x")(text)
		? Number.parseInt(text, 16)
		: Bun.hash.murmur32v3(text);
const hex = (address: bigint) => `0x${address.toString(16)}`;

interface Region {
	readonly bytes: Uint8Array;
	readonly start: bigint;
}

class Memory {
	readonly regions: Region[] = [];
	#next = 0x10_00_00_00_00n;

	map(bytes: Uint8Array, at?: bigint): bigint {
		const start = at ?? this.#next;
		this.regions.push({ start, bytes });
		this.#next = start + BigInt(bytes.length) + 0x1_00_00n;
		return start;
	}

	view(address: bigint, size: number): DataView | undefined {
		return Arr.findFirst(
			this.regions,
			(r) =>
				address >= r.start &&
				address + BigInt(size) <= r.start + BigInt(r.bytes.length)
		).pipe(
			Option.map(
				(region) =>
					new DataView(
						region.bytes.buffer,
						region.bytes.byteOffset + Number(address - region.start),
						size
					)
			),
			Option.getOrUndefined
		);
	}
}

const writeValue = (view: DataView, value: FakeValue) => {
	switch (value.type) {
		case "Bool":
			view.setUint8(0, value.value ? 1 : 0);
			return;
		case "Int":
			view.setInt32(0, value.value, true);
			return;
		case "UInt":
			view.setUint32(0, value.value, true);
			return;
		case "Float":
			view.setFloat32(0, value.value, true);
			return;
		case "Enum":
			view.setUint32(0, hash(value.value), true);
			return;
		case "String64":
			new Uint8Array(view.buffer, view.byteOffset, 64).set(
				new TextEncoder().encode(value.value)
			);
			return;
		default:
			return value satisfies never;
	}
};

const readValue = (view: DataView, value: FakeValue): Value => {
	switch (value.type) {
		case "Bool":
			return view.getUint8(0) !== 0;
		case "Int":
			return view.getInt32(0, true);
		case "UInt":
			return view.getUint32(0, true);
		case "Float":
			return view.getFloat32(0, true);
		case "Enum": {
			const held = view.getUint32(0, true);
			return Arr.findFirst(
				value.choices,
				(choice) => hash(choice) === held
			).pipe(Option.getOrElse(() => "?"));
		}
		case "String64": {
			const bytes = new Uint8Array(view.buffer, view.byteOffset, 64);
			const end = bytes.indexOf(0);
			return new TextDecoder().decode(bytes.subarray(0, end === -1 ? 64 : end));
		}
		default:
			return value satisfies never;
	}
};

const layOut = (
	values: Readonly<Record<string, FakeValue>>,
	mapCapacity: number | undefined
) => {
	const memory = new Memory();
	memory.map(Uint8Array.fromHex(programBytes), programStart);
	const managerBytes = new Uint8Array(0x8 + 0x60 * 21);
	const manager = memory.map(managerBytes);
	const managerView = new DataView(managerBytes.buffer);
	const slot = new Uint8Array(8);
	new DataView(slot.buffer).setBigUint64(0, manager, true);
	const global = new Uint8Array(8);
	new DataView(global.buffer).setBigUint64(0, memory.map(slot), true);
	memory.map(global, programStart + managerGlobal);

	const events = memory.map(new Uint8Array(0x70));
	const eventSlot = new Uint8Array(8);
	new DataView(eventSlot.buffer).setBigUint64(0, events, true);
	const eventPointer = new Uint8Array(8);
	new DataView(eventPointer.buffer).setBigUint64(
		0,
		memory.map(eventSlot),
		true
	);
	memory.map(eventPointer, programStart + eventGlobal);

	const addresses = new Map<string, { address: bigint; size: number }>();
	for (const [type, table] of Rec.toEntries(tables)) {
		const held = Arr.filter(
			Rec.toEntries(values),
			([, value]) => value.type === type
		);
		const entryBytes = new Uint8Array(table.stride * held.length);
		const entries = memory.map(entryBytes);
		// Twice the entries, with empty pairs whose index is past the count, as a hash map has. An empty table has none.
		const pairBytes = new Uint8Array(
			held.length === 0 ? 0 : held.length * 16 + 16
		).fill(0xff);
		const pairs = new DataView(pairBytes.buffer);
		for (const [index, [name, value]] of held.entries()) {
			pairs.setUint32(index * 16 + 8, hash(name), true);
			pairs.setUint32(index * 16 + 12, index, true);
			const at = index * table.stride + table.at;
			writeValue(new DataView(entryBytes.buffer, at), value);
			if (value.type !== "String64") {
				writeValue(new DataView(entryBytes.buffer, at + table.size), value);
			}
			addresses.set(name, { address: entries + BigInt(at), size: table.size });
		}
		const head = 0x8 + 0x60 * table.slot;
		managerView.setUint32(head + 0x8, held.length, true);
		managerView.setBigUint64(head + 0x10, entries, true);
		managerView.setBigUint64(head + 0x18, memory.map(pairBytes), true);
		managerView.setUint32(
			head + 0x24,
			mapCapacity ?? pairBytes.length / 8,
			true
		);
	}
	return { memory, addresses, events };
};

type Reply = { readonly result: unknown } | { readonly error: string };

interface Connection {
	text: string;
}

interface Incoming extends Request {
	readonly id: number;
	readonly socket: Bun.Socket<Connection>;
}

const hitsIn = (region: Region, pattern: Uint8Array) =>
	Arr.map(
		Arr.filter(Arr.range(0, region.bytes.length - pattern.length), (at) =>
			Arr.every([...pattern], (byte, i) => region.bytes[at + i] === byte)
		),
		(at) => region.start + BigInt(at)
	);

let listened = 0;

export const fakeLichii = (options?: {
	/** The socket file to listen on. By default an abstract socket, which leaves no file behind. */
	readonly path?: string;
	/** The frames `status` says were presented. */
	readonly presents?: number;
	readonly values?: Readonly<Record<string, FakeValue>>;
	/** `false` for a build without `write_memory`, such as Lichii's release builds. */
	readonly writes?: boolean;
	/** The capacity every table's map claims, in place of its own. */
	readonly mapCapacity?: number;
	/** Main holds Lichii's instant text add-on, its branch and its scale, as when it applied it. */
	readonly instantText?: boolean;
	/**
	 * Answers each input once this has taken its steps, in place of holding the server while its frames pass: at once,
	 * or when the promise it gives settles, as the game answers once what the input did is done. The fake presents no
	 * frames, so an input that waits for them never answers.
	 */
	readonly onInput?: (
		steps: readonly Readonly<Record<string, unknown>>[]
	) => Promise<void> | undefined;
}) =>
	Effect.gen(function* () {
		listened += 1;
		const path =
			options?.path ?? `\0game-testing-fake-lichii-${process.pid}-${listened}`;
		const values = options?.values ?? {};
		const { memory, addresses, events } = layOut(values, options?.mapCapacity);
		// The text routine's instruction at the add-on's site, and the scale after its cave's code.
		memory.map(
			Uint8Array.of(
				...(options?.instantText === true
					? [0x53, 0x23, 0xc5, 0x14]
					: [0x69, 0xde, 0x40, 0x79])
			),
			programStart + 0xa_fe_1b_4n
		);
		memory.map(
			Uint8Array.of(
				...(options?.instantText === true
					? [0, 0x24, 0x74, 0x49]
					: [0, 0, 0, 0])
			),
			programStart + 0x3_c4_6f_10n
		);
		const log: Request[] = [];
		const arrivals: { method: string; arrived: Deferred.Deferred<void> }[] = [];
		const queue: Incoming[] = [];
		const sockets = new Set<Bun.Socket<Connection>>();
		const state: {
			held: boolean;
			misnumbered: boolean;
			busy: boolean;
			shortRead: boolean;
			stuck: boolean;
		} = {
			held: false,
			misnumbered: false,
			busy: false,
			shortRead: false,
			stuck: false,
		};

		const unmapped = (address: bigint, size: number, verb: string) => ({
			error: `guest address ${hex(address)} is not mapped (${verb} ${size} bytes at ${hex(address)})`,
		});

		/** What each method answers, or `undefined` for an input, which waits. */
		const methods: Record<
			string,
			(params: Request["params"]) => Reply | Promise<Reply> | undefined
		> = {
			status: () => ({
				result: {
					build: "Lichii fake",
					powered_on: true,
					paused: false,
					presents: options?.presents ?? 0,
					program_id: "0400486024DEC000",
				},
			}),
			read_memory: (params) => {
				const address = BigInt(String(params.address));
				const size = Number(params.size);
				if (!Number.isInteger(size) || size < 1 || size > readLimit) {
					return { error: `size must be 1 to ${readLimit} bytes` };
				}
				const view = memory.view(address, size);
				const sent = state.shortRead ? size - 1 : size;
				state.shortRead = false;
				return view === undefined
					? unmapped(address, size, "reading")
					: {
							result: {
								address: hex(address),
								size,
								hex: new Uint8Array(view.buffer, view.byteOffset, sent).toHex(),
							},
						};
			},
			write_memory: (params) => {
				if (options?.writes === false) {
					return { error: 'unknown method "write_memory"' };
				}
				const address = BigInt(String(params.address));
				const data = Uint8Array.fromHex(String(params.data));
				const view = memory.view(address, data.length);
				if (view === undefined) {
					return unmapped(address, data.length, "writing");
				}
				new Uint8Array(view.buffer, view.byteOffset, data.length).set(data);
				return { result: { address: hex(address), size: data.length } };
			},
			search_memory: (params) => {
				const pattern = Uint8Array.fromHex(String(params.pattern));
				const start = BigInt(String(params.start));
				const end = BigInt(String(params.end));
				if (state.stuck) {
					return { result: { hits: [], complete: false, next: hex(start) } };
				}
				const stop = start + searchPage < end ? start + searchPage : end;
				const hits = Arr.filter(
					Arr.flatMap(memory.regions, (region) => hitsIn(region, pattern)),
					(address) => address >= start && address < stop
				);
				const listed = Arr.map(hits, hex);
				return {
					result:
						stop < end
							? { hits: listed, complete: false, next: hex(stop) }
							: { hits: listed, complete: true },
				};
			},
			input_sequence: (params) => {
				const steps = params.steps as readonly Readonly<
					Record<string, unknown>
				>[];
				const wrong = Arr.findFirst(
					Arr.flatMap(steps, (step, index) =>
						Arr.map(Rec.keys(step), (key) => ({ index, key }))
					),
					({ key }) => !Arr.contains(stepKeys, key)
				);
				if (Option.isSome(wrong)) {
					const { index, key } = wrong.value;
					return {
						error: `step ${index}: unknown key "${key}": want buttons, left_stick, right_stick, frames`,
					};
				}
				if (options?.onInput === undefined) {
					state.busy = true;
					return;
				}
				const done = options.onInput(steps);
				const answer: Reply = { result: { steps: steps.length } };
				return done === undefined ? answer : done.then(() => answer);
			},
			screenshot: () => ({
				result: { png_base64: Buffer.from("fake png").toString("base64") },
			}),
			log_tail: () => ({ result: { lines: [] } }),
		};

		const send = (request: Incoming, reply: Reply) => {
			const id = state.misnumbered ? request.id + 1000 : request.id;
			state.misnumbered = false;
			request.socket.write(`${JSON.stringify({ id, ...reply })}\n`);
		};

		const answerNext = () => {
			const [next] = queue;
			if (next === undefined || state.held || state.busy) {
				return;
			}
			queue.shift();
			const method = methods[next.method];
			const reply =
				method === undefined
					? { error: `unknown method "${next.method}"` }
					: method(next.params);
			if (reply instanceof Promise) {
				state.busy = true;
				reply.then((answer) => {
					state.busy = false;
					send(next, answer);
					answerNext();
				});
				return;
			}
			if (reply !== undefined) {
				send(next, reply);
			}
			answerNext();
		};

		const arrive = (request: Incoming) => {
			log.push({ method: request.method, params: request.params });
			for (const arrival of arrivals) {
				if (arrival.method === request.method) {
					Deferred.doneUnsafe(arrival.arrived, Exit.void);
				}
			}
			queue.push(request);
		};

		const server = yield* Effect.acquireRelease(
			Effect.sync(() =>
				Bun.listen<Connection>({
					unix: path,
					socket: {
						open: (socket) => {
							socket.data = { text: "" };
							sockets.add(socket);
						},
						data: (socket, chunk) => {
							const lines = Str.split(
								socket.data.text + chunk.toString(),
								"\n"
							);
							socket.data.text = Arr.lastNonEmpty(lines);
							for (const line of Arr.initNonEmpty(lines)) {
								arrive({ ...(JSON.parse(line) as Incoming), socket });
							}
							answerNext();
						},
						close: (socket) => {
							sockets.delete(socket);
						},
					},
				})
			),
			(listener) => Effect.sync(() => listener.stop(true))
		);

		const closeAll = Effect.sync(() => {
			for (const socket of sockets) {
				socket.end();
			}
			queue.length = 0;
			state.busy = false;
		});

		const stored = (name: string) => {
			const at = addresses.get(name);
			const value = values[name];
			if (at === undefined || value === undefined) {
				throw new Error(`the fake game holds no ${name}`);
			}
			return { ...at, value };
		};

		const fake: FakeLichii = {
			path,
			requests: Effect.sync(() => [...log]),
			received: (method) =>
				Effect.gen(function* () {
					if (Arr.some(log, (request) => request.method === method)) {
						return;
					}
					const arrived = yield* Deferred.make<void>();
					arrivals.push({ method, arrived });
					yield* Deferred.await(arrived);
				}),
			hold: Effect.sync(() => {
				state.held = true;
			}),
			release: Effect.sync(() => {
				state.held = false;
				answerNext();
			}),
			shortRead: Effect.sync(() => {
				state.shortRead = true;
			}),
			stuckSearch: Effect.sync(() => {
				state.stuck = true;
			}),
			misnumber: Effect.sync(() => {
				state.misnumbered = true;
			}),
			drop: closeAll,
			exit: closeAll.pipe(Effect.andThen(Effect.sync(() => server.stop(true)))),
			playing: (event) =>
				Effect.sync(() => {
					const view = memory.view(events + 0x68n, 8);
					if (view === undefined) {
						throw new Error("the fake game has no event manager");
					}
					view.setBigUint64(0, event ? 1n : 0n, true);
				}),
			put: (name, value) =>
				Effect.sync(() => {
					const { address, size, value: held } = stored(name);
					const view = memory.view(address, size);
					if (view === undefined) {
						throw new Error(`the fake game holds no ${name}`);
					}
					writeValue(view, { ...held, value } as FakeValue);
				}),
			value: (name) =>
				Effect.sync(() => {
					const { address, size, value } = stored(name);
					const view = memory.view(address, size);
					return view === undefined ? "?" : readValue(view, value);
				}),
			savedCopy: (name) =>
				Effect.sync(() => {
					const { address, size, value } = stored(name);
					const view = memory.view(address + BigInt(size), size);
					return view === undefined ? "?" : readValue(view, value);
				}),
		};
		return fake;
	});
