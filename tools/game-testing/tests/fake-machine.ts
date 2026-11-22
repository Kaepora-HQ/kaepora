/**
 * A machine with the game, made of fakes: the fake `kaepora-dump` reads a fake game's tables, the fake podman makes
 * places, and each place's container runs a fake Lichii that loads the save the place holds, as Lichii does: from the
 * folder of the user `profiles.dat` names. Its title, its list of saves and the question that loads one each take A,
 * as the game's do, and then it loads and gives Link control. Everything else a process asks for, a script's child or
 * the type check, runs for real.
 */
import { BunServices } from "@effect/platform-bun";
import {
	Array as Arr,
	ConfigProvider,
	Deferred,
	Effect,
	FileSystem,
	Layer,
	Logger,
	Option,
	Record as Rec,
	Ref,
	String as Str,
} from "effect";
import { UserDirectories } from "effect/persistence";
import { ChildProcessSpawner } from "effect/process";
import type { Value } from "../src/contract/values.ts";
import { Game } from "../src/game.ts";
import { HostSocket, host } from "../src/host/host.ts";
import type { Scalar } from "../src/save/codec.ts";
import { decode, encode, read, write } from "../src/save/codec.ts";
import type { Check } from "../src/save/tables.ts";
import { OwnTables } from "../src/save/tables.ts";
import type { FakeGame } from "./fake-game.ts";
import { fakeDump, fakeSave } from "./fake-game.ts";
import type { FakeLichii, FakeValue } from "./fake-lichii.ts";
import { fakeLichii } from "./fake-lichii.ts";
import type { FakeContainer } from "./fake-podman.ts";
import { fakePodman } from "./fake-podman.ts";

const unsaved = -1;
const screens = ["None", "ItemMenu", "Title", "Load", "Loading"];

/** A game with the values the title, a load and control are told by, and the ones the tests read. */
export const game: FakeGame = {
	key: "5eed",
	values: {
		"0x578d41e1": {
			type: "Enum",
			value: "None",
			choices: screens,
			file: unsaved,
		},
		UI_Debug_IsOpenedTitle: { type: "Bool", value: false, file: unsaved },
		Sequence_IsLoadingBanc: { type: "Bool", value: false, file: unsaved },
		Sequence_CurrentBanc: { type: "String64", value: "" },
		Sequence_IsChildhood: { type: "Bool", value: true },
		"CaptionData.IsAutoSave": { type: "Bool", value: true, file: 2 },
		"IsTreasureBoxOpen.5005": { type: "Bool", value: false },
		"IsTreasureBoxOpen.6006": { type: "Bool", value: false },
		"PlayerStatus.CurrentRupee": { type: "Int", value: 0 },
		Fake_Lamp_Lit: { type: "Bool", value: false, file: unsaved },
	},
};

/** What the base save holds over the game's defaults: Link in Kokiri Forest. */
const baseHolds: Readonly<Record<string, Scalar>> = {
	Sequence_CurrentBanc: "KokiriForest",
};

const userFolderOf = (profiles: Uint8Array) => {
	const view = new DataView(profiles.buffer, profiles.byteOffset);
	const word = (at: number) =>
		Str.padStart(
			16,
			"0"
		)(Str.toUpperCase(view.getBigUint64(0x10 + at, true).toString(16)));
	return `${word(8)}${word(0)}`;
};

const placeFolder = (container: FakeContainer) =>
	Str.replace(
		":/place",
		""
	)(Option.getOrThrow(Arr.findFirst(container.args, Str.endsWith(":/place"))));

/** An Enum's value as the game holds it, its choice's name, from the hash a save records. */
const fakeOf = (value: FakeGame["values"][string], held: Scalar): FakeValue => {
	if (value.type !== "Enum" || typeof held !== "number") {
		return { ...value, value: held } as FakeValue;
	}
	const choice = Arr.findFirst(
		value.choices,
		(each) => Bun.hash.murmur32v3(each) === held
	);
	return { ...value, value: Option.getOrThrow(choice) };
};

/** Each saved value of the game as the place's two save files hold it, and each unsaved one at its default. */
const heldBy = (progress: Uint8Array, caption: Uint8Array) =>
	Effect.map(
		Effect.all(
			[decode("progress.sav", progress), decode("caption.sav", caption)],
			{ concurrency: 1 }
		),
		([progressSave, captionSave]) =>
			Rec.fromEntries(
				Arr.flatMap(Rec.toEntries(game.values), ([name, value]) => {
					if (
						value.type === "Vector3" ||
						value.type === "String16" ||
						value.type === "String32"
					) {
						return [];
					}
					const hash = Str.startsWith("0x")(name)
						? Number.parseInt(name, 16)
						: Bun.hash.murmur32v3(name);
					const file = value.file ?? 0;
					const saved =
						file === 0 || file === 2
							? read(file === 0 ? progressSave : captionSave, hash)
							: Option.none();
					const held = Option.getOrElse(saved, () => value.value as Scalar);
					return [[name, fakeOf(value, held)] as const];
				})
			)
	);

export const fakeMachine = (options?: {
	readonly places?: number;
	/** What A does once Link can move. */
	readonly onA?: (lichii: FakeLichii) => void;
	/** Values the base save holds besides the game's defaults. */
	readonly base?: Readonly<Record<string, Scalar>>;
	/** Values no save holds, as the game holds them when it starts. */
	readonly unsaved?: Readonly<Record<string, Value>>;
	/** Values the game writes itself between the load and control, over what the save held. */
	readonly rewrites?: Readonly<Record<string, Value>>;
	/** The place's container exits before its debug server answers. */
	readonly crashes?: boolean;
}) =>
	Effect.gen(function* () {
		const fs = yield* FileSystem.FileSystem;
		const real = yield* ChildProcessSpawner.ChildProcessSpawner;
		const home = yield* fs.makeTempDirectoryScoped();
		const nup = `${home}/game.nup`;
		yield* fs.writeFileString(nup, "");
		const blank = yield* decode("progress.sav", yield* fakeSave(game, 0));
		const progress = encode(
			yield* write(
				blank,
				Arr.map(
					Rec.toEntries({ ...baseHolds, ...options?.base }),
					([name, value]) => ({
						hash: Bun.hash.murmur32v3(name),
						value,
					})
				),
				[]
			)
		);
		const caption = yield* fakeSave(game, 2);
		const option = new Uint8Array([1, 2, 3]);
		const base = `${home}/data/game-testing/bases/test-base`;
		yield* fs.makeDirectory(base, { recursive: true });
		yield* fs.writeFile(`${base}/progress.sav`, progress);
		yield* fs.writeFile(`${base}/caption.sav`, caption);
		yield* fs.writeFile(`${base}/option.sav`, option);
		const sha256 = (bytes: Uint8Array) =>
			new Bun.CryptoHasher("sha256").update(bytes).digest("hex");

		const games = yield* Ref.make<readonly FakeLichii[]>([]);
		const fork = Effect.runForkWith(yield* Effect.context<never>());
		const play = Effect.fnUntraced(function* (container: FakeContainer) {
			const folder = placeFolder(container);
			const nand = `${folder}/data/lichii/nand`;
			const profiles = yield* fs.readFile(
				`${nand}/system/save/8000000000000010/su/avators/profiles.dat`
			);
			const slot = `${nand}/user/save/0000000000000000/${userFolderOf(profiles)}/0400486024DEC000/slot_00`;
			const held = yield* heldBy(
				yield* fs.readFile(`${slot}/progress.sav`),
				yield* fs.readFile(`${slot}/caption.sav`)
			);
			const values = Rec.map(held, (value, name) => {
				const at = options?.unsaved?.[name];
				return at === undefined
					? value
					: ({ ...value, value: at } as FakeValue);
			});
			const scene = values.Sequence_CurrentBanc?.value as Value;
			/** As the game's title takes A: its menu, the list of saves, the question that loads one, and then play. */
			let menu: "title" | "list" | "question" | "play" = "title";
			const lichii: FakeLichii = yield* fakeLichii({
				path: `${folder}/debug.sock`,
				presents: 1,
				values: {
					...values,
					"0x578d41e1": { type: "Enum", value: "Title", choices: screens },
					UI_Debug_IsOpenedTitle: { type: "Bool", value: true },
					Sequence_CurrentBanc: { type: "String64", value: "" },
				},
				onInput: (steps) => {
					const pressedA = Arr.some(steps, (step) =>
						Arr.contains(
							(step.buttons as readonly string[] | undefined) ?? [],
							"A"
						)
					);
					if (!pressedA) {
						return;
					}
					if (menu === "play") {
						options?.onA?.(lichii);
						return;
					}
					if (menu === "title") {
						menu = "list";
						fork(lichii.put("0x578d41e1", "Load"));
						return;
					}
					if (menu === "list") {
						menu = "question";
						return;
					}
					menu = "play";
					// The title's flag stays set through the load and in play.
					fork(
						Effect.gen(function* () {
							yield* lichii.put("0x578d41e1", "Loading");
							yield* lichii.put("Sequence_IsLoadingBanc", true);
							yield* Effect.sleep("30 millis");
							yield* lichii.put("Sequence_CurrentBanc", scene);
							yield* Effect.forEach(
								Rec.toEntries(options?.rewrites ?? {}),
								([name, value]) => lichii.put(name, value),
								{ concurrency: 1, discard: true }
							);
							yield* lichii.put("Sequence_IsLoadingBanc", false);
							yield* lichii.put("0x578d41e1", "None");
						})
					);
				},
			});
			yield* Ref.update(games, (all) => [...all, lichii]);
		});

		const dump = yield* fakeDump(game);
		const podman = yield* fakePodman({
			devices: ["nvidia.com/gpu=all"],
			onStart: (container) =>
				options?.crashes === true
					? Deferred.succeed(container.exit, 1).pipe(Effect.asVoid)
					: Effect.scoped(
							Effect.andThen(play(container), Deferred.await(container.exit))
						).pipe(Effect.orDie),
			otherwise: ChildProcessSpawner.make((command) =>
				command._tag === "StandardCommand" && command.command === "kaepora-dump"
					? dump.spawner.spawn(command)
					: real.spawn(command)
			),
		});
		const config = ConfigProvider.layer(
			ConfigProvider.fromUnknown({
				HOME: home,
				XDG_CACHE_HOME: `${home}/cache`,
				XDG_DATA_HOME: `${home}/data`,
				XDG_STATE_HOME: `${home}/state`,
				XDG_RUNTIME_DIR: `${home}/run`,
				GAME_TESTING_GAME_FILE: nup,
				GAME_TESTING_PATCHER: "kaepora-dump",
				GAME_TESTING_PLACES: String(options?.places ?? 1),
				GAME_TESTING_CARD: "nvidia",
				GAME_TESTING_LICHII: `${home}/lichii`,
				GAME_TESTING_DATA: `${home}/data`,
			})
		);
		const own = Layer.succeed(
			OwnTables,
			OwnTables.of({
				spots: new Map(),
				bases: new Map([
					[
						"test-base",
						{
							name: "test-base",
							hour: "day",
							age: "child",
							progress: sha256(progress),
							caption: sha256(caption),
							option: sha256(option),
							about: "the fake game's defaults, in Kokiri Forest",
						},
					],
				]),
				tiers: [],
				checks: new Map<string, Check>(),
			})
		);
		/** What the entry point gives every command, made of the fakes. */
		const platform = Layer.mergeAll(
			own,
			UserDirectories.layer,
			Layer.succeed(Logger.LogToStderr, true)
		).pipe(
			Layer.provideMerge(Layer.merge(BunServices.layer, podman.layer)),
			Layer.provideMerge(config)
		);
		const client = Layer.unwrap(
			Effect.gen(function* () {
				const socket = yield* HostSocket;
				return Game.layer(socket.path);
			})
		).pipe(Layer.provideMerge(host), Layer.provideMerge(platform));
		return {
			layer: client,
			platform,
			podman,
			games: Ref.get(games),
			home,
		};
	});
