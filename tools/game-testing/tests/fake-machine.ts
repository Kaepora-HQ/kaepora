/**
 * A machine with the game, made of fakes: the fake `kaepora-dump` reads a fake game's tables, the fake podman makes
 * places, and each place's container runs a fake Lichii that loads the save the place holds, as Lichii does: from the
 * folder of the user `profiles.dat` names. Its title, its list of saves and the question that loads one each take A,
 * as the game's do, and then it loads and gives Link control. Everything else a process asks for, a script's child or
 * the type check, runs for real.
 */
import { BunServices } from "@effect/platform-bun";
import type { Duration } from "effect";
import {
	Array as Arr,
	ConfigProvider,
	Context,
	Deferred,
	Effect,
	FileSystem,
	Layer,
	Logger,
	Option,
	Order,
	Record as Rec,
	Redacted,
	Ref,
	String as Str,
} from "effect";
import { HttpServer } from "effect/http";
import { NetAddress } from "effect/net";
import { UserDirectories } from "effect/persistence";
import { ChildProcessSpawner } from "effect/process";
import type { Value } from "../src/contract/values.ts";
import { Game } from "../src/game.ts";
import { HostSocket, host } from "../src/host/host.ts";
import type { Scalar } from "../src/save/codec.ts";
import { decode, encode, read, write } from "../src/save/codec.ts";
import type { Check } from "../src/save/tables.ts";
import { OwnTables } from "../src/save/tables.ts";
import { Scripts } from "../src/scripts/runner.ts";
import { serve } from "../src/serve/server.ts";
import type { FakeGame } from "./fake-game.ts";
import { fakeDump, fakeSave } from "./fake-game.ts";
import type { FakeLichii, FakeValue } from "./fake-lichii.ts";
import { fakeLichii } from "./fake-lichii.ts";
import type { FakeContainer } from "./fake-podman.ts";
import { fakePodman } from "./fake-podman.ts";

/** What a place held when its game started: each file under its mods folder, by path, and its settings. */
export interface Started {
	readonly mods: Readonly<Record<string, Uint8Array>>;
	readonly settings: string;
}

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
		"PlayerStatus.SavePosRadY": { type: "Float", value: 0 },
		Player_Rot_For_Map: { type: "Float", value: 0 },
		Fake_Float: { type: "Float", value: 0 },
		"PlayerStatus.SavePos": { type: "Vector3", value: [0, 0, 0] },
		Fake_Lamp_Lit: { type: "Bool", value: false, file: unsaved },
	},
	scenes: [{ name: "KokiriForest", folder: "KokiriForest" }],
	placements: {
		"Banc/KokiriForest/KokiriForest_Static.bcett.byml.zs": {
			Actors: [{ Gyaml: "Obj_TBox_Fake", Hash: "5005", Translate: [1, 2, 3] }],
		},
	},
	mods: {
		"Too Large Mod": {
			lines: [{ path: "romfs/Fake/Big.byml.zs", needs: 4096, entry: 2048 }],
			code: 3,
		},
		"Unmeasured Mod": {
			lines: [
				{
					path: "Component/AIScheduleParam/Npc_Fake.game__component__AIScheduleParam.bgyml",
					unchecked:
						"the game's own size table gives files of its kind no one overhead, so what a larger one needs is not known",
				},
			],
			code: 0,
		},
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
					if (value.type === "String16" || value.type === "String32") {
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
	/** A once Link can move autosaves, as opening a chest does: the loaded save with these values, into `slot_01`. */
	readonly autosaveOnA?: Readonly<Record<string, Scalar>>;
	/** As `autosaveOnA`, written 100 ms after A is answered, so no look after the step sees it. */
	readonly autosaveLate?: Readonly<Record<string, Scalar>>;
	/** Values the base save holds besides the game's defaults. */
	readonly base?: Readonly<Record<string, Scalar>>;
	/** Values no save holds, as the game holds them when it starts. */
	readonly unsaved?: Readonly<Record<string, Value>>;
	/** Values the game writes itself between the load and control, over what the save held. */
	readonly rewrites?: Readonly<
		Record<string, Value | readonly [number, number, number]>
	>;
	/** The place's container exits before its debug server answers. */
	readonly crashes?: boolean;
	/** Config keys besides the machine's own, such as a far machine's URL and token. */
	readonly env?: Readonly<Record<string, string>>;
	/** The machine keeps no mod that skips, as one set up without it. */
	readonly noSkips?: boolean;
	/** Every mod changes the bytes Lichii's instant text add-on patches, so Lichii leaves the add-on out. */
	readonly textBytesTaken?: boolean;
	/** How long a place's container takes to stop. */
	readonly stopTakes?: Duration.Input;
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

		const skips = `${home}/data/game-testing/skips`;
		if (options?.noSkips !== true) {
			yield* fs.makeDirectory(`${skips}/romfs/System`, { recursive: true });
			yield* fs.writeFile(
				`${skips}/romfs/System/Skips.txt`,
				new Uint8Array([7, 7])
			);
		}
		/** Writes the save loaded from `slot` with `values` beside it, as the game writes an autosave. */
		const autosave = Effect.fnUntraced(function* (
			slot: string,
			values: Readonly<Record<string, Scalar>>
		) {
			const loaded = yield* decode(
				"progress.sav",
				yield* fs.readFile(`${slot}/progress.sav`)
			);
			const shown = yield* decode(
				"caption.sav",
				yield* fs.readFile(`${slot}/caption.sav`)
			);
			const hashed = (written: Readonly<Record<string, Scalar>>) =>
				Arr.map(Rec.toEntries(written), ([name, value]) => ({
					hash: Bun.hash.murmur32v3(name),
					value,
				}));
			const next = Str.replace("slot_00", "slot_01")(slot);
			yield* fs.makeDirectory(next, { recursive: true });
			yield* fs.writeFile(
				`${next}/progress.sav`,
				encode(yield* write(loaded, hashed(values), []))
			);
			yield* fs.writeFile(
				`${next}/caption.sav`,
				encode(
					yield* write(shown, hashed({ "CaptionData.IsAutoSave": true }), [])
				)
			);
		}, Effect.orDie);
		const games = yield* Ref.make<readonly FakeLichii[]>([]);
		const starts = yield* Ref.make<readonly Started[]>([]);
		const fork = Effect.runForkWith(yield* Effect.context<never>());
		const settle = Effect.runPromiseWith(yield* Effect.context<never>());
		const play = Effect.fnUntraced(function* (container: FakeContainer) {
			const folder = placeFolder(container);
			const load = `${folder}/data/lichii/load/0400486024DEC000`;
			const loaded = yield* fs.readDirectory(load, { recursive: true });
			const mods = yield* Effect.forEach(
				Arr.sort(loaded, Order.String),
				Effect.fnUntraced(function* (path) {
					const info = yield* fs.stat(`${load}/${path}`);
					return info.type === "File"
						? [[path, yield* fs.readFile(`${load}/${path}`)] as const]
						: [];
				}),
				{ concurrency: 1 }
			);
			const settings = yield* fs.readFileString(
				`${folder}/config/lichii/qt-config.ini`
			);
			yield* Ref.update(starts, (all) => [
				...all,
				{ mods: Rec.fromEntries(Arr.flatten(mods)), settings },
			]);
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
			/** What A does in play, answered once its autosave is written. */
			const played = () => {
				options?.onA?.(lichii);
				if (options?.autosaveLate !== undefined) {
					fork(
						Effect.andThen(
							Effect.sleep("100 millis"),
							autosave(slot, options.autosaveLate)
						)
					);
				}
				return options?.autosaveOnA === undefined
					? undefined
					: settle(autosave(slot, options.autosaveOnA));
			};
			const lichii: FakeLichii = yield* fakeLichii({
				path: `${folder}/debug.sock`,
				presents: 1,
				instantText:
					Str.includes("+Text Speed Instant (built-in)")(settings) &&
					options?.textBytesTaken !== true,
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
						return played();
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
			...(options?.stopTakes === undefined
				? {}
				: { stopTakes: options.stopTakes }),
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
				...options?.env,
			})
		);
		const own = Layer.succeed(
			OwnTables,
			OwnTables.of({
				spots: [
					{
						name: "Fake Spot Before The Chest",
						scene: "KokiriForest",
						position: [1, 2, 3],
						facing: 0,
						found: "save",
						evidence: "a save the game wrote",
						note: "",
					},
				],
				chests: ["Fake Chest Check"],
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
				checks: new Map<string, Check>([
					[
						"Fake Chest Check",
						{
							name: "Fake Chest Check",
							flows: [],
							placements: ["5005"],
							stock: [],
						},
					],
				]),
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
		const hosting = host.pipe(Layer.provideMerge(platform));
		const client = Layer.unwrap(
			Effect.gen(function* () {
				const socket = yield* HostSocket;
				return Game.layer(socket.path);
			})
		).pipe(Layer.provideMerge(hosting));
		return {
			layer: client,
			host: hosting,
			platform,
			podman,
			games: Ref.get(games),
			starts: Ref.get(starts),
			home,
		};
	});

export type Machine = Effect.Success<ReturnType<typeof fakeMachine>>;

/** The token every fake machine's `serve` takes. */
export const token = "a-token-made-for-this-test";

/** `serve` on a fake machine, on a port the system picks: the host it listens on, and its URL. */
export const listening = (machine: Machine) =>
	Effect.gen(function* () {
		const server = Context.get(
			yield* Layer.build(
				serve({ port: 0, token: Redacted.make(token), version: "test" }).pipe(
					Layer.provide(Scripts.layer.pipe(Layer.provideMerge(machine.host)))
				)
			),
			HttpServer.HttpServer
		);
		const { address } = server;
		return address._tag === "UnixPathAddress"
			? yield* Effect.die(new Error("serve listens on a TCP port"))
			: {
					host: NetAddress.formatHost(address),
					url: `http://127.0.0.1:${address.port}`,
				};
	});

export const served = (machine: Machine) =>
	Effect.map(listening(machine), ({ url }) => url);

/**
 * An agent's machine: no game file, no kaepora-dump, no podman and no save, only Bun, this package, and the URL and
 * token of a far machine's `serve`.
 */
export const fakeCaller = (url: string) =>
	Effect.gen(function* () {
		const fs = yield* FileSystem.FileSystem;
		const home = yield* fs.makeTempDirectoryScoped();
		const config = ConfigProvider.layer(
			ConfigProvider.fromUnknown({
				HOME: home,
				XDG_CACHE_HOME: `${home}/cache`,
				XDG_DATA_HOME: `${home}/data`,
				XDG_STATE_HOME: `${home}/state`,
				XDG_RUNTIME_DIR: `${home}/run`,
				GAME_TESTING_URL: url,
				GAME_TESTING_TOKEN: token,
			})
		);
		const platform = Layer.mergeAll(
			OwnTables.layer,
			UserDirectories.layer,
			Layer.succeed(Logger.LogToStderr, true)
		).pipe(Layer.provideMerge(BunServices.layer), Layer.provideMerge(config));
		return { home, platform };
	});
