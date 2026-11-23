/**
 * `serve`: one HTTP server on loopback, for callers on other machines through a tunnel the operator runs. The commands
 * travel over a WebSocket at `/rpc`, where a game lives as long as its caller's connection; the MCP tools over HTTP at
 * `/mcp`. One middleware checks the token on every request before any route, the WebSocket upgrade included.
 */
import { BunHttpServer } from "@effect/platform-bun";
import type { Redacted } from "effect";
import { Effect, Layer, Redacted as Secret } from "effect";
import { McpServer } from "effect/ai";
import { Mac } from "effect/crypto";
import { HttpRouter, HttpServerRequest, HttpServerResponse } from "effect/http";
import { RpcSerialization, RpcServer } from "effect/rpc";
import { GameRpcs } from "../contract/rpc.ts";
import { startLimit } from "../contract/state.ts";
import { handlers } from "../host/handlers.ts";
import {
	instructions,
	protocols,
	registrations,
	serverName,
} from "../mcp/server.ts";

/** Only this machine connects; a tunnel brings the callers of other machines. */
const hostname = "127.0.0.1";

/** Seconds a WebSocket may stay silent, pings included, before Bun closes it and its game stops. */
const idleTimeout = 30;

const encoder = new TextEncoder();

/** Refuses with 401 a request whose `Authorization` is not `Bearer <token>`, compared in constant time. */
const tokenChecked = (token: Redacted.Redacted) => {
	const expected = encoder.encode(`Bearer ${Secret.value(token)}`);
	return HttpRouter.middleware(
		(app) =>
			Effect.gen(function* () {
				const request = yield* HttpServerRequest.HttpServerRequest;
				const given = encoder.encode(request.headers.authorization ?? "");
				return Mac.timingSafeEqual(given, expected)
					? yield* app
					: HttpServerResponse.text(
							"Send the token serve was started with, as Authorization: Bearer <token>.\n",
							{ status: 401 }
						);
			}),
		{ global: true }
	);
};

/** The server on `port` of loopback, with the host's commands, its MCP tools, and `token` checked first. */
export const serve = (options: {
	readonly port: number;
	readonly token: Redacted.Redacted;
	readonly version: string;
}) =>
	HttpRouter.serve(
		Layer.mergeAll(
			RpcServer.layerHttp({
				group: GameRpcs,
				path: "/rpc",
				protocol: "websocket",
				// One handler's defect fails that call, not every call on the connection.
				disableFatalDefects: true,
			}).pipe(
				Layer.provide(handlers),
				Layer.provide(RpcSerialization.layerJson)
			),
			McpServer.layerHttp({
				name: serverName,
				version: options.version,
				path: "/mcp",
				protocols,
				instructions,
				registrations,
			}),
			tokenChecked(options.token)
		)
	).pipe(
		Layer.provideMerge(
			BunHttpServer.layer({
				hostname,
				port: options.port,
				// A start carries its mod's files; Bun's own limit is 16 MiB.
				websocket: { idleTimeout, maxPayloadLength: startLimit },
			})
		)
	);
