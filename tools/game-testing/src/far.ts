/**
 * A far machine's `serve`, as `GAME_TESTING_URL` and `GAME_TESTING_TOKEN` name it. The URL is read so that no secret
 * in it can reach a command line or an error; a far machine is asked first whether it answers and takes the token, so
 * a refusal names its cause; and the commands then travel over its WebSocket.
 */
import { BunSocket } from "@effect/platform-bun";
import {
	Config,
	Context,
	Effect,
	Layer,
	Option,
	Redacted,
	Schedule,
	String as Str,
} from "effect";
import { FetchHttpClient, HttpClient, HttpClientRequest } from "effect/http";
import type { RpcClientError } from "effect/rpc";
import { RpcClient, RpcSerialization } from "effect/rpc";
import { contract, GameRpcs } from "./contract/rpc.ts";
import { RemoteUnavailable, UrlRefused } from "./errors.ts";

/** `GAME_TESTING_URL`, as an http or https URL with no name or password, and no `/` at its end. */
export const farUrl = Effect.gen(function* () {
	// Read as a secret, so that a refusal never prints the value.
	const set = yield* Config.option(Config.Redacted("GAME_TESTING_URL"));
	if (Option.isNone(set)) {
		return Option.none<string>();
	}
	const url = URL.parse(Redacted.value(set.value));
	if (
		url === null ||
		!(url.protocol === "http:" || url.protocol === "https:")
	) {
		return yield* UrlRefused.make({ reason: "not-http" });
	}
	if (url.username !== "" || url.password !== "") {
		return yield* UrlRefused.make({ reason: "secret" });
	}
	return Option.some(Str.replace(trailingSlash, "")(url.href));
});

const trailingSlash = /\/$/u;

/** `GAME_TESTING_TOKEN`; an empty one is no token. */
export const token = Effect.map(
	Config.option(Config.Redacted("GAME_TESTING_TOKEN")),
	Option.filter((secret) => Redacted.value(secret) !== "")
);

const bearer = (secret: Option.Option<Redacted.Redacted>) =>
	Option.match(secret, {
		onNone: () => ({}),
		onSome: (held) => ({ authorization: `Bearer ${Redacted.value(held)}` }),
	});

/** Fails with why the far machine at `url` cannot be reached, before any WebSocket is opened to it. */
const reach = (url: string, secret: Option.Option<Redacted.Redacted>) =>
	HttpClient.execute(
		HttpClientRequest.get(`${url}/rpc`).pipe(
			HttpClientRequest.setHeaders(bearer(secret))
		)
	).pipe(
		Effect.mapError(() => RemoteUnavailable.make({ url, reason: "connect" })),
		Effect.flatMap((response) =>
			response.status === 401
				? Effect.fail(RemoteUnavailable.make({ url, reason: "token" }))
				: Effect.void
		),
		Effect.scoped
	);

/** `serve`'s URL is http or https; its WebSocket is ws or wss at the same place. */
const httpScheme = /^http/u;

/**
 * The commands over a far machine's `serve`: a WebSocket at its `/rpc`, the token on the upgrade. A lost connection
 * fails every call in flight and every later one, and is not opened again: the game ended with it, and a command sent
 * again could press twice.
 */
export const connection = (
	url: string,
	secret: Option.Option<Redacted.Redacted>
) =>
	RpcClient.layerProtocolSocket({ retryPolicy: Schedule.recurs(0) }).pipe(
		Layer.provide(
			BunSocket.layerWebSocket(Str.replace(httpScheme, "ws")(`${url}/rpc`), {
				headers: bearer(secret),
			})
		),
		Layer.provide(RpcSerialization.layerJson),
		Layer.provide(
			Layer.effectDiscard(reach(url, secret)).pipe(
				Layer.provide(FetchHttpClient.layer)
			)
		)
	);

/** A far machine's commands, after a `Hello` that checks both ends speak one contract. */
export class Far extends Context.Service<
	Far,
	{
		readonly url: string;
		readonly token: Option.Option<Redacted.Redacted>;
		readonly client: RpcClient.FromGroup<
			typeof GameRpcs,
			RpcClientError.RpcClientError
		>;
	}
>()("game-testing/far") {
	static readonly layer = (url: string) =>
		Layer.unwrap(
			Effect.map(token, (secret) =>
				Layer.effect(
					Far,
					Effect.gen(function* () {
						const client = yield* RpcClient.make(GameRpcs);
						yield* client
							.Hello({ contract })
							.pipe(
								Effect.catchTag("RpcClientError", () =>
									Effect.fail(RemoteUnavailable.make({ url, reason: "closed" }))
								)
							);
						return Far.of({ url, token: secret, client });
					})
				).pipe(Layer.provide(connection(url, secret)))
			)
		);
}
