/**
 * One game, from a built save to Link in control. The place holds one save, the built one, so A alone gets through
 * the title's menus: Continue is the first choice, the save the only one, and Yes the first answer to the question
 * that loads it. Every screen is told by values read from the game, never by a picture.
 */
import type { Scope } from "effect";
import {
	Array as Arr,
	Context,
	Duration,
	Effect,
	Equal,
	FileSystem,
	Layer,
	Option,
	Path,
	Record as Rec,
	Schema,
	String as Str,
} from "effect";
import type { ReadBack, TextSpeed } from "../contract/state.ts";
import type { Saved } from "../contract/values.ts";
import { Buttons, Value } from "../contract/values.ts";
import { GameData } from "../data/game-data.ts";
import type { Emulator, EmulatorError, WireLog } from "../emulator/emulator.ts";
import { Launcher, programId, textSpeedOf } from "../emulator/launcher.ts";
import type { Values } from "../emulator/values.ts";
import { attach, entryOf } from "../emulator/values.ts";
import type { EmulatorUnavailable } from "../errors.ts";
import { LaunchFailed, StartNotTaken } from "../errors.ts";
import type { Plan, Saves } from "../save/state.ts";
import type { Place } from "./places.ts";

/**
 * What tells the screens. `menu` is an unnamed, unsaved Enum: `Title` on the title's menu, `Load` on its list of saves
 * and the question that loads one, `Loading` from then until Link can move, and `None` in play. `loading` is set while
 * a scene loads; `scene` names the scene Link is in. The event manager is `[[program + eventManager]]`, and the event
 * it plays is the word at `+ playing`, 0 when none.
 */
export const screens = {
	menu: "0x578d41e1",
	loading: "Sequence_IsLoadingBanc",
	scene: "Sequence_CurrentBanc",
	eventManager: 0x4c_7e_09_0n,
	playing: 0x68n,
} as const;

/** What the screens' values held at one look. */
export interface Screen {
	/** An event manager of 0 is not up yet, which reads as an event. */
	readonly event: boolean;
	readonly loading: boolean;
	readonly menu: Value;
	readonly scene: Value;
}

/** The title's menus, where A takes the first choice. */
export const isTitle = (screen: Screen) =>
	screen.menu === "Title" || screen.menu === "Load";
/** Link can move: no menu, no load, no event, and a scene up. */
export const isControl = (screen: Screen) =>
	screen.menu === "None" &&
	!screen.loading &&
	!screen.event &&
	screen.scene !== "";

/** The opening film runs before the title's menu opens; it ended within 75 s on the main machine. */
const titleLimit = Duration.minutes(3);
/** From the title's menu until A has closed it. */
const menuLimit = Duration.minutes(1);
/** A load from the title took at most 32 s on the spare machine. */
const loadLimit = Duration.minutes(2);
const lookAgain = Duration.millis(250);
/** Long enough for a menu to open between presses. */
const pressSpacing = Duration.seconds(1);
const pressFrames = 4;

/**
 * The one user every place's Lichii has. Lichii writes a user with a random id when it finds none, and a save belongs
 * to its user's folder, so the place writes its own before each start, as it writes its settings.
 */
const user = new TextEncoder().encode("game-testing-uid");

/** Lichii's `profiles.dat`: 16 bytes, then eight users of 0xC8 bytes, the first two fields of each its id. */
const profiles = () => {
	const bytes = new Uint8Array(0x6_50);
	bytes.set(user, 0x10);
	bytes.set(user, 0x20);
	bytes.set(new TextEncoder().encode("game-testing"), 0x38);
	return bytes;
};

/** The user's save folder: its id read as two little-endian words, the second first. */
const userFolder = () => {
	const view = new DataView(user.buffer);
	const word = (at: number) =>
		Str.padStart(
			16,
			"0"
		)(Str.toUpperCase(view.getBigUint64(at, true).toString(16)));
	return `${word(8)}${word(0)}`;
};

/** A booted game, with Link in control. */
export interface Booted {
	readonly build: string;
	readonly emulator: Emulator;
	readonly readBack: typeof ReadBack.Type;
	/** How the game writes its text, as its program holds it. */
	readonly textSpeed: TextSpeed;
	readonly values: Values;
}

export class Boot extends Context.Service<
	Boot,
	{
		/**
		 * Puts the save and the mod folder into the place, starts its game and brings Link into control, then reads back
		 * every value the plan wrote. The game stops when the scope closes.
		 */
		readonly start: (
			place: Place,
			options: {
				readonly plan: Plan;
				readonly saves: Saves;
				readonly mod: string | undefined;
				readonly textSpeed: TextSpeed;
				readonly wire: WireLog;
			}
		) => Effect.Effect<
			Booted,
			LaunchFailed | EmulatorUnavailable | StartNotTaken,
			Scope.Scope
		>;
	}
>()("game-testing/host/boot") {
	static readonly layer = Layer.effect(
		Boot,
		Effect.gen(function* () {
			const fs = yield* FileSystem.FileSystem;
			const path = yield* Path.Path;
			const data = yield* GameData;
			const launcher = yield* Launcher;
			const entry = (name: string) =>
				Effect.flatMap(data.value(name), entryOf).pipe(Effect.orDie);
			const menu = yield* entry(screens.menu);
			const loading = yield* entry(screens.loading);
			const scene = yield* entry(screens.scene);

			const furnish = Effect.fnUntraced(function* (
				folder: string,
				saves: Saves,
				mod: string | undefined
			) {
				const nand = `${folder}/data/lichii/nand`;
				const profile = `${nand}/system/save/8000000000000010/su/avators`;
				const game = `${nand}/user/save/0000000000000000/${userFolder()}/${programId}`;
				const mods = `${folder}/data/lichii/load/${programId}`;
				yield* fs.makeDirectory(profile, { recursive: true });
				yield* fs.writeFile(`${profile}/profiles.dat`, profiles());
				// The game writes autosaves into other slots; the place keeps only the one built for this run.
				yield* fs.remove(game, { recursive: true, force: true });
				yield* fs.makeDirectory(`${game}/slot_00`, { recursive: true });
				yield* fs.writeFile(`${game}/slot_00/progress.sav`, saves.progress);
				yield* fs.writeFile(`${game}/slot_00/caption.sav`, saves.caption);
				yield* fs.writeFile(`${game}/option.sav`, saves.option);
				yield* fs.remove(mods, { recursive: true, force: true });
				yield* fs.makeDirectory(mods, { recursive: true });
				if (mod !== undefined) {
					yield* fs.copy(mod, `${mods}/${path.basename(mod)}`);
				}
			}, Effect.orDie);

			const start = Effect.fn("Boot.start")(function* (
				place: Place,
				options: {
					readonly plan: Plan;
					readonly saves: Saves;
					readonly mod: string | undefined;
					readonly textSpeed: TextSpeed;
					readonly wire: WireLog;
				}
			) {
				yield* furnish(place.folder, options.saves, options.mod);
				const emulator = yield* launcher.launch(place, {
					wire: options.wire,
					textSpeed: options.textSpeed,
				});
				const failed = (stage: LaunchFailed["stage"], detail: string) =>
					Effect.flatMap(
						emulator
							.logTail({ limit: 20 })
							.pipe(Effect.orElseSucceed(() => [])),
						(logTail) =>
							Effect.fail(
								LaunchFailed.make({ place: place.id, stage, detail, logTail })
							)
					);
				const { status, values } = yield* Effect.all(
					{ status: emulator.status, values: attach(emulator) },
					{ concurrency: 1 }
				).pipe(
					Effect.catchTag("EmulatorRefused", (error) =>
						failed("first-frame", error.message)
					)
				);
				const u64 = (address: bigint) =>
					Effect.map(emulator.readMemory(address, 8), (bytes) =>
						new DataView(bytes.buffer, bytes.byteOffset).getBigUint64(0, true)
					);
				const event = Effect.gen(function* () {
					const slot = yield* u64(values.program + screens.eventManager);
					const manager = yield* u64(slot);
					return (
						manager === 0n || (yield* u64(manager + screens.playing)) !== 0n
					);
				});
				/** One look at the screen; a value the game has not set up yet is no screen. */
				const look = Effect.all(
					{
						menu: values.read(menu),
						loading: values.read(loading),
						scene: values.read(scene),
						event,
					},
					{ concurrency: 1 }
				).pipe(
					Effect.map(
						(read): Screen => ({
							...read,
							loading: read.loading === true,
						})
					),
					Effect.asSome,
					Effect.catchTags({
						EmulatorRefused: () => Effect.succeedNone,
						ValueNotFound: () => Effect.succeedNone,
					})
				);
				/** Looks until the screen is `ready`, doing `between` after each look that is not. */
				const until = (
					stage: LaunchFailed["stage"],
					limit: Duration.Duration,
					ready: (screen: Option.Option<Screen>) => boolean,
					between: Effect.Effect<void, EmulatorError>
				) =>
					look.pipe(
						Effect.flatMap((screen) =>
							ready(screen) ? Effect.succeed(true) : Effect.as(between, false)
						),
						Effect.repeat({ until: (done) => done }),
						Effect.timeoutOrElse({
							duration: limit,
							orElse: () =>
								failed(
									stage,
									`the game did not show it within ${Duration.format(limit)}`
								),
						}),
						Effect.catchTag("EmulatorRefused", (error) =>
							failed(stage, error.message)
						)
					);
				const wait = Effect.sleep(lookAgain);
				const pressA = emulator
					.input([{ buttons: Buttons.make(["A"]), frames: pressFrames }])
					.pipe(Effect.andThen(Effect.sleep(pressSpacing)));
				yield* until("title", titleLimit, Option.exists(isTitle), wait);
				// A goes in only while a look shows the title's menus. Any other look means the save's load has begun.
				yield* until(
					"loaded",
					menuLimit,
					(screen) => !Option.exists(screen, isTitle),
					pressA
				);
				yield* until("control", loadLimit, Option.exists(isControl), wait);
				const { readBack, textSpeed } = yield* Effect.all(
					{
						readBack: readBackOf(data, values, options.plan),
						textSpeed: textSpeedOf(emulator, values.program),
					},
					{ concurrency: 1 }
				).pipe(
					Effect.catchTag("EmulatorRefused", (error) =>
						failed("control", error.message)
					)
				);
				const booted: Booted = {
					textSpeed,
					emulator,
					values,
					build: status.build,
					readBack,
				};
				return booted;
			});

			return Boot.of({ start });
		})
	);
}

/** What the game holds of one value the plan wrote, or why it could not be read. */
type Reading =
	| {
			readonly _tag: "read";
			readonly value: string;
			readonly written: Saved;
			readonly held: Value;
			readonly type: string;
	  }
	| { readonly _tag: "notRead"; readonly value: string };

/**
 * Link's facing, which the game recomputes as it turns him on load: written as -0.22997942566871668 radians, it read back
 * as -0.22997945547103882 on the game. The map's copy of it, in degrees, the game recomputes from it once a mod's start
 * values are in the save: written as 24.91800541306725, it read back as 24.917978286743164. Each holds within a
 * millionth of a radian; every other value holds exactly.
 */
const facings: Readonly<Record<string, number>> = {
	"PlayerStatus.SavePosRadY": 1e-6,
	Player_Rot_For_Map: (1e-6 * 180) / Math.PI,
};

/** The game holds a Float in 32 bits, so a written Float is compared as the save stored it. */
const sameAs = (
	{ value, type }: { readonly value: string; readonly type: string },
	written: Saved,
	held: Value
) => {
	if (typeof written !== "number" || typeof held !== "number") {
		return Equal.equals(written, held);
	}
	const within = facings[value];
	if (within !== undefined) {
		return Math.abs(written - held) <= within;
	}
	return type === "Float" ? Math.fround(written) === held : written === held;
};

/**
 * Every value the plan wrote, as the game holds it once Link can move. A value of a type the memory is not read for,
 * one the game does not hold in memory, and a per-actor key are listed as not read. A value the game holds otherwise
 * than written fails the start: the save did not load as built.
 */
const readBackOf = Effect.fnUntraced(function* (
	data: GameData["Service"],
	values: Values,
	plan: Plan
) {
	const readings = yield* Effect.forEach(
		plan.edits,
		Effect.fnUntraced(function* (edit): Effect.fn.Return<
			Reading,
			EmulatorError
		> {
			if (edit._tag === "key") {
				return { _tag: "notRead", value: `key ${edit.key}` };
			}
			const value = yield* Effect.orDie(data.value(edit.name));
			const entry = yield* Effect.option(entryOf(value));
			if (Option.isNone(entry)) {
				return { _tag: "notRead", value: edit.name };
			}
			return yield* values.read(entry.value).pipe(
				Effect.map(
					(held): Reading => ({
						_tag: "read",
						value: edit.name,
						written: edit.to,
						held,
						type: value.type,
					})
				),
				Effect.catchTag("ValueNotFound", () =>
					Effect.succeed<Reading>({ _tag: "notRead", value: edit.name })
				)
			);
		}),
		{ concurrency: 1 }
	);
	const read = Arr.flatMap(readings, (reading) =>
		reading._tag === "read" ? [reading] : []
	);
	const differences = Arr.flatMap(read, ({ value, written, held, type }) =>
		Schema.is(Value)(written) && !sameAs({ value, type }, written, held)
			? [{ value, written, held }]
			: []
	);
	if (Arr.isReadonlyArrayNonEmpty(differences)) {
		return yield* StartNotTaken.make({ differences });
	}
	const readBack: typeof ReadBack.Type = {
		values: Rec.fromEntries(
			Arr.map(read, ({ value, held }) => [value, held] as const)
		),
		notRead: Arr.flatMap(readings, (reading) =>
			reading._tag === "notRead" ? [reading.value] : []
		),
	};
	return readBack;
});
