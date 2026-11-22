import { BunServices } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	Config,
	Effect,
	Exit,
	Fiber,
	FileSystem,
	Layer,
	Option,
	Schedule,
	Schema,
	String as Str,
} from "effect";
import { McpProtocol, McpSchema, McpTest } from "effect/ai";
import { ChildProcess, ChildProcessSpawner } from "effect/process";
import { TestClock } from "effect/testing";
import { StartState } from "../src/contract/state.ts";
import { newRunId, PlaceId, RunId } from "../src/contract/values.ts";
import { Game } from "../src/game.ts";
import { Runs } from "../src/host/runs.ts";
import { Sessions } from "../src/host/sessions.ts";
import { personOf, registrations } from "../src/mcp/server.ts";
import { ran } from "../src/process.ts";
import { packageRoot, Scripts } from "../src/scripts/runner.ts";
import type { FakeLichii } from "./fake-lichii.ts";
import { fakeMachine } from "./fake-machine.ts";

/** `IsTreasureBoxOpen.5005` of the fake game, by its hash. */
const chest = "0x1d2a8fff";
const openChest = (lichii: FakeLichii) => {
	Effect.runSync(lichii.put("IsTreasureBoxOpen.5005", true));
};
const fakePng = new TextEncoder().encode("fake png");
/** A note of a hundred letters, so a few hundred of them make a long record. */
const long = Str.repeat(100)("x");
const lasting = { timeout: 60_000 };

const Json = Schema.fromJsonString(Schema.Unknown);
const Started = Schema.Struct({
	session: Schema.String,
	run: RunId,
	place: PlaceId,
});
const Ran = Schema.Struct({
	run: RunId,
	outcome: Schema.Unknown,
	result: Schema.optionalKey(Schema.Unknown),
	kept: Schema.optionalKey(Schema.String),
	notKept: Schema.optionalKey(Schema.String),
});
const Part = Schema.Struct({
	outcome: Schema.Unknown,
	part: Schema.Struct({
		from: Schema.Int,
		count: Schema.Int,
		of: Schema.Int,
	}),
	steps: Schema.Array(Schema.Struct({ input: Schema.Unknown })),
	next: Schema.optionalKey(Schema.String),
});

type Machine = Effect.Success<ReturnType<typeof fakeMachine>>;

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

/** The host's services on a fake machine, as `mcp` and `serve` build them. */
const hostOn = (machine: Machine) =>
	Layer.build(Scripts.layer.pipe(Layer.provideMerge(machine.host)));

/** An MCP client of the tools on a host, as an agent reaches them: in a session of its own. */
const clientOn = (
	host: Effect.Success<ReturnType<typeof hostOn>>,
	capabilities?: McpSchema.ClientCapabilities
) =>
	McpTest.makeClient(registrations, {
		protocols: [McpProtocol.v2025_11_25],
		...(capabilities === undefined ? {} : { clientCapabilities: capabilities }),
	}).pipe(Effect.provideContext(host));

const toolsOn = (
	machine: Machine,
	capabilities?: McpSchema.ClientCapabilities
) => Effect.flatMap(hostOn(machine), (host) => clientOn(host, capabilities));

const call = (
	client: McpTest.Client,
	name: string,
	args: Readonly<Record<string, unknown>>
) => Effect.map(client.callTool({ name, arguments: args }), answered);

/** The record of a run on the machine, as the command line reads it. */
const recordOf = (machine: Machine, run: RunId) =>
	Effect.gen(function* () {
		const runs = yield* Runs;
		return yield* runs.get(run);
	}).pipe(Effect.provide(Runs.layer.pipe(Layer.provide(machine.platform))));

const freed = (machine: Machine) =>
	machine.podman.containers.pipe(
		Effect.repeat({
			schedule: Schedule.spaced("20 millis"),
			until: (held) => held.size === 0,
		}),
		Effect.timeout("10 seconds"),
		Effect.option,
		Effect.map(Option.isSome)
	);

/** A kept script's name no other test or kept script uses, and the file it would be. */
const keptName = Effect.map(newRunId, (run) => `zz-test-${run}`);
const keptFile = (name: string) => `${packageRoot}/scripts/${name}.ts`;

describe("the MCP tools", () => {
	it.live(
		"run_script runs the body in a timed child and answers its outcome, its result and its picture as an image",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine({ onA: openChest });
				const client = yield* toolsOn(machine);
				const { error, text, images } = yield* call(client, "run_script", {
					about: "The chest Link stands before opens to A.",
					state: { base: "test-base" },
					body: `yield* game.press("A");\nconst opened = yield* game.check("${chest}", true);\nreturn { opened, shot: yield* game.screenshot("after-chest") };`,
				});
				expect(error).toBe(false);
				const answer = yield* Schema.decodeEffect(Schema.fromJsonString(Ran))(
					text
				);
				expect(answer.outcome).toEqual({ _tag: "passed" });
				expect(answer.result).toEqual({ opened: true, shot: "picture-1.png" });
				expect(images).toEqual([
					expect.objectContaining({
						type: "image",
						mimeType: "image/png",
						data: fakePng,
					}),
				]);
				const record = yield* recordOf(machine, answer.run);
				expect(record.script?.path).toBeUndefined();
				expect(record.script?.source).toContain('game.press("A")');
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a body that fails its type check comes back with the compiler's lines, numbered in the body, and no game starts",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const client = yield* toolsOn(machine);
				const { error, text } = yield* call(client, "run_script", {
					about: "A press of a button whose name is misspelled.",
					state: { base: "test-base" },
					body: 'yield* game.press("A");\nyield* game.press("Plsu");',
				});
				expect(error).toBe(true);
				expect(text).toContain("type check");
				expect(text).toContain(
					"body(2,19): error TS2345: Argument of type '\"Plsu\"'"
				);
				const creates = Arr.filter(
					yield* machine.podman.calls,
					(args) => args[0] === "create"
				);
				expect(creates).toEqual([]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"detach answers once the run has its record, and wait_run in another session reads its end from the record",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine({ onA: openChest });
				const host = yield* hostOn(machine);
				const detaching = yield* clientOn(host);
				const waiting = yield* clientOn(host);
				const detached = yield* call(detaching, "run_script", {
					about: "The chest Link stands before opens to A.",
					state: { base: "test-base" },
					body: `yield* game.press("A");\nconst opened = yield* game.check("${chest}", true);\nreturn { opened, shot: yield* game.screenshot("after-chest") };`,
					detach: true,
				});
				const { run } = yield* Schema.decodeEffect(Schema.fromJsonString(Ran))(
					detached.text
				);
				expect((yield* recordOf(machine, run)).id).toBe(run);
				const ended = yield* call(waiting, "wait_run", { run });
				const answer = yield* Schema.decodeEffect(Schema.fromJsonString(Ran))(
					ended.text
				);
				expect(answer.outcome).toEqual({ _tag: "passed" });
				expect(ended.images).toEqual([
					expect.objectContaining({ type: "image", data: fakePng }),
				]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"start_game holds a game for its session: screenshot answers an image, and stop_game ends the run stopped and frees the place",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const client = yield* toolsOn(machine);
				const started = yield* call(client, "start_game", {
					state: { base: "test-base" },
				});
				expect(started.error).toBe(false);
				const { session, run } = yield* Schema.decodeEffect(
					Schema.fromJsonString(Started)
				)(started.text);
				const shot = yield* call(client, "screenshot", { session });
				expect(shot.images).toEqual([
					expect.objectContaining({ type: "image", data: fakePng }),
				]);
				const stopped = yield* call(client, "stop_game", { session });
				expect(yield* Schema.decodeEffect(Json)(stopped.text)).toEqual({
					run,
					outcome: { _tag: "stopped", reason: "stop_game ended it" },
				});
				expect(yield* freed(machine)).toBe(true);
				const record = yield* recordOf(machine, run);
				expect(record.outcome).toEqual({
					_tag: "stopped",
					reason: "stop_game ended it",
				});
				expect(Arr.map(record.steps, (step) => step.command)).toEqual([
					"screenshot",
				]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"wait_run waits for a game started by hand to end",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const client = yield* toolsOn(machine);
				const started = yield* call(client, "start_game", {
					state: { base: "test-base" },
				});
				const { session, run } = yield* Schema.decodeEffect(
					Schema.fromJsonString(Started)
				)(started.text);
				const waiting = yield* Effect.forkChild(
					call(client, "wait_run", { run })
				);
				yield* Effect.sleep("2 seconds");
				yield* call(client, "stop_game", { session });
				const ended = yield* Fiber.join(waiting);
				expect(
					(yield* Schema.decodeEffect(Schema.fromJsonString(Ran))(ended.text))
						.outcome
				).toEqual({ _tag: "stopped", reason: "stop_game ended it" });
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.effect(
		"a game no call names for 10 minutes stops, and each call holds it 10 minutes more",
		() =>
			Effect.gen(function* () {
				// The fakes and the tools run on the live clock; the host's lease counts on the test clock.
				const machine = yield* TestClock.withLive(fakeMachine());
				return yield* Effect.gen(function* () {
					const client = yield* TestClock.withLive(
						McpTest.makeClient(registrations, {
							protocols: [McpProtocol.v2025_11_25],
						})
					);
					const started = yield* TestClock.withLive(
						call(client, "start_game", { state: { base: "test-base" } })
					);
					const { session, run } = yield* Schema.decodeEffect(
						Schema.fromJsonString(Started)
					)(started.text);
					yield* TestClock.adjust("9 minutes");
					const read = yield* TestClock.withLive(
						call(client, "read_values", {
							session,
							values: ["PlayerStatus.CurrentRupee"],
						})
					);
					expect(read.error).toBe(false);
					yield* TestClock.adjust("9 minutes");
					expect((yield* machine.podman.containers).size).toBe(1);
					yield* TestClock.adjust("2 minutes");
					expect(yield* TestClock.withLive(freed(machine))).toBe(true);
					const record = yield* recordOf(machine, run);
					expect(record.outcome).toEqual({
						_tag: "stopped",
						reason: "no call named its session for 10 minutes",
					});
				}).pipe(
					Effect.provide(Scripts.layer.pipe(Layer.provideMerge(machine.host)))
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"get_run answers a long record's outcome first, and its later steps a part at a time",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const client = yield* toolsOn(machine);
				const finished = yield* call(client, "run_script", {
					about: "Three hundred long notes.",
					state: { base: "test-base" },
					body: `for (let step = 0; step < 300; step++) {\n\tyield* game.note("note " + step + ": ${long}");\n}`,
				});
				const { run } = yield* Schema.decodeEffect(Schema.fromJsonString(Ran))(
					finished.text
				);
				const first = yield* call(client, "get_run", { run });
				expect(first.text.length).toBeLessThanOrEqual(20_000);
				const part = yield* Schema.decodeEffect(Schema.fromJsonString(Part))(
					first.text
				);
				expect(part.outcome).toEqual({ _tag: "unchecked" });
				expect(part.part.from).toBe(0);
				expect(part.part.of).toBe(300);
				expect(part.part.count).toBeLessThan(300);
				expect(part.next).toContain(`from: ${part.part.count}`);
				const second = yield* Schema.decodeEffect(Schema.fromJsonString(Part))(
					(yield* call(client, "get_run", { run, from: part.part.count })).text
				);
				expect(second.part.from).toBe(part.part.count);
				expect(second.steps[0]?.input).toEqual({
					text: `note ${part.part.count}: ${long}`,
				});
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"search_names finds a check by its spoiler log name, a spot and a base",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const client = yield* toolsOn(machine);
				const kinds = Effect.fnUntraced(function* (text: string) {
					const found = yield* call(client, "search_names", { text });
					const { names } = yield* Schema.decodeEffect(
						Schema.fromJsonString(
							Schema.Struct({
								names: Schema.Array(
									Schema.Struct({ kind: Schema.String, name: Schema.String })
								),
							})
						)
					)(found.text);
					return names;
				});
				expect(yield* kinds("Fake Chest")).toContainEqual({
					kind: "check",
					name: "Fake Chest Check",
				});
				expect(yield* kinds("Before The Chest")).toContainEqual({
					kind: "spot",
					name: "Fake Spot Before The Chest",
				});
				expect(yield* kinds("test-ba")).toContainEqual({
					kind: "base",
					name: "test-base",
				});
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"keep refuses a name a kept script holds, and leaves the file as it was",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const name = yield* keptName;
				yield* Effect.acquireRelease(
					fs.writeFileString(keptFile(name), "// Kept before the run.\n"),
					() => Effect.ignore(fs.remove(keptFile(name)))
				);
				const machine = yield* fakeMachine({ onA: openChest });
				const client = yield* toolsOn(machine);
				const { error, text } = yield* call(client, "run_script", {
					about: "The chest opens.",
					state: { base: "test-base" },
					body: `yield* game.press("A");\nreturn yield* game.check("${chest}", true);`,
					keep: name,
				});
				expect(error).toBe(true);
				expect(text).toContain(`scripts/${name}.ts`);
				expect(yield* fs.readFileString(keptFile(name))).toBe(
					"// Kept before the run.\n"
				);
				expect(
					Arr.filter(
						yield* machine.podman.calls,
						(args) => args[0] === "create"
					)
				).toEqual([]);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a broken run is not kept, and the answer says so",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const name = yield* keptName;
				yield* Effect.addFinalizer(() =>
					Effect.ignore(fs.remove(keptFile(name)))
				);
				const machine = yield* fakeMachine();
				const client = yield* toolsOn(machine);
				const { text } = yield* call(client, "run_script", {
					about: "A read of a value the game does not hold.",
					state: { base: "test-base" },
					body: 'return yield* game.read("0x0badf00d");',
					keep: name,
				});
				const answer = yield* Schema.decodeEffect(Schema.fromJsonString(Ran))(
					text
				);
				expect(answer.outcome).toMatchObject({ _tag: "broken" });
				expect(answer.kept).toBeUndefined();
				expect(answer.notKept).toContain("broken");
				expect(yield* fs.exists(keptFile(name))).toBe(false);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a failure answers its message, which says what to do next",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const client = yield* toolsOn(machine);
				const { error, text } = yield* call(client, "press", {
					session: "gone",
					buttons: ["A"],
				});
				expect(error).toBe(true);
				expect(text).toBe(
					"No game with session gone runs on this machine: it was stopped, its caller went away, or no call named it for 10 minutes. start_game starts another."
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a script's hand over asks the caller's user through the client, and a client that refuses leaves the run broken",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const client = yield* toolsOn(
					machine,
					McpSchema.ClientCapabilities.make({ elicitation: {} })
				);
				const { error, text } = yield* call(client, "run_script", {
					about: "A person opens the chest.",
					state: { base: "test-base" },
					body: `yield* game.handOver("Open the chest before Link.");\nreturn yield* game.check("${chest}", true);`,
				});
				expect(error).toBe(false);
				const answer = yield* Schema.decodeEffect(Json)(text);
				expect(answer).toMatchObject({
					outcome: {
						_tag: "broken",
						reason: "the script ended on NobodyThere",
					},
				});
				expect(text).toContain("declined");
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a hand over is done only when the user answers yes; no, a decline and a cancel find nobody",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const user = (answer: McpSchema.ElicitResult) =>
					McpSchema.McpServerClient.of({
						clientId: 1,
						protocolVersion: "2025-11-25",
						clientCapabilities: McpSchema.ClientCapabilities.make({
							elicitation: {},
						}),
						clientInfo: { name: "mcp.test.ts", version: "1" },
						initializePayload: {
							protocolVersion: "2025-11-25",
							capabilities: McpSchema.ClientCapabilities.make({
								elicitation: {},
							}),
							clientInfo: { name: "mcp.test.ts", version: "1" },
						},
						getClient: Effect.succeed({
							listRoots: () => Effect.die(new Error("not asked")),
							createMessage: () => Effect.die(new Error("not asked")),
							elicit: () => Effect.succeed(answer),
						}),
					});
				const handedOver = Effect.fnUntraced(function* (
					answer: McpSchema.ElicitResult
				) {
					const sessions = yield* Sessions;
					const games = yield* Game;
					const run = yield* newRunId;
					yield* sessions.handOversTo(run, personOf(user(answer)));
					const game = yield* games.start({
						run,
						owner: "mcp.test.ts",
						state: StartState.make({ base: "test-base" }),
					});
					const exit = yield* Effect.exit(game.handOver("Open the chest."));
					yield* games.finish(game, exit);
					return Exit.isSuccess(exit)
						? "done"
						: Exit.match(exit, {
								onSuccess: () => "done",
								onFailure: (cause) => String(cause),
							});
				}, Effect.scoped);
				const answers = yield* Effect.forEach(
					[
						McpSchema.ElicitAcceptResult.make({
							action: "accept",
							content: { done: true },
						}),
						McpSchema.ElicitAcceptResult.make({
							action: "accept",
							content: { done: false },
						}),
						McpSchema.ElicitDeclineResult.make({ action: "decline" }),
						McpSchema.ElicitDeclineResult.make({ action: "cancel" }),
					],
					handedOver,
					{ concurrency: 1 }
				).pipe(Effect.provide(machine.layer));
				expect(answers[0]).toBe("done");
				for (const answer of Arr.drop(answers, 1)) {
					expect(answer).toContain("The hand over was declined");
				}
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"mcp that cannot start says why on stderr, and nothing on stdout",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
				const home = yield* fs.makeTempDirectoryScoped();
				const path = yield* Config.String("PATH");
				const started = yield* ran(
					spawner,
					ChildProcess.make(
						process.execPath,
						[`${packageRoot}/src/main.ts`, "mcp"],
						{
							cwd: home,
							extendEnv: false,
							env: { PATH: path, HOME: home, XDG_RUNTIME_DIR: home },
						}
					)
				).pipe(Effect.timeout("30 seconds"));
				expect(started.stdout).toBe("");
				expect(started.code).not.toBe(0);
				expect(started.stderr).toContain("GAME_TESTING_");
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);
});
