import { BunFileSystem } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	Effect,
	Exit,
	Fiber,
	FileSystem,
	Schema,
	Scope,
	String as Str,
} from "effect";
import { TestClock } from "effect/testing";
import { Buttons } from "../src/contract/values.ts";
import { connect, WireLine, wireLog } from "../src/emulator/emulator.ts";
import { EmulatorRefused, EmulatorUnavailable } from "../src/errors.ts";
import { fakeLichii } from "./fake-lichii.ts";

const logFileTail = Effect.succeed([
	"[  61.2] Debug <Info> the last line before the end",
]);
const noWire = { line: () => Effect.void };
const pressA = [
	{ buttons: Buttons.make(["A"]), frames: 4 },
	{ leftStick: [0, 1], frames: 2 },
] as const;
const programStart = 0x80_27_c0_00n;

const count = (requests: readonly { method: string }[], method: string) =>
	Arr.filter(requests, (request) => request.method === method).length;

describe("the debug server client", () => {
	it.effect(
		"a reply with another request's id fails the call and every later one",
		() =>
			Effect.gen(function* () {
				const lichii = yield* fakeLichii();
				const emulator = yield* connect({
					path: lichii.path,
					wire: noWire,
					logFileTail,
				});
				yield* lichii.misnumber;
				const first = yield* Effect.flip(emulator.status);
				const second = yield* Effect.flip(emulator.status);
				expect(first).toEqual(
					EmulatorUnavailable.make({
						method: "status",
						reason: "garbled",
						logTail: yield* logFileTail,
					})
				);
				expect(second).toEqual(first);
				expect(count(yield* lichii.requests, "status")).toBe(1);
			})
	);

	it.effect(
		"Lichii exiting mid-call fails every waiting call with the end of its log",
		() =>
			Effect.gen(function* () {
				const lichii = yield* fakeLichii();
				const emulator = yield* connect({
					path: lichii.path,
					wire: noWire,
					logFileTail,
				});
				const pressing = yield* Effect.forkChild(
					Effect.flip(emulator.input(pressA))
				);
				yield* lichii.received("input_sequence");
				const asking = yield* Effect.forkChild(Effect.flip(emulator.status));
				yield* lichii.exit;
				const logTail = yield* logFileTail;
				expect(yield* Fiber.join(pressing)).toEqual(
					EmulatorUnavailable.make({
						method: "input_sequence",
						reason: "closed",
						logTail,
					})
				);
				expect(yield* Fiber.join(asking)).toEqual(
					EmulatorUnavailable.make({
						method: "status",
						reason: "closed",
						logTail,
					})
				);
				expect(count(yield* lichii.requests, "status")).toBe(0);
			})
	);

	it.effect("a press whose answer is lost is not sent again", () =>
		Effect.gen(function* () {
			const lichii = yield* fakeLichii();
			const emulator = yield* connect({
				path: lichii.path,
				wire: noWire,
				logFileTail,
			});
			const pressing = yield* Effect.forkChild(
				Effect.flip(emulator.input(pressA))
			);
			yield* lichii.received("input_sequence");
			yield* lichii.drop;
			const lost = EmulatorUnavailable.make({
				method: "input_sequence",
				reason: "closed",
				logTail: yield* logFileTail,
			});
			expect(yield* Fiber.join(pressing)).toEqual(lost);
			expect(yield* Effect.flip(emulator.input(pressA))).toEqual(lost);
			expect(count(yield* lichii.requests, "input_sequence")).toBe(1);
		})
	);

	it.effect(
		"a server that stops answering fails the call at its deadline",
		() =>
			Effect.gen(function* () {
				const lichii = yield* fakeLichii();
				const emulator = yield* connect({
					path: lichii.path,
					wire: noWire,
					logFileTail,
				});
				yield* lichii.hold;
				const asking = yield* Effect.forkChild(Effect.flip(emulator.status));
				yield* lichii.received("status");
				yield* TestClock.adjust("3 seconds");
				expect(yield* Fiber.join(asking)).toEqual(
					EmulatorUnavailable.make({
						method: "status",
						reason: "deadline",
						logTail: yield* logFileTail,
					})
				);
			})
	);

	it.effect("the wire log keeps each line sent and received, in order", () =>
		Effect.gen(function* () {
			const lichii = yield* fakeLichii();
			const fs = yield* FileSystem.FileSystem;
			const path = `${yield* fs.makeTempDirectoryScoped()}/wire.ndjson`;
			const wire = yield* wireLog(path);
			const emulator = yield* connect({ path: lichii.path, wire, logFileTail });
			const status = yield* emulator.status;
			const text = yield* fs.readFileString(path);
			const lines = yield* Effect.forEach(
				Arr.filter(Str.split(text, "\n"), Str.isNonEmpty),
				(line) => Schema.decodeEffect(WireLine)(line),
				{ concurrency: 1 }
			);
			expect(Arr.map(lines, (line) => line.direction)).toEqual([
				"sent",
				"received",
			]);
			expect(lines[0]?.text).toBe(
				'{"id":1,"method":"status","params":{"fps_window_ms":0}}'
			);
			expect(lines[1]?.text).toContain(`"presents":${status.presents}`);
		}).pipe(Effect.provide(BunFileSystem.layer))
	);
	it.effect(
		"a call its caller interrupts is seen through, and the next call is answered",
		() =>
			Effect.gen(function* () {
				const lichii = yield* fakeLichii();
				const emulator = yield* connect({
					path: lichii.path,
					wire: noWire,
					logFileTail,
				});
				yield* lichii.hold;
				const asking = yield* Effect.forkChild(emulator.status);
				yield* lichii.received("status");
				const stopping = yield* Effect.forkChild(Fiber.interrupt(asking));
				yield* lichii.release;
				yield* Fiber.join(stopping);
				expect((yield* emulator.status).build).toBe("Lichii fake");
				expect(count(yield* lichii.requests, "status")).toBe(2);
			})
	);

	it.effect(
		"closing the connection's scope fails a waiting press at once",
		() =>
			Effect.gen(function* () {
				const lichii = yield* fakeLichii();
				const scope = yield* Scope.make();
				const emulator = yield* connect({
					path: lichii.path,
					wire: noWire,
					logFileTail,
				}).pipe(Scope.provide(scope));
				const pressing = yield* Effect.forkChild(
					Effect.flip(emulator.input(pressA))
				);
				yield* lichii.received("input_sequence");
				yield* Scope.close(scope, Exit.void);
				expect(yield* Fiber.join(pressing)).toEqual(
					EmulatorUnavailable.make({
						method: "input_sequence",
						reason: "closed",
						logTail: yield* logFileTail,
					})
				);
			})
	);

	it.effect("a press's limit holds at 15 frames a second", () =>
		Effect.gen(function* () {
			const lichii = yield* fakeLichii();
			const emulator = yield* connect({
				path: lichii.path,
				wire: noWire,
				logFileTail,
			});
			yield* Effect.forkChild(
				emulator.input([{ buttons: Buttons.make(["A"]), frames: 1500 }])
			);
			yield* lichii.received("input_sequence");
			const [press] = Arr.filter(
				yield* lichii.requests,
				(request) => request.method === "input_sequence"
			);
			expect(press?.params.timeout_s).toBe(1500 / 15 + 10);
		})
	);

	it.effect("a memory answer shorter than asked for fails as garbled", () =>
		Effect.gen(function* () {
			const lichii = yield* fakeLichii();
			const emulator = yield* connect({
				path: lichii.path,
				wire: noWire,
				logFileTail,
			});
			yield* lichii.shortRead;
			expect(yield* Effect.flip(emulator.readMemory(programStart, 4))).toEqual(
				EmulatorUnavailable.make({
					method: "read_memory",
					reason: "garbled",
					logTail: yield* logFileTail,
				})
			);
		})
	);

	it.effect("a memory search whose page does not move on fails", () =>
		Effect.gen(function* () {
			const lichii = yield* fakeLichii();
			const emulator = yield* connect({
				path: lichii.path,
				wire: noWire,
				logFileTail,
			});
			yield* lichii.stuckSearch;
			const error = yield* Effect.flip(
				emulator.searchMemory({
					pattern: Uint8Array.fromHex("01000000"),
					from: 0x80_00_00_00n,
					to: 0x82_00_00_00n,
					maxHits: 2,
				})
			);
			expect(error).toBeInstanceOf(EmulatorRefused);
			expect(count(yield* lichii.requests, "search_memory")).toBe(1);
			expect((yield* emulator.status).build).toBe("Lichii fake");
		})
	);

	it.effect(
		"a closed socket fails the connection with EmulatorUnavailable",
		() =>
			Effect.gen(function* () {
				const lichii = yield* fakeLichii();
				yield* lichii.exit;
				expect(
					yield* Effect.flip(
						connect({ path: lichii.path, wire: noWire, logFileTail })
					)
				).toEqual(
					EmulatorUnavailable.make({
						method: "connect",
						reason: "connect",
						logTail: yield* logFileTail,
					})
				);
			})
	);
});
