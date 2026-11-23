/**
 * The games a host runs, by session. A game lives in the scope of whoever holds it, an `Open` stream's handler, so it
 * stops and gives its place back however that scope ends. Every command a game takes is a step in its run's record.
 */
import { hostname } from "node:os";
import type { Scope } from "effect";
import {
	Array as Arr,
	Clock,
	Config,
	Context,
	Deferred,
	Duration,
	Effect,
	Equal,
	FileSystem,
	Formatter,
	Layer,
	Option,
	Path,
	Record as Rec,
	Schedule,
	Schema,
} from "effect";
import { SecureRandom } from "effect/crypto";
import { UserDirectories } from "effect/persistence";
import { originText } from "../contract/plan.ts";
import type { Outcome, ScriptSource, Step } from "../contract/record.ts";
import type {
	Mod,
	ReadBack,
	StartState,
	TextSpeed,
} from "../contract/state.ts";
import type {
	Button,
	Picture,
	PlaceId,
	RunId,
	StickPosition,
} from "../contract/values.ts";
import { Buttons, SessionId, Value } from "../contract/values.ts";
import type { GameValue } from "../data/game-data.ts";
import { GameData } from "../data/game-data.ts";
import type { EmulatorError } from "../emulator/emulator.ts";
import { encodeValue, entryOf } from "../emulator/values.ts";
import type {
	DumpFailed,
	EmulatorUnavailable,
	ImageBuildFailed,
	LaunchFailed,
	ModUnreadable,
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
	NobodyThere,
	ProvesNothing,
	SessionNotFound,
	WaitTimedOut,
} from "../errors.ts";
import { pressFrames } from "../game.ts";
import { digestOf } from "../mod.ts";
import { decode, read } from "../save/codec.ts";
import type { Plan } from "../save/state.ts";
import { StartStates } from "../save/state.ts";
import { Boot, builtSlot, isControl, savesOf } from "./boot.ts";
import { Places } from "./places.ts";
import { Runs } from "./runs.ts";
import { watchSaves } from "./saves.ts";

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

/** How often a wait reads its value. */
const waitSpacing = Duration.millis(100);
const screenshotHeight = 720;
/**
 * A dialog's pictures are smaller, as a talk takes dozens: about 0.56 MB each where a screenshot takes 2.4 MB (measured
 * over 47). Its text stays readable at this height (seen on a talk's pages).
 */
const dialogHeight = 360;
/**
 * How long the first press of `completeDialog` waits for a dialog to open. A talk, an item's text and a sign each opened
 * within 2 frames of the press (measured), so a dialog not open by then is not coming.
 */
const dialogOpening = Duration.seconds(2);

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
	/**
	 * A picture, a press and `every` frames, until a dialog it saw open is over and Link can move; each press a step with
	 * its picture. It answers the pictures by name.
	 */
	readonly completeDialog: (
		button: Button,
		times: number,
		every: number
	) => Effect.Effect<
		{ readonly presses: number; readonly pictures: readonly string[] },
		EmulatorError | WaitTimedOut
	>;
	/**
	 * Ends the run with `outcome`, unless an end came first, and its `Open` stream with it: the game stops, its last saves
	 * are kept, its place is given back, and the record takes the outcome last.
	 */
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
	/**
	 * Reads the value, then presses until it holds, `times` presses at most, `every` frames apart: one step that lists
	 * what each press read, answering the presses.
	 */
	readonly pressUntil: (
		buttons: Buttons,
		value: string,
		expected: Value,
		times: number,
		every: number
	) => Effect.Effect<number, ReadError | ValueRefused | WaitTimedOut>;
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
		/** A session's game. */
		readonly get: (
			session: SessionId
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
			const staging = `${yield* Effect.orDie(directories.cache("game-testing"))}/mods`;

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
					return `${originText(written.value.from)} writes it.`;
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
				const plan = yield* states.plan(start.state, start.mod);
				yield* recorder.planned({
					...(plan.location === undefined ? {} : { location: plan.location }),
					edits: plan.edits,
					leftAlone: plan.leftAlone,
				});
				const { mod } = plan;
				const folder = Option.isSome(mod)
					? yield* Effect.asSome(staged(mod.value))
					: Option.none();
				const unmeasured = Option.isSome(folder)
					? yield* data.checkMod(folder.value)
					: [];
				const saves = yield* states.build(plan);
				yield* recorder.save({
					kind: "built",
					slot: builtSlot,
					progress: saves.progress,
					caption: saves.caption,
				});
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
					unmeasured,
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
				const keepSaves = watchSaves(
					fs,
					savesOf(place.folder),
					saves,
					recorder.save
				);
				// What the game wrote in the last step, or as the run went wrong, is kept before the game stops.
				yield* Effect.addFinalizer(() => keepSaves());
				yield* keepSaves();
				const { emulator, values } = booted;
				const built = {
					progress: yield* Effect.orDie(decode("progress.sav", saves.progress)),
					caption: yield* Effect.orDie(decode("caption.sav", saves.caption)),
				};
				const steps: Step[] = [];
				const finished = yield* Deferred.make<void>();

				/** What the value held when Link could move: read back if the state wrote it, else as the built save holds it. */
				const startOf = (value: GameValue): Option.Option<Value> => {
					// A place read back is no Value a check compares; the built save holds it as written.
					const readBack = booted.readBack.values[value.name];
					if (readBack !== undefined && typeof readBack !== "object") {
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
						yield* keepSaves();
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
					pressUntil: (buttons, name, expected, times, every) =>
						record(
							"press-until",
							{ buttons, value: name, expected, times, every },
							Effect.gen(function* () {
								const { entry } = yield* lookUp(name);
								yield* encodeValue(entry, expected);
								const began = yield* Clock.currentTimeMillis;
								const held: Value[] = [];
								let last = yield* values.read(entry);
								while (!Equal.equals(last, expected)) {
									if (held.length === times) {
										const ended = yield* Clock.currentTimeMillis;
										return yield* WaitTimedOut.make({
											value: name,
											expected,
											last,
											within: Duration.millis(ended - began),
											presses: times,
										});
									}
									yield* send({ buttons, frames: pressFrames });
									yield* send({ frames: every });
									last = yield* values.read(entry);
									held.push(last);
									yield* keepSaves(held.length);
								}
								return { presses: held.length, held };
							}),
							(pressed) => ({ value: pressed })
						).pipe(Effect.map((pressed) => pressed.presses)),
					completeDialog: Effect.fnUntraced(function* (button, times, every) {
						const began = yield* Clock.currentTimeMillis;
						const pictures: string[] = [];
						const inControl = Effect.map(
							booted.screen,
							Option.exists(isControl)
						);
						/** Whether a dialog opens within `dialogOpening`: Link out of control. */
						const opens = inControl.pipe(
							Effect.repeat({
								schedule: Schedule.spaced(waitSpacing),
								while: (control) => control,
							}),
							Effect.as(true),
							Effect.timeoutOrElse({
								duration: dialogOpening,
								orElse: () => Effect.succeed(false),
							})
						);
						const ranOut = (dialog: "none-opened" | "still-open") =>
							Effect.flatMap(Clock.currentTimeMillis, (ended) =>
								WaitTimedOut.make({
									value: "Link in control",
									expected: true,
									last: dialog === "none-opened",
									within: Duration.millis(ended - began),
									presses: pictures.length,
									dialog,
								})
							);
						/** A picture, a press and `every` frames: whether a dialog is open after it. */
						const pressOnce = (press: number, opened: boolean) =>
							record(
								"dialog",
								{ button, press, every },
								Effect.gen(function* () {
									const png = yield* emulator.screenshot({
										height: dialogHeight,
									});
									const { path: taken } = yield* recorder.picture(
										png,
										undefined
									);
									pictures.push(taken);
									yield* send({
										buttons: Buttons.make([button]),
										frames: pressFrames,
									});
									yield* send({ frames: every });
									const control = yield* inControl;
									// One the first press opens a moment late is still seen.
									const late = control && !opened && (yield* opens);
									return { path: taken, open: !control || late };
								}),
								(pressed) => ({ value: pressed, pictures: [pressed.path] })
							);
						// A dialog an earlier press opened counts as seen.
						let seen = !(yield* inControl);
						for (let press = 1; press <= times; press += 1) {
							const { open } = yield* pressOnce(press, seen);
							if (!(open || seen)) {
								return yield* ranOut("none-opened");
							}
							if (!open) {
								return { presses: press, pictures };
							}
							seen = true;
						}
						return yield* ranOut("still-open");
					}),
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
				return game;
			});

			const handOversTo = (run: RunId, person: Person) =>
				Effect.acquireRelease(
					Effect.sync(() => people.set(run, person)),
					() => Effect.sync(() => people.delete(run))
				);

			return Sessions.of({ hold, get, handOversTo });
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
