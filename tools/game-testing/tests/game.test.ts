import { setDefaultTimeout } from "bun:test";
import { BunServices } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	Cause,
	Effect,
	Exit,
	Formatter,
	Option,
	Schedule,
	Schema,
	Stream,
} from "effect";
import { ChildProcess, ChildProcessSpawner } from "effect/process";
import type { Outcome } from "../src/contract/record.ts";
import { StartState } from "../src/contract/state.ts";
import type { Value } from "../src/contract/values.ts";
import { newRunId, PlaceId } from "../src/contract/values.ts";
import { LaunchFailed, StartNotTaken, UnknownName } from "../src/errors.ts";
import type { RunningGame } from "../src/game.ts";
import { Game } from "../src/game.ts";
import { Runs } from "../src/host/runs.ts";
import type { Scalar } from "../src/save/codec.ts";
import type { ScriptError } from "../src/script.ts";
import type { FakeLichii } from "./fake-lichii.ts";
import { fakeMachine } from "./fake-machine.ts";

type Words = Omit<ConstructorParameters<typeof StartState>[0], "base">;

// Every game boots through the title's three menus, one press a second, as the game's own do.
setDefaultTimeout(30_000);

/**
 * The fake game's values by their hashes, `IsTreasureBoxOpen.5005`, `IsTreasureBoxOpen.6006` and `Fake_Lamp_Lit`, so
 * the tests type-check whether or not a machine's `generated/names.d.ts` names the real game's values.
 */
const chest = "0x1d2a8fff";
const otherChest = "0x46dd3676";
const lamp = "0x33bf0164";
const openChest = (lichii: FakeLichii) => {
	Effect.runSync(lichii.put("IsTreasureBoxOpen.5005", true));
};

/**
 * Starts a game on a fake machine, runs `body` in it, and gives the run's outcome, why its start failed if it did, and
 * its record.
 */
const played = (
	body: (game: RunningGame) => Effect.Effect<unknown, ScriptError>,
	options?: {
		readonly base?: Readonly<Record<string, Scalar>>;
		readonly baseName?: string;
		readonly crashes?: boolean;
		readonly during?: (games: readonly FakeLichii[]) => Effect.Effect<void>;
		readonly finish?: boolean;
		readonly onA?: (lichii: FakeLichii) => void;
		readonly rewrites?: Readonly<Record<string, Value>>;
		readonly unsaved?: Readonly<Record<string, Value>>;
		readonly words?: Words;
	}
) =>
	Effect.gen(function* () {
		const machine = yield* fakeMachine(options);
		return yield* Effect.gen(function* () {
			const games = yield* Game;
			const runs = yield* Runs;
			const run = yield* newRunId;
			const ended = yield* Effect.exit(
				Effect.scoped(
					Effect.gen(function* () {
						const game = yield* games.start({
							run,
							owner: "game.test.ts",
							state: StartState.make({
								base: options?.baseName ?? "test-base",
								...options?.words,
							}),
						});
						yield* options?.during?.(yield* machine.games) ?? Effect.void;
						const exit = yield* Effect.exit(body(game));
						return options?.finish === false
							? undefined
							: yield* games.finish(game, exit);
					})
				)
			);
			// The host ends a game whose caller has gone when it sees the connection close.
			const freed = yield* machine.podman.containers.pipe(
				Effect.repeat({
					schedule: Schedule.spaced("20 millis"),
					until: (held) => held.size === 0,
				}),
				Effect.timeout("5 seconds"),
				Effect.option
			);
			const record = yield* runs.get(run).pipe(
				Effect.repeat({
					schedule: Schedule.spaced("20 millis"),
					until: (kept) => kept.outcome._tag !== "running",
				}),
				Effect.timeout("5 seconds")
			);
			return {
				outcome: Exit.isSuccess(ended) ? ended.value : undefined,
				startFailure: Exit.isFailure(ended)
					? Cause.squash(ended.cause)
					: undefined,
				record,
				freed: Option.isSome(freed),
			};
		}).pipe(Effect.provide(machine.layer));
	}).pipe(Effect.provide(BunServices.layer));

const brokenWith = (outcome: Outcome | undefined) =>
	outcome?._tag === "broken" ? outcome.error : "";

describe("a game on the host", () => {
	it.live(
		"a check holds when the value holds what it expects and did not at the start, and the record keeps each step",
		() =>
			Effect.gen(function* () {
				const { outcome, record } = yield* played(
					(game) =>
						Effect.gen(function* () {
							yield* game.press("A");
							return yield* game.check(chest, true);
						}),
					{
						onA: openChest,
						words: { set: { "PlayerStatus.CurrentRupee": 25 } },
					}
				);
				expect(outcome).toEqual({ _tag: "passed" });
				expect(record.outcome).toEqual({ _tag: "passed" });
				expect(record.result).toBe(true);
				expect(record.readBack).toEqual({
					values: {
						"CaptionData.IsAutoSave": false,
						"PlayerStatus.CurrentRupee": 25,
					},
					notRead: [],
				});
				expect(Arr.map(record.steps, (step) => step.command)).toEqual([
					"press",
					"check",
				]);
				expect(record.steps[1]?.check).toEqual({
					form: "check",
					value: chest,
					expected: true,
					start: false,
					actual: true,
					held: true,
				});
			})
	);

	it.live(
		"the boot presses nothing once the save's load has begun, so a chest A opens stands closed at control",
		() =>
			Effect.gen(function* () {
				const { outcome } = yield* played((game) => game.still(chest, false), {
					onA: openChest,
				});
				expect(outcome).toEqual({ _tag: "passed" });
			})
	);

	it.live(
		"a check whose start already held the answer breaks the run, naming the value and the word that takes it away",
		() =>
			Effect.gen(function* () {
				const { outcome } = yield* played((game) => game.check(chest, true), {
					base: { "IsTreasureBoxOpen.5005": true },
				});
				expect(outcome?._tag).toBe("broken");
				expect(brokenWith(outcome)).toBe(
					`${chest} already held true at the start, so the check proves nothing. set: { "${chest}": false } would make it so.`
				);
			})
	);

	it.live(
		"still holds when the value held what it expects at the start and holds it still",
		() =>
			Effect.gen(function* () {
				const { outcome, record } = yield* played(
					(game) =>
						Effect.gen(function* () {
							yield* game.press("A");
							return yield* game.still(otherChest, false);
						}),
					{ onA: openChest }
				);
				expect(outcome).toEqual({ _tag: "passed" });
				expect(record.steps[1]?.check).toEqual({
					form: "still",
					value: otherChest,
					expected: false,
					start: false,
					actual: false,
					held: true,
				});
			})
	);

	it.live(
		"a value no save holds has no known start, so its check counts as an effect",
		() =>
			Effect.gen(function* () {
				const { outcome, record } = yield* played(
					(game) => game.check(lamp, true),
					{ unsaved: { Fake_Lamp_Lit: true } }
				);
				expect(outcome).toEqual({ _tag: "passed" });
				expect(record.steps[0]?.check).toEqual({
					form: "check",
					value: lamp,
					expected: true,
					actual: true,
					held: true,
				});
			})
	);

	it.live("a check that does not hold fails the run", () =>
		Effect.gen(function* () {
			const { outcome } = yield* played((game) => game.check(chest, true));
			expect(outcome).toEqual({
				_tag: "failed",
				reason: `${chest} held false, not true`,
			});
		})
	);

	it.live(
		"a game whose Lichii dies breaks the run at its next command, and its place is given back",
		() =>
			Effect.gen(function* () {
				const { outcome, freed } = yield* played((game) => game.press("A"), {
					during: (games) =>
						Effect.forEach(games, (lichii) => lichii.exit, {
							discard: true,
							concurrency: 1,
						}),
				});
				expect(outcome?._tag).toBe("broken");
				expect(brokenWith(outcome)).toContain(
					"Lichii's debug server did not answer input_sequence"
				);
				expect(freed).toBe(true);
			})
	);

	it.live(
		"a caller that goes away without finishing breaks its run, and its place is given back",
		() =>
			Effect.gen(function* () {
				const { record, freed } = yield* played((game) => game.press("A"), {
					finish: false,
				});
				expect(record.outcome).toEqual({
					_tag: "broken",
					reason: "its caller went away before the script finished",
					error: "",
				});
				expect(freed).toBe(true);
			})
	);
	it.live("a run that ends with no check is unchecked", () =>
		Effect.gen(function* () {
			const { outcome, record } = yield* played((game) => game.press("A"));
			expect(outcome).toEqual({ _tag: "unchecked" });
			expect(record.outcome).toEqual({ _tag: "unchecked" });
		})
	);

	it.live(
		"still on a value no save holds is refused, since nothing shows its start",
		() =>
			Effect.gen(function* () {
				const { outcome } = yield* played((game) => game.still(lamp, false));
				expect(outcome?._tag).toBe("broken");
				expect(brokenWith(outcome)).toContain(
					`${lamp} has no known start, since no save holds it`
				);
			})
	);

	it.live("a still whose value differed at the start does not hold", () =>
		Effect.gen(function* () {
			const { outcome } = yield* played((game) => game.still(chest, false), {
				base: { "IsTreasureBoxOpen.5005": true },
			});
			expect(outcome?._tag).toBe("broken");
			expect(brokenWith(outcome)).toBe(
				`${chest} held true at the start, not false, so it cannot stay false. set: { "${chest}": false } would make it so.`
			);
		})
	);

	it.live("a wait that times out ends the run failed", () =>
		Effect.gen(function* () {
			const { outcome } = yield* played((game) =>
				game.wait(chest, true, { within: "1 second" })
			);
			expect(outcome).toEqual({
				_tag: "failed",
				reason: `${chest} still held false after 1s; the wait expected true.`,
			});
		})
	);

	it.live(
		"a start whose values the game does not hold as written ends broken, naming each one",
		() =>
			Effect.gen(function* () {
				const { startFailure, record } = yield* played(() => Effect.void, {
					words: { set: { "PlayerStatus.CurrentRupee": 25 } },
					rewrites: { "PlayerStatus.CurrentRupee": 0 },
				});
				expect(startFailure).toBeInstanceOf(StartNotTaken);
				expect(record.outcome).toEqual({
					_tag: "broken",
					reason: "the game did not start",
					error:
						"The game does not hold the state as it was written: PlayerStatus.CurrentRupee holds 0, not 25.",
				});
			})
	);

	it.live(
		"a start refused before any place is taken leaves a broken record",
		() =>
			Effect.gen(function* () {
				const { startFailure, record } = yield* played(() => Effect.void, {
					baseName: "no-such-base",
				});
				expect(startFailure).toBeInstanceOf(UnknownName);
				expect(record.outcome._tag).toBe("broken");
				expect(record.place).toBeUndefined();
			})
	);

	it.live(
		"a start that fails after its place is taken leaves a broken record that names the place",
		() =>
			Effect.gen(function* () {
				const { startFailure, record, freed } = yield* played(
					() => Effect.void,
					{ crashes: true }
				);
				expect(startFailure).toBeInstanceOf(LaunchFailed);
				expect(record.outcome).toEqual({
					_tag: "broken",
					reason: "the game did not start",
					error:
						"The game in place 1 stopped short of debug-server: the container exited with status 1",
				});
				expect(record.place).toBe(PlaceId.make(1));
				expect(freed).toBe(true);
			})
	);

	it.live(
		"a record read by another process while its steps are written is always whole",
		() =>
			Effect.gen(function* () {
				const machine = yield* fakeMachine();
				const seen = yield* Effect.gen(function* () {
					const games = yield* Game;
					const spawner = yield* ChildProcessSpawner.ChildProcessSpawner;
					const run = yield* newRunId;
					const record = `${machine.home}/state/game-testing/runs/${run}/record.json`;
					return yield* Effect.scoped(
						Effect.gen(function* () {
							const game = yield* games.start({
								run,
								owner: "game.test.ts",
								state: StartState.make({ base: "test-base" }),
							});
							// Reads and parses the record as fast as it can for 3 seconds, as game-testing runs would.
							const reader = yield* spawner.spawn(
								ChildProcess.make(process.execPath, [
									"-e",
									`let reads = 0, cut = 0; const until = Date.now() + 3000; while (Date.now() < until) { try { JSON.parse(await Bun.file(${Formatter.formatJson(record)}).text()); } catch { cut += 1; } reads += 1; } console.log(JSON.stringify({ reads, cut }));`,
								])
							);
							const printed = yield* Effect.forever(game.note("a step")).pipe(
								Effect.raceFirst(
									Stream.mkString(Stream.decodeText(reader.stdout))
								)
							);
							return yield* Schema.decodeEffect(
								Schema.fromJsonString(
									Schema.Struct({ reads: Schema.Finite, cut: Schema.Finite })
								)
							)(printed);
						})
					);
				}).pipe(Effect.provide(machine.layer));
				expect(seen.reads).toBeGreaterThan(1000);
				expect(seen.cut).toBe(0);
			}).pipe(Effect.provide(BunServices.layer)),
		{ timeout: 30_000 }
	);
});
