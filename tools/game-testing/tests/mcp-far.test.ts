import { BunServices } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import type { Duration } from "effect";
import {
	Array as Arr,
	Config,
	Context,
	Deferred,
	Effect,
	Exit,
	Fiber,
	FileSystem,
	Formatter,
	Layer,
	Option,
	Order,
	Queue,
	Schedule,
	Schema,
	Scope,
	String as Str,
	Stream,
} from "effect";
import { McpProtocol, McpSchema, McpTest } from "effect/ai";
import { ChildProcess, ChildProcessSpawner } from "effect/process";
import { TestClock } from "effect/testing";
import { RunRecord } from "../src/contract/record.ts";
import { PlaceId, RunId } from "../src/contract/values.ts";
import { Runs } from "../src/host/runs.ts";
import { Machine as GameMachine } from "../src/machine.ts";
import { registrations } from "../src/mcp/server.ts";
import { packageRoot, Scripts } from "../src/scripts/runner.ts";
import { modList, startList } from "./fake-game.ts";
import type { FakeLichii } from "./fake-lichii.ts";
import type { Machine } from "./fake-machine.ts";
import {
	fakeCaller,
	game as fakeGame,
	fakeMachine,
	served,
	token,
} from "./fake-machine.ts";

/** `IsTreasureBoxOpen.5005` of the fake game, by its hash. */
const chest = "0x1d2a8fff";
const openChest = (lichii: FakeLichii) => {
	Effect.runSync(lichii.put("IsTreasureBoxOpen.5005", true));
};
/** A talk that its second press of A ends. */
const talkOfTwo = () => {
	let presses = 0;
	return (lichii: FakeLichii) => {
		presses += 1;
		if (presses === 2) {
			Effect.runSync(lichii.playing(false));
		}
	};
};
const fakePng = new TextEncoder().encode("fake png");
const lasting = { timeout: 90_000 };

const Json = Schema.fromJsonString(Schema.Unknown);
const Started = Schema.fromJsonString(
	Schema.Struct({ session: Schema.String, run: RunId, place: PlaceId })
);
const Ran = Schema.fromJsonString(
	Schema.Struct({
		run: RunId,
		outcome: Schema.Unknown,
		result: Schema.optionalKey(Schema.Unknown),
		folder: Schema.optionalKey(Schema.String),
	})
);
const Stopped = Schema.fromJsonString(
	Schema.Struct({
		run: RunId,
		outcome: Schema.Unknown,
		folder: Schema.optionalKey(Schema.String),
	})
);
const Landed = Schema.fromJsonString(
	Schema.Struct({
		run: RunId,
		outcome: Schema.Unknown,
		folder: Schema.optionalKey(Schema.String),
		notCopied: Schema.optionalKey(Schema.String),
	})
);
const Record = Schema.fromJsonString(RunRecord);

/** What the agent's client reads from a JSON-RPC line the server wrote. */
const Answer = Schema.fromJsonString(
	Schema.Struct({
		id: Schema.optionalKey(Schema.Finite),
		result: Schema.optionalKey(Schema.Unknown),
		error: Schema.optionalKey(Schema.Unknown),
	})
);

/** The text a tool answered, and its images. */
const answered = (result: McpSchema.CallToolResult) => ({
	error: result.isError === true,
	text: Arr.join(
		Arr.flatMap(result.content, (block) =>
			block.type === "text" ? [block.text] : []
		),
		"\n"
	),
	images: Arr.flatMap(result.content, (block) =>
		block.type === "image" ? [block] : []
	),
});

/** An agent's machine: a home with no game file, no podman and no save. */
const agentHome = Effect.flatMap(FileSystem.FileSystem, (fs) =>
	fs.makeTempDirectoryScoped()
);

/**
 * `game-testing mcp` as an agent's client runs it: a process on the agent's machine with the far machine's URL, spoken to
 * over its stdio. The token comes from the environment unless `env` says otherwise.
 */
const agentServer = (options: {
	readonly url: string;
	readonly home: string;
	readonly env?: Readonly<Record<string, string>>;
}) =>
	Effect.gen(function* () {
		const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
		const path = yield* Config.String("PATH");
		const { home } = options;
		const handle = yield* spawner.spawn(
			ChildProcess.make(
				process.execPath,
				[`${packageRoot}/src/main.ts`, "mcp"],
				{
					cwd: home,
					extendEnv: false,
					env: {
						PATH: path,
						HOME: home,
						XDG_CACHE_HOME: `${home}/cache`,
						XDG_CONFIG_HOME: `${home}/config`,
						XDG_DATA_HOME: `${home}/data`,
						XDG_STATE_HOME: `${home}/state`,
						XDG_RUNTIME_DIR: `${home}/run`,
						GAME_TESTING_URL: options.url,
						...(options.env ?? { GAME_TESTING_TOKEN: token }),
					},
				}
			)
		);
		const requests = yield* Queue.unbounded<string>();
		yield* Stream.fromQueue(requests).pipe(
			Stream.map((line) => `${line}\n`),
			Stream.encodeText,
			Stream.run(handle.stdin),
			Effect.ignore,
			Effect.forkScoped
		);
		const waiting = new Map<number, Deferred.Deferred<unknown>>();
		yield* Stream.decodeText(handle.stdout).pipe(
			Stream.splitLines,
			Stream.runForEach((line) =>
				Effect.flatMap(Schema.decodeEffect(Answer)(line), (message) => {
					const answer =
						message.id === undefined ? undefined : waiting.get(message.id);
					return answer === undefined
						? Effect.void
						: Deferred.succeed(answer, message.result ?? message.error);
				})
			),
			Effect.ignore,
			Effect.forkScoped
		);
		let last = 0;
		const request = Effect.fnUntraced(function* (
			method: string,
			params: unknown
		) {
			last += 1;
			const id = last;
			const answer = yield* Deferred.make<unknown>();
			waiting.set(id, answer);
			yield* Queue.offer(
				requests,
				Formatter.formatJson({ jsonrpc: "2.0", id, method, params })
			);
			// A server that exits answers nothing more.
			return yield* Effect.raceFirst(
				Deferred.await(answer),
				Effect.flatMap(handle.exitCode, (code) =>
					Effect.die(new Error(`mcp exited with status ${code}`))
				)
			);
		});
		yield* request("initialize", {
			protocolVersion: "2025-11-25",
			capabilities: {},
			clientInfo: { name: "mcp-far.test.ts", version: "1" },
		});
		yield* Queue.offer(
			requests,
			Formatter.formatJson({
				jsonrpc: "2.0",
				method: "notifications/initialized",
			})
		);
		const call = (name: string, args: Readonly<Record<string, unknown>>) =>
			request("tools/call", { name, arguments: args }).pipe(
				Effect.flatMap(Schema.decodeUnknownEffect(McpSchema.CallToolResult)),
				Effect.map(answered)
			);
		return { call, kill: handle.kill({ killSignal: "SIGKILL" }) };
	});

const held = (machine: Machine, count: number) =>
	machine.podman.containers.pipe(
		Effect.repeat({
			schedule: Schedule.spaced("50 millis"),
			until: (containers) => containers.size === count,
		}),
		Effect.timeout("20 seconds"),
		Effect.option,
		Effect.map(Option.isSome)
	);

/** A run's record once it has its outcome, in a runs folder under `home`, as the far machine or the caller keeps it. */
const endedIn = (home: string, run: RunId) =>
	Effect.flatMap(FileSystem.FileSystem, (fs) =>
		fs
			.readFileString(`${home}/state/game-testing/runs/${run}/record.json`)
			.pipe(
				Effect.flatMap(Schema.decodeEffect(Record)),
				Effect.retry({ schedule: Schedule.spaced("50 millis") }),
				Effect.repeat({
					schedule: Schedule.spaced("50 millis"),
					until: (record) => record.outcome._tag !== "running",
				}),
				Effect.timeoutOption("20 seconds")
			)
	);

/** A far machine with `serve`, and the MCP tools on a caller with no game: their client opens in the scope it is given. */
const farTools = (options?: {
	readonly onA?: (lichii: FakeLichii) => void;
	readonly places?: number;
	readonly stopTakes?: Duration.Input;
}) =>
	Effect.gen(function* () {
		const far = yield* fakeMachine(options);
		const url = yield* served(far);
		const caller = yield* fakeCaller(url);
		const tools = yield* Layer.build(
			Scripts.layerRemote(url).pipe(Layer.provideMerge(caller.platform))
		);
		const client = McpTest.makeClient(registrations, {
			protocols: [McpProtocol.v2025_11_25],
		}).pipe(
			Effect.provideContext(tools),
			Effect.map(
				(made) => (name: string, args: Readonly<Record<string, unknown>>) =>
					Effect.map(made.callTool({ name, arguments: args }), answered)
			)
		);
		return { far, caller, tools, client };
	});

const filesOf = (folder: string) =>
	Effect.flatMap(FileSystem.FileSystem, (fs) =>
		Effect.map(fs.readDirectory(folder), Arr.sort(Order.String))
	);

describe("the MCP tools for an agent on another machine", () => {
	it.live(
		"run_script plays a far game with a mod from the agent's machine, and the run's record, pictures and saves land on the agent's machine",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const far = yield* fakeMachine({
					onA: openChest,
					autosaveOnA: { "IsTreasureBoxOpen.5005": true },
				});
				const home = yield* agentHome;
				const mod = `${home}/scratch/Test Mod`;
				yield* fs.makeDirectory(`${mod}/romfs/Banc`, { recursive: true });
				yield* fs.writeFile(
					`${mod}/romfs/Banc/Field.bcett.byml.zs`,
					new Uint8Array([1, 2, 3])
				);
				const agent = yield* agentServer({ url: yield* served(far), home });
				const ran = yield* agent.call("run_script", {
					about: "The chest Link stands before opens to A.",
					state: { base: "test-base" },
					body: `yield* game.press("A");\nconst opened = yield* game.check("${chest}", true);\nreturn { opened, shot: yield* game.screenshot("after-chest") };`,
					mod: { folder: mod },
				});
				expect(ran.error).toBe(false);
				const answer = yield* Schema.decodeEffect(Ran)(ran.text);
				expect(answer.outcome).toEqual({ _tag: "passed" });
				expect(answer.result).toEqual({ opened: true, shot: "picture-1.png" });
				expect(ran.images).toEqual([
					expect.objectContaining({ type: "image", data: fakePng }),
				]);
				const [started] = yield* far.starts;
				expect(started?.mods).toEqual({
					"Test Mod/romfs/Banc/Field.bcett.byml.zs": new Uint8Array([1, 2, 3]),
				});

				const folder = `${home}/state/game-testing/runs/${answer.run}`;
				expect(answer.folder).toBe(folder);
				const record = yield* Schema.decodeEffect(Record)(
					yield* fs.readFileString(`${folder}/record.json`)
				);
				expect(record.outcome).toEqual({ _tag: "passed" });
				const there = `${far.home}/state/game-testing/runs/${answer.run}`;
				const kept = Arr.filter(
					yield* filesOf(there),
					(file) => Str.endsWith(".png")(file) || Str.endsWith(".sav")(file)
				);
				expect(kept).toEqual([
					"picture-1.png",
					"save-0-caption.sav",
					"save-0-progress.sav",
					"save-1-caption.sav",
					"save-1-progress.sav",
				]);
				for (const file of kept) {
					expect(yield* fs.readFile(`${folder}/${file}`)).toEqual(
						yield* fs.readFile(`${there}/${file}`)
					);
				}

				const listed = yield* agent.call("list_runs", {});
				expect(listed.text).toContain(answer.run);
				const waited = yield* agent.call("wait_run", { run: answer.run });
				expect((yield* Schema.decodeEffect(Ran)(waited.text)).outcome).toEqual({
					_tag: "passed",
				});
				const part = yield* agent.call("get_run", {
					run: answer.run,
					pictures: true,
				});
				expect(yield* Schema.decodeEffect(Json)(part.text)).toMatchObject({
					steps: expect.arrayContaining([
						expect.objectContaining({ command: "screenshot" }),
					]),
				});
				expect(part.images).toHaveLength(1);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a game started by hand on a far machine takes every by-hand tool, and stop_game ends it stopped and brings its files to the agent's machine",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const far = yield* fakeMachine({ onA: talkOfTwo() });
				const home = yield* agentHome;
				const mod = `${home}/scratch/Test Mod`;
				yield* fs.makeDirectory(`${mod}/romfs/GameData`, { recursive: true });
				yield* fs.writeFile(
					`${mod}/${startList}`,
					modList(fakeGame, { "IsTreasureBoxOpen.6006": true })
				);
				const agent = yield* agentServer({ url: yield* served(far), home });

				const found = yield* agent.call("search_names", { text: "test-ba" });
				expect(yield* Schema.decodeEffect(Json)(found.text)).toMatchObject({
					names: [expect.objectContaining({ kind: "base", name: "test-base" })],
				});
				const planned = yield* agent.call("plan_state", {
					state: { base: "test-base" },
					mod: { folder: mod },
				});
				expect(yield* Schema.decodeEffect(Json)(planned.text)).toMatchObject({
					edits: expect.arrayContaining([
						expect.objectContaining({
							name: "IsTreasureBoxOpen.6006",
							from: { _tag: "mod", mod: "Test Mod" },
						}),
					]),
				});

				const started = yield* agent.call("start_game", {
					state: { base: "test-base" },
				});
				expect(started.error).toBe(false);
				const { session, run } = yield* Schema.decodeEffect(Started)(
					started.text
				);
				const places = yield* agent.call("places", {});
				expect(places.text).toContain(session);
				const steps = [
					yield* agent.call("press", { session, buttons: ["B"] }),
					yield* agent.call("stick", {
						session,
						position: { stick: "Left", x: 0, y: 1 },
						frames: 4,
					}),
					yield* agent.call("set_values", {
						session,
						values: { "PlayerStatus.CurrentRupee": 50 },
					}),
				];
				expect(Arr.map(steps, (step) => step.error)).toEqual([
					false,
					false,
					false,
				]);
				const read = yield* agent.call("read_values", {
					session,
					values: ["PlayerStatus.CurrentRupee"],
				});
				expect(yield* Schema.decodeEffect(Json)(read.text)).toEqual({
					values: { "PlayerStatus.CurrentRupee": 50 },
				});
				const until = yield* agent.call("press_until", {
					session,
					buttons: ["A"],
					value: chest,
					expected: false,
				});
				expect(until.text).toBe("0");
				const shot = yield* agent.call("screenshot", {
					session,
					label: "by-hand",
				});
				expect(shot.images).toEqual([
					expect.objectContaining({ type: "image", data: fakePng }),
				]);
				yield* Effect.forEach(
					yield* far.games,
					(lichii) => lichii.playing(true),
					{ concurrency: 1, discard: true }
				);
				const talk = yield* agent.call("complete_dialog", {
					session,
					every: 2,
				});
				expect(yield* Schema.decodeEffect(Json)(talk.text)).toEqual({
					presses: 2,
					pictures: ["picture-2.png", "picture-3.png"],
				});
				expect(talk.images).toHaveLength(2);

				const stopped = yield* agent.call("stop_game", { session });
				const folder = `${home}/state/game-testing/runs/${run}`;
				expect(yield* Schema.decodeEffect(Stopped)(stopped.text)).toEqual({
					run,
					outcome: { _tag: "stopped", reason: "stop_game ended it" },
					folder,
				});
				expect(yield* held(far, 0)).toBe(true);
				const record = yield* Schema.decodeEffect(Record)(
					yield* fs.readFileString(`${folder}/record.json`)
				);
				expect(Arr.map(record.steps, (step) => step.command)).toEqual([
					"press",
					"stick",
					"set",
					"read",
					"press-until",
					"screenshot",
					"dialog",
					"dialog",
				]);
				expect(yield* filesOf(folder)).toEqual([
					"picture-1.png",
					"picture-2.png",
					"picture-3.png",
					"record.json",
					"save-0-caption.sav",
					"save-0-progress.sav",
				]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.effect(
		"a far game no call names for 10 minutes stops, its run ends stopped, and each call holds it 10 minutes more",
		() =>
			Effect.gen(function* () {
				// The fakes, the far serve and the connection to it run on the live clock; the lease counts on the test clock.
				const far = yield* TestClock.withLive(fakeMachine());
				const url = yield* TestClock.withLive(served(far));
				const caller = yield* TestClock.withLive(fakeCaller(url));
				const tools = yield* TestClock.withLive(
					Layer.build(
						Scripts.layerRemote(url).pipe(Layer.provideMerge(caller.platform))
					)
				);
				const client = yield* McpTest.makeClient(registrations, {
					protocols: [McpProtocol.v2025_11_25],
				}).pipe(Effect.provideContext(tools));
				const call = (name: string, args: Readonly<Record<string, unknown>>) =>
					TestClock.withLive(
						Effect.map(client.callTool({ name, arguments: args }), answered)
					);
				const started = yield* call("start_game", {
					state: { base: "test-base" },
				});
				const { session, run } = yield* Schema.decodeEffect(Started)(
					started.text
				);
				yield* TestClock.adjust("9 minutes");
				const read = yield* call("read_values", {
					session,
					values: ["PlayerStatus.CurrentRupee"],
				});
				expect(read.error).toBe(false);
				yield* TestClock.adjust("9 minutes");
				expect((yield* far.podman.containers).size).toBe(1);
				yield* TestClock.adjust("2 minutes");
				expect(yield* TestClock.withLive(held(far, 0))).toBe(true);
				const fs = yield* FileSystem.FileSystem;
				const record = yield* TestClock.withLive(
					Effect.flatMap(
						fs.readFileString(
							`${far.home}/state/game-testing/runs/${run}/record.json`
						),
						Schema.decodeEffect(Record)
					)
				);
				expect(record.outcome).toEqual({
					_tag: "stopped",
					reason: "no call named its session for 10 minutes",
				});
				const copied = yield* TestClock.withLive(
					Effect.flatMap(
						fs.readFileString(
							`${caller.home}/state/game-testing/runs/${run}/record.json`
						),
						Schema.decodeEffect(Record)
					)
				);
				expect(copied.outcome).toEqual(record.outcome);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"an agent's MCP server killed mid-game gives the far place back",
		() =>
			Effect.gen(function* () {
				const far = yield* fakeMachine();
				const home = yield* agentHome;
				const agent = yield* agentServer({ url: yield* served(far), home });
				const started = yield* agent.call("start_game", {
					state: { base: "test-base" },
				});
				expect(started.error).toBe(false);
				expect(yield* held(far, 1)).toBe(true);
				yield* agent.kill;
				expect(yield* held(far, 0)).toBe(true);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a wrong token answers what to do, and with none in the environment the token file is read",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const far = yield* fakeMachine();
				const url = yield* served(far);
				const wrong = yield* agentServer({
					url,
					home: yield* agentHome,
					env: { GAME_TESTING_TOKEN: "not-the-token" },
				});
				const refused = yield* wrong.call("places", {});
				expect(refused.error).toBe(true);
				expect(refused.text).toContain(
					"is not the token serve was started with there"
				);
				const home = yield* agentHome;
				yield* fs.makeDirectory(`${home}/config/game-testing`, {
					recursive: true,
				});
				yield* fs.writeFileString(
					`${home}/config/game-testing/token`,
					`${token}\n`
				);
				const filed = yield* agentServer({ url, home, env: {} });
				const places = yield* filed.call("places", {});
				expect(places.error).toBe(false);
				expect(yield* Schema.decodeEffect(Json)(places.text)).toEqual([
					{ place: 1 },
				]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);
});

describe("a far game ends once, and calls at once never fail each other", () => {
	it.live(
		"a server that stops in order ends its game stopped, with its reason, and gives the place back",
		() =>
			Effect.gen(function* () {
				const { far, client } = yield* farTools();
				const server = yield* Scope.make();
				const call = yield* client.pipe(Scope.provide(server));
				const started = yield* call("start_game", {
					state: { base: "test-base" },
				});
				const { run } = yield* Schema.decodeEffect(Started)(started.text);
				yield* Scope.close(server, Exit.void);
				expect(yield* held(far, 0)).toBe(true);
				const record = yield* endedIn(far.home, run);
				expect(Option.map(record, (each) => each.outcome)).toEqual(
					Option.some({
						_tag: "stopped",
						reason: "the MCP server that started it stopped",
					})
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a stop_game its client cancels still ends the game, frees the place and brings the run here",
		() =>
			Effect.gen(function* () {
				const { far, caller, client } = yield* farTools({
					stopTakes: "1 second",
				});
				const call = yield* client;
				const started = yield* call("start_game", {
					state: { base: "test-base" },
				});
				const { session, run } = yield* Schema.decodeEffect(Started)(
					started.text
				);
				const stopping = yield* Effect.forkChild(
					call("stop_game", { session })
				);
				// The place takes a second to stop, so the call is cancelled while the game is still ending.
				yield* Effect.sleep("200 millis");
				yield* Fiber.interrupt(stopping);
				expect(yield* held(far, 0)).toBe(true);
				const copied = yield* endedIn(caller.home, run);
				expect(Option.map(copied, (each) => each.outcome)).toEqual(
					Option.some({ _tag: "stopped", reason: "stop_game ended it" })
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"runs at once on a far machine each get its names",
		() =>
			Effect.gen(function* () {
				const { tools } = yield* farTools({ places: 2 });
				const machine = Context.get(tools, GameMachine);
				// Each run asks for the names before its type check.
				const exits = yield* Effect.forEach(
					Arr.range(1, 6),
					() => Effect.exit(machine.names),
					{ concurrency: "unbounded" }
				);
				expect(Arr.filter(exits, Exit.isFailure)).toEqual([]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a detached far run and the wait_run calls after it all answer the run, copied here",
		() =>
			Effect.gen(function* () {
				const { caller, client } = yield* farTools({ onA: openChest });
				const call = yield* client;
				const detached = yield* call("run_script", {
					about: "The chest Link stands before opens to A.",
					state: { base: "test-base" },
					body: `yield* game.press("A");\nconst opened = yield* game.check("${chest}", true);\nreturn { opened, shot: yield* game.screenshot("after-chest") };`,
					detach: true,
				});
				const { run } = yield* Schema.decodeEffect(Ran)(detached.text);
				const waits = yield* Effect.forEach(
					Arr.range(1, 4),
					() => call("wait_run", { run }),
					{ concurrency: "unbounded" }
				);
				for (const waited of waits) {
					expect(waited.error).toBe(false);
					expect(yield* Schema.decodeEffect(Landed)(waited.text)).toEqual({
						run,
						outcome: { _tag: "passed" },
						folder: `${caller.home}/state/game-testing/runs/${run}`,
					});
					expect(waited.images).toHaveLength(1);
				}
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a far run whose copy fails answers its outcome and why its files are not here",
		() =>
			Effect.gen(function* () {
				const { far, client } = yield* farTools({ onA: openChest });
				const call = yield* client;
				// The body takes its own picture away on the game machine, as a copy that fails meets it.
				const ran = yield* call("run_script", {
					about: "The chest opens, and its picture is gone before the copy.",
					state: { base: "test-base" },
					body: `yield* game.press("A");\nconst opened = yield* game.check("${chest}", true);\nconst shot = yield* game.screenshot("after-chest");\nyield* Effect.promise(() => Bun.file("${far.home}/state/game-testing/runs/" + game.run + "/picture-1.png").delete());\nreturn { opened, shot };`,
				});
				expect(ran.error).toBe(false);
				const answer = yield* Schema.decodeEffect(Landed)(ran.text);
				expect(answer.outcome).toEqual({ _tag: "passed" });
				expect(answer.folder).toBeUndefined();
				expect(answer.notCopied).toContain(
					"the game machine has no file picture-1.png"
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.effect(
		"a connection nobody holds for 30 seconds is closed, and the next call opens one",
		() =>
			Effect.gen(function* () {
				const sockets: WebSocket[] = [];
				const Real = globalThis.WebSocket;
				const Counted = new Proxy(Real, {
					construct: (
						target,
						args: ConstructorParameters<typeof WebSocket>
					) => {
						const socket = new target(...args);
						sockets.push(socket);
						return socket;
					},
				});
				yield* Effect.acquireRelease(
					Effect.sync(() => {
						globalThis.WebSocket = Counted;
					}),
					() =>
						Effect.sync(() => {
							globalThis.WebSocket = Real;
						})
				);
				// The fakes and the far serve run on the live clock; the caller's connection counts on the test clock.
				const far = yield* TestClock.withLive(fakeMachine());
				const url = yield* TestClock.withLive(served(far));
				const caller = yield* TestClock.withLive(fakeCaller(url));
				const built = yield* Layer.build(
					GameMachine.layerRemote(url).pipe(
						Layer.provide(Runs.layer),
						Layer.provideMerge(caller.platform)
					)
				);
				const machine = Context.get(built, GameMachine);
				const places = TestClock.withLive(
					machine.call((client) => client.Places())
				);
				yield* places;
				// Its pings answer in real time between the steps of the test clock.
				for (const _ of Arr.range(1, 8)) {
					yield* TestClock.adjust("4 seconds");
					yield* TestClock.withLive(Effect.sleep("50 millis"));
				}
				yield* places;
				yield* places;
				expect(Arr.map(sockets, (socket) => socket.readyState)).toEqual([
					WebSocket.CLOSED,
					WebSocket.OPEN,
				]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);
});
