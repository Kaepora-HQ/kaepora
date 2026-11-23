/**
 * The machine whose game a caller on this machine drives: this machine's host, or with `GAME_TESTING_URL` a far
 * machine's `serve`. A script's child reaches it by flags of its own; the MCP tools and the command line send it
 * commands. Either way a run's record, pictures and saves end up in a folder on this machine.
 *
 * Its commands travel on a connection made by the first call that needs one and checked with `Hello`. Calls share it
 * while one holds it, and the first call after it was lost makes another, so a caller that lives long, such as `mcp`,
 * outlives a far `serve` that restarts. A call in flight when the connection is lost fails and is not sent again: a
 * command sent twice could press twice.
 */
import { BunSocket } from "@effect/platform-bun";
import type { Scope } from "effect";
import {
	Array as Arr,
	Cache,
	Context,
	Duration,
	Effect,
	FileSystem,
	Layer,
	Option,
	Path,
	RcRef,
	Redacted,
	Ref,
	Schedule,
} from "effect";
import { RpcClient, RpcSerialization } from "effect/rpc";
import type { RunRecord } from "./contract/record.ts";
import { contract, GameRpcs } from "./contract/rpc.ts";
import type { RunId } from "./contract/values.ts";
import {
	farNamesFile,
	namesHere,
	writeFarDeclarations,
} from "./data/declarations.ts";
import type {
	DumpFailed,
	NoGameHere,
	RemoteUnavailable,
	RunNotFound,
	StartError,
	VersionMismatch,
} from "./errors.ts";
import { CopyFailed } from "./errors.ts";
import { connection, reached, token } from "./far.ts";
import type { Client, Opened, Opening } from "./game.ts";
import { open } from "./game.ts";
import { HostSocket } from "./host/host.ts";
import { Runs } from "./host/runs.ts";
import type { Person } from "./host/sessions.ts";
import { Sessions } from "./host/sessions.ts";

/** Why a machine could not be asked. */
export type Unreachable = RemoteUnavailable | VersionMismatch;

type NamesError = Unreachable | NoGameHere | DumpFailed;

/**
 * An ended run as it is on this machine: its final record and its folder here, or why its files are not here, with the
 * record as the game machine holds it when that could be read.
 */
export type Landed =
	| { readonly record: RunRecord; readonly folder: string }
	| { readonly record?: RunRecord; readonly notCopied: CopyFailed };

/** How long a connection nobody holds stays open for the next call. */
const connectionIdle = Duration.seconds(30);

/** How long a run's record may still say running once its game is gone, and how often it is read meanwhile. */
const finalLimit = Duration.minutes(1);
const lookAgain = Duration.millis(500);

/** How many far runs' copies a caller remembers, so each is made once whoever asks. */
const copiesKept = 200;

export class Machine extends Context.Service<
	Machine,
	{
		/** A script's child's flags that name the machine. */
		readonly flags: readonly string[];
		/** What a script's child's environment holds besides this process's, such as a far machine's token. */
		readonly env: Readonly<Record<string, string>>;
		/** Writes the machine's game's names where a script's type check reads them, and gives what it includes. */
		readonly names: Effect.Effect<string, NamesError>;
		/** The commands, on a connection held while the scope lasts. */
		readonly connection: Effect.Effect<Client, Unreachable, Scope.Scope>;
		/** One command, on the connection held, or on a new one. */
		readonly call: <A, E, R>(
			command: (client: Client) => Effect.Effect<A, E, R>
		) => Effect.Effect<
			A,
			Exclude<E, { readonly _tag: "RpcClientError" }> | Unreachable,
			Exclude<R, Scope.Scope>
		>;
		/** Starts a game that lives while the scope lasts, and answers once Link can move. */
		readonly open: (
			opening: Opening
		) => Effect.Effect<Opened, StartError | Unreachable, Scope.Scope>;
		/** A run's record: this machine's copy once it has one, else the machine's own. */
		readonly record: (
			run: RunId
		) => Effect.Effect<RunRecord, RunNotFound | Unreachable>;
		/** A picture or a kept save's file of a run, by its name in the run's folder, from where `record` reads. */
		readonly file: (
			run: RunId,
			path: string
		) => Effect.Effect<Uint8Array, RunNotFound | Unreachable>;
		/**
		 * An ended run on this machine, once its host has written its outcome, which it does last. A far run's record,
		 * pictures and saves are copied here first, once, whoever asks: a copy that failed answers why each time.
		 */
		readonly landed: (run: RunId) => Effect.Effect<Landed, RunNotFound>;
		/** Sends the hand overs of the run `run` to `person` while the scope lasts. Without one, a run has nobody. */
		readonly handOversTo: (
			run: RunId,
			person: Person
		) => Effect.Effect<void, never, Scope.Scope>;
	}
>()("game-testing/machine") {
	/** This machine's host, over its socket. */
	static readonly layer = Layer.effect(
		Machine,
		Effect.gen(function* () {
			const socket = yield* HostSocket;
			const runs = yield* Runs;
			const sessions = yield* Sessions;
			const commands = yield* commandsOn(
				socket.path,
				// The host's own socket listens while this layer lives.
				RpcClient.layerProtocolSocket({ retryPolicy: Schedule.recurs(0) }).pipe(
					Layer.provide(BunSocket.layerNet({ path: socket.path })),
					Layer.provide(RpcSerialization.layerNdjson),
					Layer.orDie
				),
				() => Effect.succeed(namesHere)
			);
			return Machine.of({
				...commands,
				flags: ["--host", socket.path],
				env: {},
				record: runs.get,
				file: runs.file,
				landed: (run) =>
					finalOf(run, runs.get).pipe(
						Effect.map(
							(record): Landed => ({ record, folder: runs.folder(run) })
						),
						Effect.catchTag("CopyFailed", (notCopied) =>
							Effect.succeed<Landed>({ notCopied })
						)
					),
				handOversTo: sessions.handOversTo,
			});
		})
	);

	/**
	 * The far machine's `serve` at `url`, with the token from `GAME_TESTING_TOKEN` or its file. Scripts run here; only
	 * their commands and results cross. Its names are written here before a script's type check, so the check reads the
	 * far game's. That machine keeps the records, and each run's record, pictures and saves are copied here. Its host
	 * has no way to ask a person on this machine, so a hand over in a far run finds nobody.
	 */
	static readonly layerRemote = (url: string) =>
		Layer.effect(
			Machine,
			Effect.gen(function* () {
				const secret = yield* token;
				const runs = yield* Runs;
				const fs = yield* FileSystem.FileSystem;
				const path = yield* Path.Path;
				const namesFile = yield* farNamesFile;
				const commands = yield* commandsOn(
					url,
					connection(url, secret),
					(client) =>
						Effect.flatMap(reached(url, client.Names()), (names) =>
							writeFarDeclarations(fs, path, namesFile, names)
						)
				);
				const { call } = commands;
				const failed = (run: RunId, reason: string) =>
					CopyFailed.make({ run, reason });
				/** Its pictures' and saves' files, fetched from the game machine and written here beside its record. */
				const copy = Effect.fnUntraced(function* (record: RunRecord) {
					const run = record.id;
					const files = yield* Effect.forEach(
						[
							...Arr.flatMap(record.steps, (step) => step.pictures ?? []),
							...Arr.flatMap(record.saves ?? [], (save) => [
								save.progress.path,
								save.caption.path,
							]),
						],
						(file) =>
							call((client) => client.RunFile({ run, path: file })).pipe(
								Effect.map((bytes) => ({ path: file, bytes })),
								Effect.catchTags({
									RunNotFound: () =>
										Effect.fail(
											failed(run, `the game machine has no file ${file}`)
										),
									RemoteUnavailable: (lost) =>
										Effect.fail(failed(run, lost.message)),
									VersionMismatch: (other) =>
										Effect.fail(failed(run, other.message)),
								})
							),
						{ concurrency: 1 }
					);
					yield* runs.copy(record, files);
				});
				const land = Effect.fnUntraced(function* (run: RunId) {
					// A copy here is written only once its record is final.
					const here = yield* Effect.option(runs.get(run));
					if (Option.isSome(here)) {
						const kept: Landed = {
							record: here.value,
							folder: runs.folder(run),
						};
						return kept;
					}
					const record = yield* finalOf(run, (each) =>
						call((client) => client.Run({ run: each }))
					).pipe(
						Effect.catchTags({
							RemoteUnavailable: (lost) =>
								Effect.fail(failed(run, lost.message)),
							VersionMismatch: (other) =>
								Effect.fail(failed(run, other.message)),
						})
					);
					const landed: Landed = yield* copy(record).pipe(
						Effect.as<Landed>({ record, folder: runs.folder(run) }),
						Effect.catchTag("CopyFailed", (notCopied) =>
							Effect.succeed<Landed>({ record, notCopied })
						)
					);
					return landed;
				});
				const copies = yield* Cache.makeWith(
					(run: RunId) =>
						land(run).pipe(
							Effect.catchTag("CopyFailed", (notCopied) =>
								Effect.succeed<Landed>({ notCopied })
							)
						),
					{ capacity: copiesKept }
				);
				return Machine.of({
					...commands,
					flags: ["--host", url],
					env: Option.match(secret, {
						onNone: () => ({}),
						onSome: (held) => ({ GAME_TESTING_TOKEN: Redacted.value(held) }),
					}),
					record: (run) =>
						Effect.catchTag(runs.get(run), "RunNotFound", () =>
							call((client) => client.Run({ run }))
						),
					file: (run, file) =>
						Effect.catchTag(runs.file(run, file), "RunNotFound", () =>
							call((client) => client.RunFile({ run, path: file }))
						),
					landed: (run) => Cache.get(copies, run),
					handOversTo: () => Effect.void,
				});
			})
		);
}

/** A run's record once its outcome is written, which its host does last, after the game stops and its place is free. */
const finalOf = <E>(
	run: RunId,
	read: (run: RunId) => Effect.Effect<RunRecord, E>
) =>
	read(run).pipe(
		Effect.repeat({
			schedule: Schedule.spaced(lookAgain),
			until: (record) => record.outcome._tag !== "running",
		}),
		Effect.timeoutOrElse({
			duration: finalLimit,
			orElse: () =>
				Effect.fail(
					CopyFailed.make({
						run,
						reason: `its record still said running after ${Duration.format(finalLimit)}`,
					})
				),
		})
	);

/** A connection to a host: its commands, and its game's names, written once while it lasts, at the first ask. */
interface Connection {
	readonly client: Client;
	readonly names: Effect.Effect<string, NamesError>;
}

/**
 * The commands of the host at `where`, on connections `protocol` makes: each checked with `Hello`, shared while one
 * holds it, closed once nobody has held it for `connectionIdle`, and given up once it is lost, so the next call makes
 * another. `namesOf` writes the host's names for a type check.
 */
const commandsOn = Effect.fnUntraced(function* (
	where: string,
	protocol: Layer.Layer<RpcClient.Protocol, Unreachable>,
	namesOf: (client: Client) => Effect.Effect<string, NamesError>
) {
	const connections: RcRef.RcRef<Connection, Unreachable> = yield* RcRef.make({
		acquire: Effect.gen(function* () {
			// A socket closed as its connection is let go is no loss, and must not give up the one in use.
			const released = yield* Ref.make(false);
			const lost = Layer.succeed(
				RpcClient.ConnectionHooks,
				RpcClient.ConnectionHooks.of({
					onConnect: Effect.void,
					onDisconnect: Effect.flatMap(Ref.get(released), (gone) =>
						gone ? Effect.void : RcRef.invalidate(connections)
					),
				})
			);
			const made = yield* Layer.build(Layer.provideMerge(protocol, lost));
			yield* Effect.addFinalizer(() => Ref.set(released, true));
			const client = yield* RpcClient.make(GameRpcs).pipe(
				Effect.provideContext(made)
			);
			yield* reached(where, client.Hello({ contract }));
			const names = yield* Effect.cached(namesOf(client));
			const held: Connection = { client, names };
			return held;
		}),
		idleTimeToLive: connectionIdle,
	});
	const held = RcRef.get(connections);
	return {
		connection: Effect.map(held, (each) => each.client),
		call: <A, E, R>(command: (client: Client) => Effect.Effect<A, E, R>) =>
			Effect.scoped(
				Effect.flatMap(held, (each) => reached(where, command(each.client)))
			),
		names: Effect.scoped(Effect.flatMap(held, (each) => each.names)),
		open: (opening: Opening) =>
			Effect.flatMap(held, (each) => open(each.client, where, opening)),
	};
});
