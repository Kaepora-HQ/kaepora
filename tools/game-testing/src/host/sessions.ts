/**
 * The games a host runs, by session. A game lives in the scope of whoever holds it, so it stops and gives its place back
 * however that scope ends: an `Open` stream's handler, or, for a caller with no connection to hold it, this host on an
 * idle lease. Every command a game takes is a step in its run's record.
 */
import { hostname } from "node:os";
import {
	Array as Arr,
	Clock,
	Config,
	Context,
	Deferred,
	Duration,
	Effect,
	Equal,
	Exit,
	FileSystem,
	Formatter,
	Layer,
	Option,
	Path,
	RcMap,
	Record as Rec,
	Schedule,
	Schema,
	Scope,
} from "effect";
import { SecureRandom } from "effect/crypto";
import { UserDirectories } from "effect/persistence";
import type { Outcome, ScriptSource, Step } from "../contract/record.ts";
import type {
	Mod,
	ReadBack,
	StartState,
	TextSpeed,
} from "../contract/state.ts";
import type {
	Buttons,
	Picture,
	PlaceId,
	RunId,
	StickPosition,
} from "../contract/values.ts";
import { SessionId, Value } from "../contract/values.ts";
import type { GameValue } from "../data/game-data.ts";
import { GameData } from "../data/game-data.ts";
import type { EmulatorError } from "../emulator/emulator.ts";
import { encodeValue, entryOf } from "../emulator/values.ts";
import type {
	DumpFailed,
	EmulatorUnavailable,
	ImageBuildFailed,
	LaunchFailed,
	NoFreePlace,
	NoGameHere,
	NoGraphicsCard,
	SaveUnreadable,
	StartNotTaken,
	StateRefused,
	UnknownName,
	ValueNotFound,
	ValueRefused,
	ValueUnreadable,
	WriteUnsupported,
} from "../errors.ts";
import {
	failureOf,
	ModUnreadable,
	NobodyThere,
	ProvesNothing,
	SessionNotFound,
	WaitTimedOut,
} from "../errors.ts";
import { digestOf, readMod } from "../mod.ts";
import { decode, read } from "../save/codec.ts";
import type { Plan } from "../save/state.ts";
import { StartStates } from "../save/state.ts";
import { Boot } from "./boot.ts";
import { Places } from "./places.ts";
import { Runs } from "./runs.ts";

/**
 * Who answers a run's hand overs: asks a person to do what `say` says in the game, and succeeds once they are done.
 * It owns the wait, up to `within`.
 */
export type Person = (
	say: string,
	within: Duration.Duration
) => Effect.Effect<void, NobodyThere>;

/** A run with nobody to hand over to, as in CI. */
const nobody: Person = (say) =>
	Effect.fail(NobodyThere.make({ ask: say, reason: "no-person" }));

/** A kept game stops when no call names its session for this long. */
const leaseIdle = Duration.minutes(10);
const leaseEnded = "no call named its session for 10 minutes";

/** A kept game, and the scope it lives in, which only its lease's end closes. */
interface Lease {
	readonly game: HostGame;
	readonly scope: Scope.Closeable;
}

/** How often a wait reads its value. */
const waitSpacing = Duration.millis(100);
const screenshotHeight = 720;

/** What a host needs to start one game. */
export interface Start {
	readonly attemptOf?: RunId | undefined;
	/** The mod the place gets alone; `none` for none; absent, the machine's own mod that skips. */
	readonly mod?: Mod | "none" | undefined;
	/** Shown in the places list: a script's path, `mcp`, or a CI job's name. */
	readonly owner: string;
	readonly run: RunId;
	readonly script?: typeof ScriptSource.Type | undefined;
	readonly state: StartState;
	/** `instant` unless given. */
	readonly textSpeed?: TextSpeed | undefined;
	readonly waitForPlace: Duration.Duration;
}

export type StartError =
	| ModUnreadable
	| UnknownName
	| StateRefused
	| ValueRefused
	| SaveUnreadable
	| NoGameHere
	| DumpFailed
	| NoFreePlace
	| NoGraphicsCard
	| ImageBuildFailed
	| LaunchFailed
	| EmulatorUnavailable
	| StartNotTaken;

type ReadError = EmulatorError | UnknownName | ValueNotFound | ValueUnreadable;

/** A started game on this host, as the commands of `GameRpcs` drive it. */
export interface HostGame {
	readonly check: (
		form: "check" | "still",
		value: string,
		expected: Value
	) => Effect.Effect<
		{ readonly actual: Value; readonly held: boolean },
		ReadError | ValueRefused | ProvesNothing
	>;
	/** Ends the run with `outcome`, unless it has one already. */
	readonly end: (outcome: Outcome) => Effect.Effect<void>;
	/** The run's end: its outcome, from the failure the script ended on or from its checks. */
	readonly finish: (end: {
		readonly result?: unknown;
		readonly failure?: { readonly tag: string; readonly message: string };
	}) => Effect.Effect<Outcome>;
	/** Done once the run is finished, so the session's `Open` stream can end. */
	readonly finished: Effect.Effect<void>;
	readonly handOver: (
		say: string,
		within: Duration.Duration
	) => Effect.Effect<void, NobodyThere>;
	readonly idle: (frames: number) => Effect.Effect<void, EmulatorError>;
	readonly note: (text: string) => Effect.Effect<void>;
	readonly place: PlaceId;
	readonly press: (
		buttons: Buttons,
		frames: number
	) => Effect.Effect<void, EmulatorError>;
	readonly read: (
		values: readonly string[]
	) => Effect.Effect<Readonly<Record<string, Value>>, ReadError>;
	readonly readBack: typeof ReadBack.Type;
	readonly run: RunId;
	readonly screenshot: (
		label: string | undefined
	) => Effect.Effect<Picture, EmulatorError>;
	readonly session: SessionId;
	readonly set: (
		values: Readonly<Record<string, Value>>
	) => Effect.Effect<void, ReadError | ValueRefused | WriteUnsupported>;
	readonly stick: (
		position: StickPosition,
		frames: number
	) => Effect.Effect<void, EmulatorError>;
	readonly wait: (
		value: string,
		expected: Value,
		within: Duration.Duration
	) => Effect.Effect<Value, ReadError | ValueRefused | WaitTimedOut>;
}

/** Failures that are the game's answer; any other failure means the run could not ask. */
const answers = new Set(["WaitTimedOut"]);

/** The outcome of a run that ended on `failure`, or else on its checks. */
const outcomeOf = (
	steps: readonly Step[],
	failure: { readonly tag: string; readonly message: string } | undefined
): Outcome => {
	if (failure !== undefined) {
		return answers.has(failure.tag)
			? { _tag: "failed", reason: failure.message }
			: {
					_tag: "broken",
					reason: `the script ended on ${failure.tag}`,
					error: failure.message,
				};
	}
	const checks = Arr.flatMapNullishOr(steps, (step) => step.check);
	if (checks.length === 0) {
		return { _tag: "unchecked" };
	}
	const missed = Arr.findFirst(checks, (check) => !check.held);
	return Option.match(missed, {
		onNone: (): Outcome => ({ _tag: "passed" }),
		onSome: (check): Outcome => ({
			_tag: "failed",
			reason: `${check.value} held ${String(check.actual)}, not ${String(check.expected)}`,
		}),
	});
};

/** The value as JSON writes it, for the word a message suggests. */
const asJson = (value: Value) => Formatter.formatJson(value);

export class Sessions extends Context.Service<
	Sessions,
	{
		/** Starts a game that lives in the caller's scope. The state is planned before a place is taken. */
		readonly hold: (
			start: Start
		) => Effect.Effect<HostGame, StartError, Scope.Scope>;
		/**
		 * Starts a game that lives in this host on an idle lease, for a caller with no connection to hold it: it stops
		 * when no call names its session for 10 minutes, at `stop`, or when the host stops.
		 */
		readonly keep: (start: Start) => Effect.Effect<HostGame, StartError>;
		/** A session's game. A call that names a kept game's session renews its lease. */
		readonly get: (
			session: SessionId
		) => Effect.Effect<HostGame, SessionNotFound>;
		/** Stops a kept game, gives its place back, and ends its run `stopped` for `reason`. */
		readonly stop: (
			session: SessionId,
			reason: string
		) => Effect.Effect<HostGame, SessionNotFound>;
		/** Sends the hand overs of the run `run` to `person` while the scope lasts. Without one, a run has nobody. */
		readonly handOversTo: (
			run: RunId,
			person: Person
		) => Effect.Effect<void, never, Scope.Scope>;
	}
>()("game-testing/host/sessions") {
	static readonly layer = Layer.effect(
		Sessions,
		Effect.gen(function* () {
			const data = yield* GameData;
			const states = yield* StartStates;
			const places = yield* Places;
			const runs = yield* Runs;
			const boot = yield* Boot;
			const program = yield* Config.String("GAME_TESTING_LICHII");
			const fs = yield* FileSystem.FileSystem;
			const path = yield* Path.Path;
			const directories = yield* UserDirectories.UserDirectories;
			const random = yield* SecureRandom.SecureRandom;
			const machine = hostname();
			const skips = `${yield* Effect.orDie(directories.data("game-testing"))}/skips`;
			const staging = `${yield* Effect.orDie(directories.cache("game-testing"))}/mods`;

			/** The mod a start names: the caller's, none, or the machine's own that skips. */
			const modOf = (mod: Start["mod"]) => {
				if (mod === "none") {
					return Effect.succeedNone;
				}
				return mod === undefined
					? readMod(fs, path, skips).pipe(
							Effect.mapError(() =>
								ModUnreadable.make({ folder: skips, reason: "no-skips" })
							),
							Effect.asSome
						)
					: Effect.succeedSome(mod);
			};

			/** Writes a mod's files into a folder of its own for as long as the scope lasts, and gives the folder. */
			const staged = Effect.fnUntraced(function* (mod: Mod) {
				yield* fs.makeDirectory(staging, { recursive: true });
				const folder = `${yield* fs.makeTempDirectoryScoped({ directory: staging })}/${mod.name}`;
				for (const file of mod.files) {
					const at = `${folder}/${file.path}`;
					yield* fs.makeDirectory(path.dirname(at), { recursive: true });
					yield* fs.writeFile(at, file.bytes);
				}
				return folder;
			}, Effect.orDie);
			const games = new Map<SessionId, HostGame>();
			const people = new Map<RunId, Person>();
			const host = yield* Effect.scope;
			/**
			 * The kept games' leases. A lease ends alone, after `leaseIdle` with no call on the clock the host was built
			 * with, or when `stop` takes it away; either way its game's scope closes.
			 */
			const kept = new Map<SessionId, Lease>();
			const leases = yield* RcMap.make({
				lookup: (session: SessionId) =>
					Effect.flatMap(
						Effect.sync(() => kept.get(session)),
						(lease) =>
							lease === undefined
								? Effect.die(new Error(`no kept game ${session} to lease`))
								: Effect.addFinalizer(() =>
										lease.game
											.end({ _tag: "stopped", reason: leaseEnded })
											.pipe(
												Effect.andThen(Scope.close(lease.scope, Exit.void)),
												Effect.ensuring(Effect.sync(() => kept.delete(session)))
											)
									)
					),
				idleTimeToLive: leaseIdle,
			});

			/** The item whose records hold `value`, for the word that takes it away. */
			const itemOf = (value: string) =>
				Arr.findFirst(data.items, (item) =>
					Arr.some(
						[
							`IsGet.${item.savedActor}`,
							`HavePlayedGetEvent.${item.savedActor}`,
							`PouchData.${item.savedActor}.StockNum`,
						],
						(name) => name === value
					)
				);

			/** The word of the state that would make `value` start at `wanted`, or at another value when that is none. */
			const remedy = (
				plan: Plan,
				value: GameValue,
				wanted: Option.Option<Value>
			) => {
				const written = Arr.findFirst(
					plan.edits,
					(edit) => edit._tag === "value" && edit.name === value.name
				);
				if (Option.isSome(written)) {
					return `The state's word ${written.value.from} writes it.`;
				}
				if (Option.isNone(wanted)) {
					return `Give it another value with set: { "${value.name}": ... }.`;
				}
				const item = itemOf(value.name);
				if (Option.isSome(item) && typeof wanted.value !== "string") {
					const word =
						wanted.value === false || wanted.value === 0
							? "withoutItems"
							: "withItems";
					return `${word}: ["${item.value.name}"] would make it ${asJson(wanted.value)}.`;
				}
				return `set: { "${value.name}": ${asJson(wanted.value)} } would make it so.`;
			};

			const hold = Effect.fn("Sessions.hold")(function* (start: Start) {
				const id = yield* random.uuidV4();
				const session = SessionId.make(id);
				const span = yield* Effect.option(Effect.currentSpan);
				const recorder = yield* runs.begin({
					id: start.run,
					session,
					machine,
					emulator: { program },
					game: data.key,
					state: start.state,
					...(start.script === undefined ? {} : { script: start.script }),
					...(start.attemptOf === undefined
						? {}
						: { attemptOf: start.attemptOf }),
					...(Option.isSome(span) ? { traceId: span.value.traceId } : {}),
				});
				const plan = yield* states.plan(start.state);
				const mod = yield* modOf(start.mod);
				const folder = Option.isSome(mod)
					? yield* Effect.asSome(staged(mod.value))
					: Option.none();
				if (Option.isSome(folder)) {
					yield* data.checkMod(folder.value);
				}
				const saves = yield* states.build(plan);
				const place = yield* places.take({
					session,
					run: start.run,
					owner: start.owner,
					waitForPlace: start.waitForPlace,
				});
				yield* recorder.taken({
					place: place.id,
					mod: Option.match(mod, {
						onNone: () => "none" as const,
						onSome: (taken) => ({ name: taken.name, digest: digestOf(taken) }),
					}),
				});
				const booted = yield* boot.start(place, {
					plan,
					saves,
					mod: Option.getOrUndefined(folder),
					textSpeed: start.textSpeed ?? "instant",
					wire: recorder.wire,
				});
				yield* recorder.started({
					build: booted.build,
					readBack: booted.readBack,
					textSpeed: booted.textSpeed,
				});
				const { emulator, values } = booted;
				const built = {
					progress: yield* Effect.orDie(decode("progress.sav", saves.progress)),
					caption: yield* Effect.orDie(decode("caption.sav", saves.caption)),
				};
				const steps: Step[] = [];
				const finished = yield* Deferred.make<void>();

				/** What the value held when Link could move: read back if the state wrote it, else as the built save holds it. */
				const startOf = (value: GameValue): Option.Option<Value> => {
					const readBack = booted.readBack.values[value.name];
					if (readBack !== undefined) {
						return Option.some(readBack);
					}
					if (value.file !== "progress" && value.file !== "caption") {
						return Option.none();
					}
					const held = Option.getOrElse(
						read(built[value.file], value.hash),
						() => value.default
					);
					if (Arr.isArray(held)) {
						return Option.none();
					}
					return value.type === "Enum" && typeof held === "number"
						? Arr.findFirst(
								value.choices,
								(choice) => Bun.hash.murmur32v3(choice) === held
							)
						: Option.some(held as Value);
				};

				/** The failure of a check whose start settles it already, before the game is asked. */
				const settledBy = (
					form: "check" | "still",
					value: GameValue,
					expected: Value,
					started: Option.Option<Value>
				): Option.Option<ProvesNothing> =>
					Option.match(started, {
						// With no start there is nothing to compare: a check counts as an effect, and still cannot be said.
						onNone: () =>
							form === "still"
								? Option.some(
										ProvesNothing.make({
											form,
											value: value.name,
											expected,
											remedy:
												"Use check, which counts a value with no known start as an effect.",
										})
									)
								: Option.none(),
						onSome: (held) => {
							const same = Equal.equals(held, expected);
							return (form === "check" ? same : !same)
								? Option.some(
										ProvesNothing.make({
											form,
											value: value.name,
											expected,
											start: held,
											remedy: remedy(
												plan,
												value,
												form === "check"
													? otherThan(value, expected)
													: Option.some(expected)
											),
										})
									)
								: Option.none();
						},
					});

				/** Runs one command and keeps it as a step: its input, its result as `shown` gives it, and how long it took. */
				const record = <A, E, R>(
					command: Step["command"],
					input: unknown,
					effect: Effect.Effect<A, E, R>,
					shown: (result: A) => Pick<Step, "check" | "pictures"> & {
						readonly value?: unknown;
					} = (result) => ({ value: result })
				) =>
					Effect.gen(function* () {
						const before = yield* Clock.currentTimeMillis;
						const exit = yield* Effect.exit(effect);
						const after = yield* Clock.currentTimeMillis;
						const { value, ...rest } =
							exit._tag === "Success" ? shown(exit.value) : {};
						const step: Step = {
							at: Temporal.Instant.fromEpochMilliseconds(before),
							command,
							input,
							milliseconds: after - before,
							result:
								exit._tag === "Success"
									? { _tag: "ok", ...(value === undefined ? {} : { value }) }
									: { _tag: "error", ...failureOf(exit.cause) },
							...rest,
						};
						steps.push(step);
						yield* recorder.step(step);
						return yield* exit;
					});

				const lookUp = Effect.fnUntraced(function* (name: string) {
					const value = yield* data.value(name);
					const entry = yield* entryOf(value);
					return { value, entry };
				});
				const readOne = Effect.fnUntraced(function* (name: string) {
					const { entry } = yield* lookUp(name);
					return yield* values.read(entry);
				});
				const send = (step: Parameters<typeof emulator.input>[0][0]) =>
					emulator.input([step]);

				const game: HostGame = {
					session,
					run: start.run,
					place: place.id,
					readBack: booted.readBack,
					press: (buttons, frames) =>
						record("press", { buttons, frames }, send({ buttons, frames })),
					stick: (position, frames) =>
						record(
							"stick",
							{ position, frames },
							send({
								frames,
								[position.stick === "Left" ? "leftStick" : "rightStick"]: [
									position.x,
									position.y,
								],
							})
						),
					idle: (frames) => record("idle", { frames }, send({ frames })),
					read: (names) =>
						record(
							"read",
							{ values: names },
							Effect.map(
								Effect.forEach(
									names,
									(name) =>
										Effect.map(readOne(name), (held) => [name, held] as const),
									{ concurrency: 1 }
								),
								Rec.fromEntries
							)
						),
					set: (written) =>
						record(
							"set",
							{ values: written },
							Effect.forEach(
								Rec.toEntries(written),
								Effect.fnUntraced(function* ([name, to]) {
									const { entry } = yield* lookUp(name);
									yield* values.write(entry, to);
								}),
								{ concurrency: 1, discard: true }
							)
						),
					wait: (name, expected, within) =>
						record(
							"wait",
							{ value: name, expected, within: Duration.format(within) },
							Effect.gen(function* () {
								const { entry } = yield* lookUp(name);
								yield* encodeValue(entry, expected);
								return yield* values.read(entry).pipe(
									Effect.repeat({
										schedule: Schedule.spaced(waitSpacing),
										until: (held) => Equal.equals(held, expected),
									}),
									Effect.timeoutOrElse({
										duration: within,
										orElse: () =>
											Effect.flatMap(values.read(entry), (last) =>
												Effect.fail(
													WaitTimedOut.make({
														value: name,
														expected,
														last,
														within,
													})
												)
											),
									})
								);
							})
						),
					check: (form, name, expected) =>
						record(
							"check",
							{ form, value: name, expected },
							Effect.gen(function* () {
								const { value, entry } = yield* lookUp(name);
								yield* encodeValue(entry, expected);
								const started = startOf(value);
								const settled = settledBy(form, value, expected, started);
								if (Option.isSome(settled)) {
									return yield* settled.value;
								}
								const actual = yield* values.read(entry);
								return {
									actual,
									held: Equal.equals(actual, expected),
									...(Option.isSome(started) ? { start: started.value } : {}),
								};
							}),
							(result) => ({
								value: result,
								check: { form, value: name, expected, ...result },
							})
						),
					screenshot: (label) =>
						record(
							"screenshot",
							label === undefined ? {} : { label },
							Effect.flatMap(
								emulator.screenshot({ height: screenshotHeight }),
								(png) => recorder.picture(png, label)
							),
							(picture) => ({
								value: { path: picture.path },
								pictures: [picture.path],
							})
						),
					handOver: (say, within) =>
						record(
							"hand-over",
							{ say },
							(people.get(start.run) ?? nobody)(say, within)
						),
					note: (text) => record("note", { text }, Effect.void),
					end: (outcome) =>
						Effect.andThen(
							recorder.end(outcome),
							Deferred.succeed(finished, undefined)
						),
					finished: Deferred.await(finished),
					finish: Effect.fnUntraced(function* ({ result, failure }) {
						const outcome = outcomeOf(steps, failure);
						yield* recorder.end(outcome, result);
						yield* Deferred.succeed(finished, undefined);
						return outcome;
					}),
				};
				yield* Effect.acquireRelease(
					Effect.sync(() => games.set(session, game)),
					() => Effect.sync(() => games.delete(session))
				);
				return game;
			});

			const get = Effect.fnUntraced(function* (session: SessionId) {
				const game = games.get(session);
				if (game === undefined) {
					return yield* SessionNotFound.make({ session });
				}
				if (kept.has(session)) {
					yield* RcMap.touch(leases, session);
				}
				return game;
			});

			const keep = Effect.fn("Sessions.keep")(function* (start: Start) {
				const scope = yield* Scope.fork(host);
				const game = yield* hold(start).pipe(
					Scope.provide(scope),
					Effect.onError((cause) => Scope.close(scope, Exit.failCause(cause)))
				);
				const lease: Lease = { game, scope };
				// From here the game's scope belongs to its lease.
				yield* Effect.uninterruptible(
					Effect.andThen(
						Effect.sync(() => kept.set(game.session, lease)),
						Effect.scoped(RcMap.get(leases, game.session))
					)
				);
				return game;
			});

			const stop = Effect.fn("Sessions.stop")(function* (
				session: SessionId,
				reason: string
			) {
				const lease = kept.get(session);
				if (lease === undefined) {
					return yield* SessionNotFound.make({ session });
				}
				yield* lease.game.end({ _tag: "stopped", reason });
				yield* RcMap.invalidate(leases, session);
				return lease.game;
			});

			const handOversTo = (run: RunId, person: Person) =>
				Effect.acquireRelease(
					Effect.sync(() => people.set(run, person)),
					() => Effect.sync(() => people.delete(run))
				);

			return Sessions.of({
				hold,
				keep,
				get,
				stop,
				handOversTo,
			});
		})
	);
}

/** A value of the value's type other than `expected`: the other Bool, or the list's default when that differs. */
const otherThan = (value: GameValue, expected: Value): Option.Option<Value> => {
	if (typeof expected === "boolean") {
		return Option.some(!expected);
	}
	const fallback = value.default;
	return Schema.is(Value)(fallback) && !Equal.equals(fallback, expected)
		? Option.some(fallback)
		: Option.none();
};
