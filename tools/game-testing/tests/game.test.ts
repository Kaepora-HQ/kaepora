import { setDefaultTimeout } from "bun:test";
import { BunServices } from "@effect/platform-bun";
import { describe, expect, it } from "@voila.dev/effect-bun-test";
import {
	Array as Arr,
	Cause,
	Effect,
	Exit,
	FileSystem,
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
import { decode, read } from "../src/save/codec.ts";
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
const endTalk = (lichii: FakeLichii) => {
	Effect.runSync(lichii.playing(false));
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
		readonly autosaveOnA?: Readonly<Record<string, Scalar>>;
		readonly autosaveLate?: Readonly<Record<string, Scalar>>;
		readonly rewrites?: Readonly<
			Record<string, Value | readonly [number, number, number]>
		>;
		readonly unsaved?: Readonly<Record<string, Value>>;
		readonly words?: Words;
		readonly textBytesTaken?: boolean;
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
				folder: runs.folder(run),
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
				const { record, freed } = yield* played(
					(game) => Effect.andThen(game.press("A"), Effect.sleep("300 millis")),
					{
						finish: false,
						// The game autosaves after the press is answered, so only the look as the run ends can keep it.
						autosaveLate: { "IsTreasureBoxOpen.5005": true },
					}
				);
				expect(record.outcome).toEqual({
					_tag: "broken",
					reason: "its caller went away before the script finished",
					error: "",
				});
				expect(freed).toBe(true);
				expect(Arr.map(record.saves ?? [], (save) => save.kind)).toEqual([
					"built",
					"autosave",
				]);
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
		"pressUntil presses until the value holds, answers how many presses it took, and its step lists what each press read",
		() =>
			Effect.gen(function* () {
				let presses = 0;
				const { record } = yield* played(
					(game) =>
						Effect.gen(function* () {
							const took = yield* game.pressUntil("A", chest, true, {
								every: 2,
							});
							yield* game.check(chest, true);
							return { took };
						}),
					{
						onA: (lichii) => {
							presses += 1;
							if (presses === 3) {
								openChest(lichii);
							}
						},
					}
				);
				expect(record.outcome).toEqual({ _tag: "passed" });
				expect(record.result).toEqual({ took: 3 });
				expect(Arr.map(record.steps, (step) => step.command)).toEqual([
					"press-until",
					"check",
				]);
				expect(record.steps[0]?.result).toEqual({
					_tag: "ok",
					value: { presses: 3, held: [false, false, true] },
				});
			})
	);

	it.live("pressUntil on a value that already holds presses nothing", () =>
		Effect.gen(function* () {
			let presses = 0;
			const { record } = yield* played(
				(game) =>
					Effect.map(
						game.pressUntil("A", chest, true, { every: 2 }),
						(took) => ({ took })
					),
				{
					base: { "IsTreasureBoxOpen.5005": true },
					onA: () => {
						presses += 1;
					},
				}
			);
			expect(record.result).toEqual({ took: 0 });
			expect(presses).toBe(0);
		})
	);

	it.live(
		"pressUntil whose presses run out ends the run failed, as a wait that ran out",
		() =>
			Effect.gen(function* () {
				const { outcome } = yield* played((game) =>
					game.pressUntil("A", chest, true, { times: 4, every: 2 })
				);
				expect(outcome).toMatchObject({
					_tag: "failed",
					reason: expect.stringContaining(
						`${chest} still held false after 4 presses`
					),
				});
			})
	);

	it.live(
		"completeDialog takes a picture before each press until the dialog is over, each press a step with its picture",
		() =>
			Effect.gen(function* () {
				let presses = 0;
				const { record } = yield* played(
					(game) =>
						Effect.gen(function* () {
							const talk = yield* game.completeDialog({ every: 2 });
							return { presses: talk.presses, pictures: talk.pictures };
						}),
					{
						during: (games) =>
							Effect.forEach(games, (lichii) => lichii.playing(true), {
								concurrency: 1,
								discard: true,
							}),
						onA: (lichii) => {
							presses += 1;
							if (presses === 3) {
								endTalk(lichii);
							}
						},
					}
				);
				expect(record.result).toEqual({
					presses: 3,
					pictures: ["picture-1.png", "picture-2.png", "picture-3.png"],
				});
				expect(
					Arr.map(record.steps, (step) => [step.command, step.pictures])
				).toEqual([
					["dialog", ["picture-1.png"]],
					["dialog", ["picture-2.png"]],
					["dialog", ["picture-3.png"]],
				]);
			})
	);

	it.live(
		"completeDialog with no dialog to open fails as a wait that ran out, and says no dialog opened",
		() =>
			Effect.gen(function* () {
				let presses = 0;
				const { outcome } = yield* played(
					(game) => game.completeDialog({ every: 2 }),
					{
						onA: () => {
							presses += 1;
						},
					}
				);
				expect(outcome).toMatchObject({
					_tag: "failed",
					reason: expect.stringContaining("No dialog opened"),
				});
				expect(presses).toBe(1);
			})
	);

	it.live(
		"completeDialog whose presses run out before the dialog ends ends the run failed",
		() =>
			Effect.gen(function* () {
				const { outcome } = yield* played(
					(game) => game.completeDialog({ times: 2, every: 2 }),
					{
						during: (games) =>
							Effect.forEach(games, (lichii) => lichii.playing(true), {
								concurrency: 1,
								discard: true,
							}),
					}
				);
				expect(outcome).toMatchObject({
					_tag: "failed",
					reason: expect.stringContaining("after 2 presses"),
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
		"a start reads Link's saved place back: one whose height the game set 0.09 lower is taken, and one a floor lower is not",
		() =>
			Effect.gen(function* () {
				const location = {
					scene: "KokiriForest",
					position: [1.5, 2.25, 3.125] as const,
					facing: 0,
				};
				// Seen on the game: a worked-out spot written at y 115.818 was held at 115.72996, set on the floor.
				const settled = yield* played(() => Effect.void, {
					words: { location },
					rewrites: { "PlayerStatus.SavePos": [1.5, 2.16, 3.125] },
				});
				expect(settled.startFailure).toBeUndefined();
				expect(settled.record.readBack?.values["PlayerStatus.SavePos"]).toEqual(
					[1.5, Math.fround(2.16), 3.125]
				);
				const fell = yield* played(() => Effect.void, {
					words: { location },
					rewrites: { "PlayerStatus.SavePos": [1.5, -7.75, 3.125] },
				});
				expect(fell.startFailure).toBeInstanceOf(StartNotTaken);
			})
	);

	it.live(
		"a start whose facing the game recomputed by a few float steps, or across half a turn, counts as taken, and any other Float holds exactly",
		() =>
			Effect.gen(function* () {
				// Seen on the game: a facing written as -0.22997942566871668 read back as -0.22997945547103882.
				const turned = yield* played(() => Effect.void, {
					words: {
						set: { "PlayerStatus.SavePosRadY": -0.229_979_425_668_716_68 },
					},
					rewrites: { "PlayerStatus.SavePosRadY": -0.229_979_455_471_038_82 },
				});
				expect(turned.startFailure).toBeUndefined();
				// Seen on the game: half a turn written either way read back as the other, the same facing.
				const half = yield* played(() => Effect.void, {
					words: { set: { "PlayerStatus.SavePosRadY": Math.PI } },
					rewrites: { "PlayerStatus.SavePosRadY": -3.141_592_502_593_994 },
				});
				expect(half.startFailure).toBeUndefined();
				const other = yield* played(() => Effect.void, {
					words: { set: { Fake_Float: -0.229_979_425_668_716_68 } },
					rewrites: { Fake_Float: -0.229_979_455_471_038_82 },
				});
				expect(other.startFailure).toBeInstanceOf(StartNotTaken);
			})
	);

	it.live(
		"a start whose map facing the game recomputed from Link's, or held across half a turn, counts as taken",
		() =>
			Effect.gen(function* () {
				// Seen on the game, Mido's chest with the default mod: written 24.91800541306725, held 24.917978286743164.
				const turned = yield* played(() => Effect.void, {
					words: { set: { Player_Rot_For_Map: 24.918_005_413_067_25 } },
					rewrites: { Player_Rot_For_Map: 24.917_978_286_743_164 },
				});
				expect(turned.startFailure).toBeUndefined();
				// Half a turn either way is one facing, as Link's saved facing showed on the game.
				const half = yield* played(() => Effect.void, {
					words: { set: { Player_Rot_For_Map: -180 } },
					rewrites: { Player_Rot_For_Map: 180 },
				});
				expect(half.startFailure).toBeUndefined();
			})
	);

	it.live(
		"every save of a run is kept with it in order: the one built for the start, then each the game wrote, after its step",
		() =>
			Effect.gen(function* () {
				const fs = yield* FileSystem.FileSystem;
				const { record, folder } = yield* played(
					(game) =>
						Effect.gen(function* () {
							yield* game.press("A");
							return yield* game.check(chest, true);
						}),
					{ onA: openChest, autosaveOnA: { "IsTreasureBoxOpen.5005": true } }
				);
				const kept = record.saves ?? [];
				expect(
					Arr.map(kept, ({ kind, slot, afterSteps }) => [
						kind,
						slot,
						afterSteps,
					])
				).toEqual([
					["built", "slot_00", 0],
					["autosave", "slot_01", 1],
				]);
				for (const save of kept) {
					for (const file of [save.progress, save.caption]) {
						const bytes = yield* fs.readFile(`${folder}/${file.path}`);
						expect(
							new Bun.CryptoHasher("sha256").update(bytes).digest("hex")
						).toBe(file.sha256);
					}
				}
				const autosaved = yield* decode(
					"progress.sav",
					yield* fs.readFile(`${folder}/${kept[1]?.progress.path}`)
				);
				expect(
					read(autosaved, Bun.hash.murmur32v3("IsTreasureBoxOpen.5005"))
				).toEqual(Option.some(true));
			}).pipe(Effect.provide(BunServices.layer))
	);

	it.live(
		"a save the game writes during pressUntil names the press it followed",
		() =>
			Effect.gen(function* () {
				let presses = 0;
				const { record } = yield* played(
					(game) => game.pressUntil("A", chest, true, { every: 2 }),
					{
						autosaveOnA: { "IsTreasureBoxOpen.5005": true },
						onA: (lichii) => {
							presses += 1;
							if (presses === 2) {
								openChest(lichii);
							}
						},
					}
				);
				expect(
					Arr.map(record.saves ?? [], ({ kind, afterSteps, afterPress }) => ({
						kind,
						afterSteps,
						afterPress,
					}))
				).toEqual([
					{ kind: "built", afterSteps: 0, afterPress: undefined },
					{ kind: "autosave", afterSteps: 0, afterPress: 1 },
				]);
			})
	);

	it.live(
		"a run that ends broken keeps the save the game wrote in its last step",
		() =>
			Effect.gen(function* () {
				const { record } = yield* played(
					(game) =>
						Effect.andThen(game.press("A"), Effect.die("the body gave up")),
					{ autosaveOnA: { "IsTreasureBoxOpen.5005": true } }
				);
				expect(record.outcome._tag).toBe("broken");
				expect(Arr.map(record.saves ?? [], (save) => save.kind)).toEqual([
					"built",
					"autosave",
				]);
			})
	);

	it.live(
		"a record states the mod and the text speed the game had, which is the game's own when a mod took the add-on's bytes",
		() =>
			Effect.gen(function* () {
				const plain = yield* played(() => Effect.void);
				expect(plain.record.mod).toMatchObject({ name: "skips" });
				expect(plain.record.textSpeed).toBe("instant");
				const taken = yield* played(() => Effect.void, {
					textBytesTaken: true,
				});
				expect(taken.record.textSpeed).toBe("game");
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
