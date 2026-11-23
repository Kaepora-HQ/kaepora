/**
 * A client of one running Lichii's debug server. The server runs every handler on one thread, one at a time across
 * all connections, so the client holds one connection per game and sends one request at a time. It never sends a
 * request again: a lost `input` or `write_memory` answer does not mean the call did nothing.
 */
import { BunSocket } from "@effect/platform-bun";
import {
	Array as Arr,
	Clock,
	Deferred,
	Duration,
	Effect,
	Exit,
	FileSystem,
	Queue,
	Schema,
	Scope,
	Semaphore,
	Stream,
} from "effect";
import { Socket } from "effect/socket";
import type { Buttons } from "../contract/values.ts";
import {
	EmulatorRefused,
	EmulatorUnavailable,
	WriteUnsupported,
} from "../errors.ts";
import {
	Done,
	hex,
	LogTail,
	Read,
	Reply,
	Request,
	Screenshot,
	Search,
	Status,
} from "./protocol.ts";

export const connectDeadline = "2 seconds";

/**
 * Added to the server's own limit for a call, so the server's limit fires first and this one only catches a server
 * that stopped answering.
 */
export const replyMargin = Duration.seconds(3);

/** The most bytes one `read_memory` returns. */
const readChunk = 64 * 1024;

const searchBudget = Duration.seconds(2);

const screenshotLimit = Duration.seconds(5);

/**
 * The server's own limit for an input: its frames at 15 a second, the slowest a hidden Hyprland workspace draws, plus
 * 10 seconds, at most the server's 900.
 */
const inputLimit = (frames: number) =>
	Duration.seconds(Math.min(900, Math.ceil(frames / 15) + 10));

/** What a build without `write_memory` answers it with. */
const noWrites = 'unknown method "write_memory"';

export type EmulatorError = EmulatorUnavailable | EmulatorRefused;

/** One step of input: buttons held and sticks set for `frames` presented frames, then released. */
export interface InputStep {
	readonly buttons?: Buttons;
	readonly frames: number;
	readonly leftStick?: readonly [number, number];
	readonly rightStick?: readonly [number, number];
}

export interface Emulator {
	/**
	 * Steps in order, in one `input_sequence` call: at most 1,000 steps, and 13,350 frames in all, the most the server's
	 * limit holds at 15 frames a second.
	 */
	readonly input: (
		steps: readonly [InputStep, ...InputStep[]]
	) => Effect.Effect<void, EmulatorError>;
	readonly logTail: (options: {
		readonly limit: number;
		readonly contains?: string;
	}) => Effect.Effect<readonly string[], EmulatorError>;
	readonly readMemory: (
		address: bigint,
		size: number
	) => Effect.Effect<Uint8Array, EmulatorError>;
	/**
	 * A PNG of the next presented frame. Before the first frame it fails, and so does every later one until a frame is
	 * presented.
	 */
	readonly screenshot: (options: {
		readonly height: number;
	}) => Effect.Effect<Uint8Array, EmulatorError>;
	readonly searchMemory: (options: {
		readonly pattern: Uint8Array;
		readonly from: bigint;
		readonly to: bigint;
		readonly maxHits: number;
	}) => Effect.Effect<readonly bigint[], EmulatorError>;
	/** `status` with `fps_window_ms: 0`, so a poll never holds the server's thread. */
	readonly status: Effect.Effect<Status, EmulatorError>;
	readonly writeMemory: (
		address: bigint,
		data: Uint8Array
	) => Effect.Effect<void, EmulatorError | WriteUnsupported>;
}

export interface WireLog {
	readonly line: (
		direction: "sent" | "received",
		text: string
	) => Effect.Effect<void>;
}

export const WireLine = Schema.fromJsonString(
	Schema.Struct({
		at: Schema.String,
		direction: Schema.Literals(["sent", "received"]),
		text: Schema.String,
	})
);

export const wireLog = Effect.fn("wireLog")(function* (path: string) {
	const fs = yield* FileSystem.FileSystem;
	const file = yield* fs.open(path, { flag: "a" });
	const encoder = new TextEncoder();
	const wire: WireLog = {
		line: Effect.fnUntraced(function* (direction, text) {
			const millis = yield* Clock.currentTimeMillis;
			const at = Temporal.Instant.fromEpochMilliseconds(millis).toString();
			const line = yield* Schema.encodeEffect(WireLine)({
				at,
				direction,
				text,
			});
			yield* file.writeAll(encoder.encode(`${line}\n`));
		}, Effect.orDie),
	};
	return wire;
});

interface Ended {
	readonly logTail: readonly string[];
	readonly reason: EmulatorUnavailable["reason"];
}

/**
 * Where a debug server listens: the Unix socket a place's relay passes it through, or the loopback address a player's
 * Lichii serves it on.
 */
export type DebugServer =
	| { readonly path: string }
	| { readonly host: string; readonly port: number };

export const connect = Effect.fn("Emulator.connect")(function* (
	options: DebugServer & {
		readonly wire: WireLog;
		/** The end of Lichii's log file, read when the connection ends, since the server may be gone. */
		readonly logFileTail: Effect.Effect<readonly string[]>;
	}
) {
	const scope = yield* Effect.scope;
	const connection = yield* Scope.fork(scope);
	const socket = yield* BunSocket.makeNet({
		...("path" in options
			? { path: options.path }
			: { host: options.host, port: options.port }),
		openTimeout: connectDeadline,
	});
	const pull = yield* Socket.readerString(socket).pipe(
		Scope.provide(connection),
		Effect.catchTag("SocketError", () =>
			Effect.flatMap(options.logFileTail, (logTail) =>
				Effect.fail(
					EmulatorUnavailable.make({
						method: "connect",
						reason: "connect",
						logTail,
					})
				)
			)
		)
	);
	const writer = yield* Scope.provide(socket.writer, connection);
	const replies = yield* Queue.unbounded<string>();
	const ended = yield* Deferred.make<Ended>();
	const permit = yield* Semaphore.make(1);
	let lastId = 0;

	const end = Effect.fnUntraced(function* (reason: Ended["reason"]) {
		if (yield* Deferred.isDone(ended)) {
			return;
		}
		const logTail = yield* options.logFileTail;
		yield* Deferred.succeed(ended, { reason, logTail });
	});
	yield* Scope.addFinalizer(connection, end("closed"));
	const unavailable = (method: string) =>
		Effect.flatMap(Deferred.await(ended), (how) =>
			Effect.fail(EmulatorUnavailable.make({ method, ...how }))
		);
	const drop = (method: string, reason: Ended["reason"]) =>
		end(reason).pipe(
			Effect.andThen(Scope.close(connection, Exit.void)),
			Effect.uninterruptible,
			Effect.andThen(unavailable(method))
		);

	// Lines are split here rather than by Ndjson, so the wire log gets each line as it came, one that does not parse too.
	yield* Stream.fromPull(Effect.succeed(pull)).pipe(
		Stream.splitLines,
		Stream.runForEach((line) =>
			options.wire
				.line("received", line)
				.pipe(Effect.andThen(Queue.offer(replies, line)))
		),
		Effect.ignore,
		Effect.andThen(end("closed")),
		Effect.forkIn(connection)
	);

	const exchange = <A>(
		method: string,
		params: Readonly<Record<string, unknown>>,
		result: Schema.Decoder<A>
	) =>
		Effect.gen(function* () {
			lastId += 1;
			const id = lastId;
			const text = yield* Schema.encodeEffect(Request)({
				id,
				method,
				params,
			}).pipe(Effect.orDie);
			yield* options.wire.line("sent", text);
			yield* writer
				.write(`${text}\n`)
				.pipe(Effect.catchTag("SocketError", () => drop(method, "closed")));
			const reply = yield* Queue.take(replies).pipe(
				Effect.flatMap(Schema.decodeEffect(Reply)),
				Effect.catchTag("SchemaError", () => drop(method, "garbled"))
			);
			if (reply.id !== id) {
				return yield* drop(method, "garbled");
			}
			if ("error" in reply) {
				return yield* EmulatorRefused.make({ method, error: reply.error });
			}
			return yield* Schema.decodeUnknownEffect(result)(reply.result).pipe(
				Effect.catchTag("SchemaError", () => drop(method, "garbled"))
			);
		});

	const call = <A>(
		method: string,
		params: Readonly<Record<string, unknown>>,
		limit: Duration.Duration,
		result: Schema.Decoder<A>
	) =>
		permit.withPermit(
			Effect.gen(function* () {
				if (yield* Deferred.isDone(ended)) {
					return yield* unavailable(method);
				}
				// A written request is seen through to its answer, its deadline or the connection's end before an
				// interrupt takes effect, so the next request never meets this one's answer.
				return yield* exchange(method, params, result).pipe(
					Effect.raceFirst(unavailable(method)),
					Effect.timeoutOrElse({
						duration: Duration.sum(limit, replyMargin),
						orElse: () => drop(method, "deadline"),
					}),
					Effect.uninterruptible
				);
			})
		);

	const emulator: Emulator = {
		input: Effect.fn("Emulator.input")(function* (steps) {
			const limit = inputLimit(
				Arr.reduce(steps, 0, (frames, step) => frames + step.frames)
			);
			yield* call(
				"input_sequence",
				{
					steps: Arr.map(steps, (step) => ({
						buttons: step.buttons,
						left_stick: step.leftStick,
						right_stick: step.rightStick,
						frames: step.frames,
					})),
					timeout_s: Duration.toSeconds(limit),
				},
				limit,
				Done
			);
		}),
		logTail: Effect.fn("Emulator.logTail")(function* ({ limit, contains }) {
			const tail = yield* call(
				"log_tail",
				{ limit, contains },
				Duration.zero,
				LogTail
			);
			return tail.lines;
		}),
		readMemory: Effect.fn("Emulator.readMemory")(function* (address, size) {
			const pieces = yield* Effect.forEach(
				Arr.range(0, Math.ceil(size / readChunk) - 1),
				(piece) => {
					const length = Math.min(readChunk, size - piece * readChunk);
					return call(
						"read_memory",
						{ address: hex(address + BigInt(piece * readChunk)), size: length },
						Duration.zero,
						Read(length)
					);
				},
				{ concurrency: 1 }
			);
			const bytes = new Uint8Array(size);
			for (const [piece, { hex: data }] of pieces.entries()) {
				bytes.set(data, piece * readChunk);
			}
			return bytes;
		}),
		screenshot: Effect.fn("Emulator.screenshot")(function* ({ height }) {
			const shot = yield* call(
				"screenshot",
				{ height, timeout_s: Duration.toSeconds(screenshotLimit) },
				screenshotLimit,
				Screenshot
			);
			return shot.png_base64;
		}),
		searchMemory: Effect.fn("Emulator.searchMemory")(function* ({
			pattern,
			from,
			to,
			maxHits,
		}) {
			const search = (
				start: bigint,
				before: readonly bigint[]
			): Effect.Effect<readonly bigint[], EmulatorError> =>
				call(
					"search_memory",
					{
						pattern: pattern.toHex(),
						regions: "all",
						start: hex(start),
						end: hex(to),
						max_hits: maxHits - before.length,
						budget_ms: Duration.toMillis(searchBudget),
					},
					searchBudget,
					Search
				).pipe(
					Effect.flatMap((page) => {
						const hits = [...before, ...page.hits];
						if (page.complete || hits.length >= maxHits) {
							return Effect.succeed(hits);
						}
						return page.next > start
							? search(page.next, hits)
							: Effect.fail(
									EmulatorRefused.make({
										method: "search_memory",
										error: `the search did not move past ${hex(start)}`,
									})
								);
					})
				);
			return yield* search(from, []);
		}),
		status: call("status", { fps_window_ms: 0 }, Duration.zero, Status).pipe(
			Effect.withSpan("Emulator.status")
		),
		writeMemory: Effect.fn("Emulator.writeMemory")(function* (address, data) {
			yield* call(
				"write_memory",
				{ address: hex(address), data: data.toHex() },
				Duration.zero,
				Done
			).pipe(
				Effect.catchIf(
					(error) =>
						error._tag === "EmulatorRefused" && error.error === noWrites,
					() => Effect.fail(WriteUnsupported.make({}))
				)
			);
		}),
	};
	return emulator;
});
