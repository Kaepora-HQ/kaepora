import { BunServices, BunSocket } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	Context,
	Effect,
	Exit,
	Layer,
	Option,
	Redacted,
	Schedule,
	Scope,
	String as Str,
} from "effect";
import { McpClient, McpProtocol } from "effect/ai";
import { Command } from "effect/cli";
import {
	FetchHttpClient,
	HttpClient,
	HttpClientRequest,
	HttpServer,
} from "effect/http";
import { NetAddress } from "effect/net";
import { RpcClient } from "effect/rpc";
import { TestConsole } from "effect/testing";
import { cli } from "../src/cli.ts";
import { contract, GameRpcs } from "../src/contract/rpc.ts";
import { StartState } from "../src/contract/state.ts";
import { newRunId } from "../src/contract/values.ts";
import { connection } from "../src/far.ts";
import { Game } from "../src/game.ts";
import { Runs } from "../src/host/runs.ts";
import { Scripts } from "../src/scripts/runner.ts";
import { serve } from "../src/serve/server.ts";
import type { FakeLichii } from "./fake-lichii.ts";
import { fakeMachine } from "./fake-machine.ts";

const token = "a-token-made-for-this-test";
const openChest = (lichii: FakeLichii) => {
	Effect.runSync(lichii.put("IsTreasureBoxOpen.5005", true));
};
const lasting = { timeout: 60_000 };

type Machine = Effect.Success<ReturnType<typeof fakeMachine>>;

/** `serve` on a fake machine, on a port the system picks: where it listens, and its URL. */
const listening = (machine: Machine) =>
	Effect.gen(function* () {
		const server = Context.get(
			yield* Layer.build(
				serve({ port: 0, token: Redacted.make(token), version: "test" }).pipe(
					Layer.provide(Scripts.layer.pipe(Layer.provideMerge(machine.host)))
				)
			),
			HttpServer.HttpServer
		);
		const { address } = server;
		return address._tag === "UnixPathAddress"
			? yield* Effect.die(new Error("serve listens on a TCP port"))
			: {
					host: NetAddress.formatHost(address),
					url: `http://127.0.0.1:${address.port}`,
				};
	});

const served = (machine: Machine) =>
	Effect.map(listening(machine), ({ url }) => url);

const secret = Option.some(Redacted.make(token));

/** The status line serve answers a WebSocket upgrade of `/rpc` with, sent by hand: fetch sends no upgrade. */
const upgraded = (url: string, headers: string) =>
	Effect.scoped(
		Effect.gen(function* () {
			const socket = yield* BunSocket.makeNet({
				host: "127.0.0.1",
				port: Number(Str.split(url, ":")[2]),
			});
			const writer = yield* socket.writer;
			const reader = yield* socket.reader;
			yield* writer.write(
				`GET /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n${headers}\r\n`
			);
			const [first] = yield* reader.pull;
			const answer =
				typeof first === "string" ? first : new TextDecoder().decode(first);
			return Str.split(answer, "\r\n")[0];
		})
	);

const status = (request: HttpClientRequest.HttpClientRequest) =>
	Effect.map(HttpClient.execute(request), (response) => response.status);

const initialize = {
	jsonrpc: "2.0",
	id: 1,
	method: "initialize",
	params: {
		protocolVersion: "2025-11-25",
		capabilities: {},
		clientInfo: { name: "serve.test.ts", version: "1" },
	},
};

describe("serve", () => {
	it.live(
		"a request or an upgrade without the token gets 401, and a request with it reaches the tools",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const url = yield* served(machine);
				const mcp = HttpClientRequest.post(`${url}/mcp`).pipe(
					HttpClientRequest.setHeader(
						"accept",
						"application/json, text/event-stream"
					),
					HttpClientRequest.bodyJsonUnsafe(initialize)
				);
				expect(yield* status(mcp)).toBe(401);
				expect(
					yield* status(mcp.pipe(HttpClientRequest.bearerToken("wrong")))
				).toBe(401);
				expect(yield* upgraded(url, "")).toBe("HTTP/1.1 401 Unauthorized");
				expect(yield* upgraded(url, `Authorization: Bearer ${token}\r\n`)).toBe(
					"HTTP/1.1 101 Switching Protocols"
				);
				expect(
					yield* status(mcp.pipe(HttpClientRequest.bearerToken(token)))
				).toBe(200);
			}).pipe(
				Effect.scoped,
				Effect.provide([BunServices.layer, FetchHttpClient.layer])
			),
		lasting
	);

	it.live(
		"serve listens on 127.0.0.1 and on no other address",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				expect((yield* listening(machine)).host).toBe("127.0.0.1");
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a tool called over /mcp with the token answers",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const url = yield* served(machine);
				const client = Context.get(
					yield* Layer.build(
						Layer.effect(McpClient.McpClient)(
							McpClient.make({ name: "serve.test.ts", version: "1" })
						).pipe(
							Layer.provide(
								Layer.effect(McpClient.Transport)(
									McpClient.http({
										url: `${url}/mcp`,
										protocol: McpProtocol.v2025_11_25,
										headers: { authorization: `Bearer ${token}` },
									})
								)
							)
						)
					),
					McpClient.McpClient
				);
				const search = Arr.findFirst(
					yield* client.listTools(),
					(tool) => tool.name === "search_names"
				);
				const found = yield* client.callTool({
					tool: Option.getOrThrow(search),
					arguments: { text: "test-ba" },
				});
				expect(found.isError).not.toBe(true);
				expect(found.content).toContainEqual(
					expect.objectContaining({
						type: "text",
						text: expect.stringContaining('"name":"test-base"'),
					})
				);
			}).pipe(
				Effect.scoped,
				Effect.provide([BunServices.layer, FetchHttpClient.layer])
			),
		lasting
	);

	it.live(
		"a far caller with a wrong token is told the token is wrong",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const url = yield* served(machine);
				const refused = yield* Effect.flip(
					Layer.build(
						Game.layerRemote(url, Option.some(Redacted.make("wrong")))
					)
				);
				expect(refused).toMatchObject({
					_tag: "RemoteUnavailable",
					reason: "token",
				});
				expect(refused.message).toContain("GAME_TESTING_TOKEN");
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a far caller whose connection is lost fails at once, and nothing is sent again",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const scope = yield* Scope.make();
				const url = yield* served(machine).pipe(Scope.provide(scope));
				const games = Context.get(
					yield* Layer.build(Game.layerRemote(url, secret)),
					Game
				);
				const game = yield* games.start({
					owner: "serve.test.ts",
					state: StartState.make({ base: "test-base" }),
				});
				yield* Scope.close(scope, Exit.void);
				const lost = yield* game
					.press("A")
					.pipe(Effect.flip, Effect.timeout("10 seconds"));
				expect(lost._tag).toBe("RemoteUnavailable");
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"Hello over the WebSocket refuses a caller built from another contract",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const url = yield* served(machine);
				const far = yield* Layer.build(connection(url, secret));
				const client = yield* RpcClient.make(GameRpcs).pipe(
					Effect.provideContext(far)
				);
				const refused = yield* Effect.flip(
					client.Hello({ contract: contract + 1 })
				);
				expect(refused._tag).toBe("VersionMismatch");
				expect(refused.message).toContain(
					`speaks contract ${contract + 1} and this copy speaks ${contract}`
				);
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"a far caller that goes away ends its run broken and frees its place",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const url = yield* served(machine);
				const run = yield* newRunId;
				yield* Effect.scoped(
					Effect.gen(function* () {
						const games = Context.get(
							yield* Layer.build(Game.layerRemote(url, secret)),
							Game
						);
						yield* games.start({
							run,
							owner: "serve.test.ts",
							state: StartState.make({ base: "test-base" }),
						});
					})
				);
				const freed = yield* machine.podman.containers.pipe(
					Effect.repeat({
						schedule: Schedule.spaced("20 millis"),
						until: (held) => held.size === 0,
					}),
					Effect.timeout("10 seconds"),
					Effect.option
				);
				expect(Option.isSome(freed)).toBe(true);
				const record = yield* Effect.gen(function* () {
					const runs = yield* Runs;
					return yield* runs.get(run);
				}).pipe(
					Effect.provide(Runs.layer.pipe(Layer.provide(machine.platform)))
				);
				expect(record.outcome).toEqual({
					_tag: "broken",
					reason: "its caller went away before the script finished",
					error: "",
				});
			}).pipe(Effect.scoped, Effect.provide(BunServices.layer)),
		lasting
	);

	it.live(
		"run with GAME_TESTING_URL runs a kept script against the far machine, and runs reads its record there",
		() =>
			Effect.gen(function* () {
				const far = yield* fakeMachine({ onA: openChest });
				const url = yield* served(far);
				const here = yield* fakeMachine({
					env: { GAME_TESTING_URL: url, GAME_TESTING_TOKEN: token },
				});
				const exit = yield* Effect.exit(
					Command.runWith(cli, { version: "test" })([
						"run",
						`${import.meta.dir}/scripts/opens-chest.ts`,
					])
				).pipe(Effect.provide(here.platform));
				const lines = Arr.map(yield* TestConsole.logLines, String);
				expect(lines[0]).toStartWith("passed  ");
				expect(Exit.isSuccess(exit)).toBe(true);
				const run = Arr.findFirst(Str.split(lines[0] ?? "", "  "), (word) =>
					Str.startsWith("20")(word)
				);
				expect(Option.isSome(run)).toBe(true);
				expect(
					Arr.filter(yield* here.podman.calls, (args) => args[0] === "create")
				).toEqual([]);
				yield* Command.runWith(cli, { version: "test" })(["runs"]).pipe(
					Effect.provide(here.platform)
				);
				const listed = Arr.drop(
					Arr.map(yield* TestConsole.logLines, String),
					1
				);
				expect(listed[0]).toStartWith(`${Option.getOrThrow(run)}  passed`);
			}).pipe(
				Effect.scoped,
				Effect.provide([BunServices.layer, TestConsole.layer])
			),
		lasting
	);

	it.live(
		"a GAME_TESTING_URL with a password in it is refused, and the password is printed nowhere",
		() =>
			Effect.gen(function* () {
				const here = yield* fakeMachine({
					env: {
						GAME_TESTING_URL: "http://someone:hunter2@127.0.0.1:1",
						GAME_TESTING_TOKEN: token,
					},
				});
				const exit = yield* Effect.exit(
					Command.runWith(cli, { version: "test" })([
						"run",
						`${import.meta.dir}/scripts/opens-chest.ts`,
					])
				).pipe(Effect.provide(here.platform));
				expect(Exit.isFailure(exit)).toBe(true);
				const printed = Arr.join(
					Arr.map(
						[
							...(yield* TestConsole.logLines),
							...(yield* TestConsole.errorLines),
						],
						String
					),
					"\n"
				);
				expect(printed).toContain(
					"GAME_TESTING_URL holds a name or a password"
				);
				expect(printed).not.toContain("hunter2");
				expect(String(exit)).not.toContain("hunter2");
			}).pipe(
				Effect.scoped,
				Effect.provide([BunServices.layer, TestConsole.layer])
			),
		lasting
	);

	it.live("serve without a token does not start", () =>
		Effect.gen(function* () {
			const machine = yield* fakeMachine();
			const exit = yield* Effect.exit(
				Command.runWith(cli, { version: "test" })(["serve"])
			).pipe(Effect.provide(machine.platform));
			expect(Exit.isFailure(exit)).toBe(true);
			const errors = Arr.map(yield* TestConsole.errorLines, String);
			expect(Arr.join(errors, "\n")).toContain(
				"GAME_TESTING_TOKEN is not set, so serve does not start."
			);
		}).pipe(
			Effect.scoped,
			Effect.provide([BunServices.layer, TestConsole.layer])
		)
	);
});
