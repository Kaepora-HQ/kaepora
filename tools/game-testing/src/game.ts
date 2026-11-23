/**
 * `Game`, what a script drives: an RPC client of `GameRpcs` over the socket of the host that started the script. The
 * code that drives the emulator lives only in the host.
 */
import { BunSocket } from "@effect/platform-bun";
import type { Redacted, Scope } from "effect";
import {
	Array as Arr,
	Cause,
	Context,
	Deferred,
	Duration,
	Effect,
	Exit,
	Fiber,
	FileSystem,
	Layer,
	Option,
	Path,
	Predicate,
	Record as Rec,
	Schema,
	Stream,
} from "effect";
import type { RpcClientError } from "effect/rpc";
import { RpcClient, RpcSerialization } from "effect/rpc";
import type { Outcome, ScriptSource } from "./contract/record.ts";
import { contract, GameRpcs } from "./contract/rpc.ts";
import type {
	ModFolder,
	ReadBack,
	StartState,
	TextSpeed,
} from "./contract/state.ts";
import type {
	Button,
	PlaceId,
	RunId,
	SessionId,
	StickPosition,
	Value,
} from "./contract/values.ts";
import { Buttons, newRunId, Picture } from "./contract/values.ts";
import type {
	CommandError,
	NobodyThere,
	ProvesNothing,
	StartError,
	ValueRefused,
	WaitTimedOut,
	WriteUnsupported,
} from "./errors.ts";
import { failureOf, RemoteUnavailable } from "./errors.ts";
import { connection, reached } from "./far.ts";
import { readMod } from "./mod.ts";
import type { ValueName, ValueOf } from "./names.ts";

/** A press lasts this many frames unless the script says otherwise: long enough for the game to see it. */
export const pressFrames = 4;

/** A wait without its own limit fails after this long. */
const defaultWait = Duration.seconds(10);

/** How long a start waits for a free place. */
export const defaultPlaceWait = Duration.minutes(10);

/** A started game. Every method is a step in the run's record. */
export interface RunningGame {
	/**
	 * Proves an effect of the script: holds when the value holds `expected` now and did not at the start. A check that
	 * does not hold fails the run, not the call. One whose start already held `expected` fails with `ProvesNothing`.
	 */
	readonly check: <Name extends ValueName>(
		value: Name,
		expected: ValueOf<Name>
	) => Effect.Effect<boolean, CommandError | ValueRefused | ProvesNothing>;
	/** Shows a person what to do and waits until they are done. */
	readonly handOver: (
		say: string
	) => Effect.Effect<void, CommandError | NobodyThere>;
	/** Lets `frames` pass with no input. */
	readonly idle: (frames: number) => Effect.Effect<void, CommandError>;
	/** A line in the record. */
	readonly note: (text: string) => Effect.Effect<void, CommandError>;
	readonly place: PlaceId;
	/** Holds the buttons together for `frames`, 4 unless given, then lets go. */
	readonly press: (
		buttons: Button | readonly [Button, ...Button[]],
		options?: { readonly frames?: number }
	) => Effect.Effect<void, CommandError>;
	readonly read: <Name extends ValueName>(
		value: Name
	) => Effect.Effect<ValueOf<Name>, CommandError>;
	/** Every value the start state wrote, as the game held it once Link could move. */
	readonly readBack: typeof ReadBack.Type;
	readonly run: RunId;
	readonly screenshot: (label?: string) => Effect.Effect<Picture, CommandError>;
	readonly session: SessionId;
	readonly set: <Name extends ValueName>(
		value: Name,
		to: ValueOf<Name>
	) => Effect.Effect<void, CommandError | ValueRefused | WriteUnsupported>;
	readonly stick: (
		position: StickPosition,
		options: { readonly frames: number }
	) => Effect.Effect<void, CommandError>;
	/**
	 * Proves the script left a value alone: holds when the value holds `expected` now and did at the start. One whose
	 * start did not hold `expected` fails with `ProvesNothing`.
	 */
	readonly still: <Name extends ValueName>(
		value: Name,
		expected: ValueOf<Name>
	) => Effect.Effect<boolean, CommandError | ValueRefused | ProvesNothing>;
	/** Waits until the value holds `expected`, failing with what it held when `within` passes, 10 seconds unless given. */
	readonly wait: <Name extends ValueName>(
		value: Name,
		expected: ValueOf<Name>,
		options?: { readonly within?: Duration.Input }
	) => Effect.Effect<ValueOf<Name>, CommandError | ValueRefused | WaitTimedOut>;
}

export interface StartOptions {
	/** The broken run this one repeats. */
	readonly attemptOf?: RunId | undefined;
	/** A mod folder on this machine, which travels to the game's; `none` for no mod; absent, the machine's mod that skips. */
	readonly mod?: ModFolder | "none" | undefined;
	/** Who asks, for the places list. */
	readonly owner: string;
	/** The run's name; a new one when absent. */
	readonly run?: RunId | undefined;
	readonly script?: typeof ScriptSource.Type | undefined;
	readonly state: StartState;
	readonly textSpeed?: TextSpeed | undefined;
	readonly waitForPlace?: Duration.Input | undefined;
}

/** A script's result as the record keeps it: each picture by its path in the run's folder. */
const kept = (value: unknown): unknown => {
	if (Schema.is(Picture)(value)) {
		return value.path;
	}
	if (Arr.isArray(value)) {
		return Arr.map(value, kept);
	}
	return Predicate.isObject(value) && !Predicate.isFunction(value)
		? Rec.map(value as Readonly<Record<string, unknown>>, kept)
		: value;
};

export class Game extends Context.Service<
	Game,
	{
		/** Starts a game in the state and returns once Link can move. The game stops when the scope closes. */
		readonly start: (
			options: StartOptions
		) => Effect.Effect<RunningGame, StartError, Scope.Scope>;
		/** Ends the run with how the script's body ended, and gives the run's outcome. */
		readonly finish: (
			game: RunningGame,
			ended: Exit.Exit<unknown, unknown>
		) => Effect.Effect<Outcome, CommandError>;
	}
>()("game-testing/game") {
	/** `Game` over the Unix socket of the host at `path`, which a script's child is given. */
	static readonly layer = (path: string) =>
		Game.on(path).pipe(
			Layer.provide(RpcClient.layerProtocolSocket()),
			Layer.provide(BunSocket.layerNet({ path })),
			Layer.provide(RpcSerialization.layerNdjson)
		);

	/** `Game` over a far machine's `serve` at `url`, with the token it was started with. */
	static readonly layerRemote = (
		url: string,
		token: Option.Option<Redacted.Redacted>
	) => Game.on(url).pipe(Layer.provide(connection(url, token)));

	/** `Game` on a connection to its host at `where`, after a `Hello` that checks both ends speak one contract. */
	private static readonly on = (where: string) =>
		Layer.effect(
			Game,
			Effect.gen(function* () {
				const client = yield* RpcClient.make(GameRpcs);
				const fs = yield* FileSystem.FileSystem;
				const path = yield* Path.Path;
				const remote = <A, E, R>(
					call: Effect.Effect<A, E | RpcClientError.RpcClientError, R>
				) => reached(where, call);
				yield* remote(client.Hello({ contract }));

				/** Each started session's `Open` stream, by the effect that waits for its end. */
				const streams = new Map<SessionId, Effect.Effect<unknown>>();
				const start = Effect.fn("Game.start")(function* (
					options: StartOptions
				) {
					const run = options.run ?? (yield* newRunId);
					const mod =
						options.mod === undefined || options.mod === "none"
							? options.mod
							: yield* readMod(fs, path, options.mod.folder);
					const started = yield* Deferred.make<
						{
							readonly session: SessionId;
							readonly place: PlaceId;
							readonly readBack: typeof ReadBack.Type;
						},
						StartError
					>();
					const open = yield* client
						.Open({
							run,
							state: options.state,
							owner: options.owner,
							waitForPlace: Duration.fromInputUnsafe(
								options.waitForPlace ?? defaultPlaceWait
							),
							...(mod === undefined ? {} : { mod }),
							...(options.textSpeed === undefined
								? {}
								: { textSpeed: options.textSpeed }),
							...(options.script === undefined
								? {}
								: { script: options.script }),
							...(options.attemptOf === undefined
								? {}
								: { attemptOf: options.attemptOf }),
						})
						.pipe(
							Stream.runForEach((event) =>
								event._tag === "started"
									? Deferred.succeed(started, event)
									: Effect.void
							),
							remote,
							Effect.exit,
							Effect.flatMap((exit) =>
								Deferred.failCause(
									started,
									Exit.isFailure(exit)
										? exit.cause
										: Cause.fail(
												RemoteUnavailable.make({ url: where, reason: "closed" })
											)
								)
							),
							Effect.forkScoped
						);
					const { session, place, readBack } = yield* Deferred.await(started);
					streams.set(session, Fiber.await(open));
					const call = remote;
					/** The host read the value by the type the game data list gives it, which the generated names give too. */
					const typed = <Name extends ValueName>(_name: Name, value: Value) =>
						value as ValueOf<Name>;
					const game: RunningGame = {
						session,
						run,
						place,
						readBack,
						press: (buttons, pressing) =>
							call(
								client.Press({
									session,
									buttons: Buttons.make(
										typeof buttons === "string" ? [buttons] : buttons
									),
									frames: pressing?.frames ?? pressFrames,
								})
							),
						stick: (position, { frames }) =>
							call(client.Stick({ session, position, frames })),
						idle: (frames) => call(client.Idle({ session, frames })),
						wait: (value, expected, waiting) =>
							Effect.map(
								call(
									client.Wait({
										session,
										value,
										expected,
										within: Duration.fromInputUnsafe(
											waiting?.within ?? defaultWait
										),
									})
								),
								(held) => typed(value, held)
							),
						read: (value) =>
							Effect.map(
								call(client.Read({ session, values: [value] })),
								(held) => typed(value, Option.getOrThrow(Rec.get(held, value)))
							),
						set: (value, to) =>
							call(client.Set({ session, values: { [value]: to } })),
						check: (value, expected) =>
							Effect.map(
								call(client.Check({ session, form: "check", value, expected })),
								(checked) => checked.held
							),
						still: (value, expected) =>
							Effect.map(
								call(client.Check({ session, form: "still", value, expected })),
								(checked) => checked.held
							),
						screenshot: (label) =>
							call(
								client.Screenshot({
									session,
									...(label === undefined ? {} : { label }),
								})
							),
						handOver: (say) =>
							Effect.asVoid(
								call(
									client.HandOver({
										session,
										say,
										within: Duration.minutes(10),
									})
								)
							),
						note: (text) => call(client.Note({ session, text })),
					};
					return game;
				});

				/**
				 * The host ends a session's `Open` stream once its run is finished. The client waits for that end, so it leaves
				 * no answer of the host's unread when it closes its connection.
				 */
				const finish = Effect.fn("Game.finish")(function* (
					game: RunningGame,
					ended: Exit.Exit<unknown, unknown>
				) {
					const how = Exit.match(ended, {
						onFailure: (cause) => ({ failure: failureOf(cause) }),
						onSuccess: (result) =>
							result === undefined ? {} : { result: kept(result) },
					});
					const outcome = yield* remote(
						client.Finish({ session: game.session, ...how })
					);
					yield* streams.get(game.session) ?? Effect.void;
					streams.delete(game.session);
					return outcome;
				});

				return Game.of({ start, finish });
			})
		);
}
