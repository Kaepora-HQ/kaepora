import {
	Array as Arr,
	Effect,
	Layer,
	Record as Rec,
	Ref,
	Sink,
	String as Str,
	Stream,
} from "effect";
import { ChildProcessSpawner } from "effect/process";
import type { SaveFile, SaveRecord } from "../src/save/codec.ts";
import { encode, write } from "../src/save/codec.ts";

export type FakeValue = { readonly file?: number } & (
	| { readonly type: "Bool"; readonly value: boolean }
	| { readonly type: "Int" | "UInt" | "Float"; readonly value: number }
	| {
			readonly type: "Enum";
			readonly value: string;
			readonly choices: readonly string[];
	  }
	| {
			readonly type: "String16" | "String32" | "String64";
			readonly value: string;
	  }
	| {
			readonly type: "Vector3";
			readonly value: readonly [number, number, number];
	  }
);

type Row = Readonly<Record<string, unknown>>;

export interface FakeGame {
	readonly actorInfo?: readonly Row[];
	readonly flows?: Readonly<Record<string, readonly string[]>>;
	readonly key: string;
	readonly keys?: readonly string[];
	readonly landings?: Readonly<Record<string, Readonly<Record<string, Row>>>>;
	readonly mods?: Readonly<
		Record<string, { readonly lines: readonly Row[]; readonly code: number }>
	>;
	readonly modules?: Readonly<Record<string, readonly string[]>>;
	readonly placements?: Readonly<Record<string, Row>>;
	readonly pouch?: readonly Row[];
	readonly quests?: readonly string[];
	readonly scenes?: readonly {
		readonly name: string;
		readonly folder: string;
		readonly rooms?: number;
		readonly field?: boolean;
	}[];
	readonly text?: Readonly<Record<string, string>>;
	readonly values: Readonly<Record<string, FakeValue>>;
}

const hash = (name: string) =>
	Str.startsWith("0x")(name)
		? Number.parseInt(name, 16)
		: Bun.hash.murmur32v3(name);

const named = (name: string) => !Str.startsWith("0x")(name);

const tables = {
	actorInfo: "RSDB/ActorInfo.Product.111.rstbl.byml.zs",
	pouch: "RSDB/PouchActorInfo.Product.111.rstbl.byml.zs",
	challenge: "RSDB/Challenge.Product.111.rstbl.byml.zs",
	dungeons: "RSDB/DungeonParam.Product.111.rstbl.byml.zs",
	scenes: "RSDB/GameBancInfo.Product.111.rstbl.byml.zs",
	list: "GameData/GameDataList.Product.110.byml.zs",
	startPos: "Banc/BancBasedResource/StartPos.byml.zs",
	text: "Mals/USen.Product.110.sarc.zs",
} as const;

const typeNames = [
	"Bool",
	"Int",
	"UInt",
	"Float",
	"Enum",
	"String16",
	"String32",
	"String64",
	"Vector3",
] as const;

export const gameDataList = (game: FakeGame) => {
	const values = Rec.toEntries(game.values);
	const group = (type: FakeValue["type"]) =>
		Arr.flatMapNullishOr(values, ([name, value]) => {
			if (value.type !== type) {
				return;
			}
			const entry = { Hash: hash(name), SaveFileIndex: value.file ?? 0 };
			switch (value.type) {
				case "Enum":
					return {
						...entry,
						DefaultValue: hash(value.value),
						RawValues: value.choices,
					};
				case "Vector3": {
					const [x, y, z] = value.value;
					return { ...entry, DefaultValue: { x, y, z } };
				}
				default:
					return { ...entry, DefaultValue: value.value };
			}
		});
	const members = new Map<string, Map<string, string>>();
	for (const [name] of Arr.filter(values, ([each]) => named(each))) {
		const parts = Str.split(name, ".");
		for (const [index] of parts.entries()) {
			if (index > 0) {
				const parent = Arr.join(parts.slice(0, index), ".");
				const struct = members.get(parent) ?? new Map<string, string>();
				struct.set(
					parts[index] as string,
					Arr.join(parts.slice(0, index + 1), ".")
				);
				members.set(parent, struct);
			}
		}
	}
	return {
		Data: {
			...Rec.fromEntries(Arr.map(typeNames, (type) => [type, group(type)])),
			Struct: Arr.map([...members], ([struct, fields]) => ({
				Hash: hash(struct),
				DefaultValue: Arr.map([...fields], ([member, full]) => ({
					Hash: hash(member),
					Value: hash(full),
				})),
			})),
			Bool64bitKey: Arr.map(game.keys ?? [], (key) => ({ Hash: key })),
		},
		MetaData: { FormatVersion: 1 },
	};
};

const files = (game: FakeGame): Readonly<Record<string, unknown>> => ({
	[tables.actorInfo]: game.actorInfo ?? [],
	[tables.pouch]: game.pouch ?? [],
	[tables.challenge]: Arr.map(game.quests ?? [], (quest) => ({
		__RowId: `Work/Challenge/${quest}.game__challenge__Challenge.gyml`,
	})),
	[tables.dungeons]: Arr.flatMapNullishOr(game.scenes ?? [], (scene) =>
		scene.rooms === undefined
			? undefined
			: {
					__RowId: scene.name,
					RoomParam: Arr.replicate({}, scene.rooms),
					...(scene.field === true ? { IsFieldData: true } : {}),
				}
	),
	[tables.scenes]: Arr.map(game.scenes ?? [], (scene) => ({
		__RowId: scene.name,
		BancMain: `Work/Banc/Scene/${scene.folder}/${scene.name}.bcett.json`,
	})),
	[tables.list]: gameDataList(game),
	[tables.startPos]: game.landings ?? {},
	[`${tables.text}/ActorMsg/PouchContent.msbt`]: Arr.map(
		Rec.toEntries(game.text ?? {}),
		([label, text]) => ({ label, text })
	),
	...(game.placements ?? {}),
});

const sources = (
	game: FakeGame
): Readonly<Record<string, readonly string[]>> => ({
	...Rec.mapKeys(
		game.modules ?? {},
		(name) => `Logic/${name}.logic.module.ainb`
	),
	...Rec.mapKeys(
		game.flows ?? {},
		(name) => `Event/EventFlow/${name}.bfevfl.zs`
	),
});

const allStrings = (game: FakeGame) =>
	Arr.dedupe([
		...Arr.flatMap(Rec.keys(game.values), (name) =>
			named(name) ? Str.split(name, ".") : []
		),
		...Arr.flatten(Rec.values(sources(game))),
	]);

const line = (value: unknown) => JSON.stringify(value);

const answer = (
	game: FakeGame,
	args: readonly string[]
): { readonly stdout: string; readonly code: number } => {
	const [command, , ...rest] = args;
	const known = { ...files(game), ...sources(game) };
	switch (command) {
		case "key":
			return {
				stdout: line({ build: "FAKE", format: 1, key: game.key }),
				code: 0,
			};
		case "list":
			return {
				stdout: Arr.join(
					Arr.filter([...Rec.keys(known), tables.text], (path) =>
						Str.startsWith(rest[0] ?? "")(path)
					),
					"\n"
				),
				code: 0,
			};
		case "files":
			return {
				stdout: Arr.join(
					Arr.map(rest, (path) =>
						line({ path, root: files(game)[path], sha256: "0" })
					),
					"\n"
				),
				code: 0,
			};
		case "strings":
			return {
				stdout:
					rest.length === 0
						? Arr.join(allStrings(game), "\n")
						: Arr.join(
								Arr.map(rest, (path) =>
									line({ path, strings: sources(game)[path] ?? [] })
								),
								"\n"
							),
				code: 0,
			};
		case "check-mod": {
			// A mod is known by its folder's own name; one the game does not list fits the size table.
			const mod = game.mods?.[Arr.lastNonEmpty(Str.split(rest[0] ?? "", "/"))];
			return mod === undefined
				? { stdout: "", code: 0 }
				: { stdout: Arr.join(Arr.map(mod.lines, line), "\n"), code: mod.code };
		}
		default:
			return { stdout: "", code: 2 };
	}
};

export const fakeDump = Effect.fn("fakeDump")(function* (
	game: FakeGame,
	options?: { readonly fail?: string }
) {
	const commands = yield* Ref.make<readonly (readonly string[])[]>([]);
	const spawner = ChildProcessSpawner.make((command) =>
		Effect.suspend(() => {
			const args =
				command._tag === "StandardCommand" ? Arr.map(command.args, String) : [];
			const answered: Effect.Effect<{
				readonly stdout: string;
				readonly code: number;
				readonly stderr?: string;
			}> =
				options?.fail === undefined
					? opened(args)
					: Effect.succeed({ stdout: "", code: 1, stderr: options.fail });
			const bytes = (text: string) =>
				text.length === 0
					? Stream.empty
					: Stream.make(new TextEncoder().encode(`${text}\n`));
			return Ref.update(commands, (all) => [...all, args]).pipe(
				Effect.andThen(answered),
				Effect.map(({ stdout, code, stderr }) =>
					ChildProcessSpawner.makeHandle({
						pid: ChildProcessSpawner.ProcessId(1),
						exitCode: Effect.succeed(ChildProcessSpawner.ExitCode(code)),
						isRunning: Effect.succeed(false),
						kill: () => Effect.void,
						stdin: Sink.drain,
						stdout: bytes(stdout),
						stderr: bytes(stderr ?? ""),
						all: bytes(stdout),
						getInputFd: () => Sink.drain,
						getOutputFd: () => Stream.empty,
						unref: Effect.succeed(Effect.void),
					})
				)
			);
		})
	);
	/** `open` reads files on disk, which a fake mod holds as the JSON of their trees; the rest is the game's. */
	const opened = (args: readonly string[]) => {
		const [command, , ...rest] = args;
		if (command !== "open") {
			return Effect.succeed(answer(game, args));
		}
		// A file that is not a tree's JSON is refused as the dump refuses a file it cannot open.
		return Effect.forEach(
			rest,
			(path) =>
				Effect.map(
					Effect.tryPromise({
						try: () => Bun.file(path).json(),
						catch: () => path,
					}),
					(root: unknown) => line({ path, root })
				),
			{ concurrency: 1 }
		).pipe(
			Effect.map((lines) => ({ stdout: Arr.join(lines, "\n"), code: 0 })),
			Effect.orElseSucceed(() => ({
				stdout: "",
				code: 1,
				stderr: `${rest[0]}: neither a BYML file nor a message file`,
			}))
		);
	};
	return {
		spawner,
		layer: Layer.succeed(ChildProcessSpawner.ChildProcessSpawner, spawner),
		subcommands: Effect.map(Ref.get(commands), (all) =>
			Arr.map(all, (args) => args[0] ?? "")
		),
	};
});

const sizes = {
	String16: 16,
	String32: 32,
	String64: 64,
	Vector3: 12,
} as const;

export const fakeSave = (
	game: FakeGame,
	file: number,
	keys: readonly bigint[] = []
) => {
	const values = Arr.filter(
		Rec.toEntries(game.values),
		([, value]) => (value.file ?? 0) === file
	);
	const records = Arr.map(values, ([name, value]): SaveRecord => {
		switch (value.type) {
			case "String16":
			case "String32":
			case "String64":
			case "Vector3":
				return {
					type: value.type,
					hash: hash(name),
					bytes: new Uint8Array(sizes[value.type]),
				};
			default:
				return { type: value.type, hash: hash(name), word: 0 };
		}
	});
	const blank: SaveFile = {
		version: 0x2d_80_eb,
		kind: file,
		records,
		keys: file === 0 ? [] : undefined,
	};
	return Effect.map(
		write(
			blank,
			Arr.map(values, ([name, value]) => ({
				hash: hash(name),
				value: value.value,
			})),
			Arr.map(keys, (key) => ({ key, set: true }))
		),
		encode
	);
};

/**
 * A mod's game data list as the fake `open` reads it: the game's, with each start value as its default. The file's path
 * in a mod's folder is `startList`.
 */
export const startList = "romfs/GameData/GameDataList.Product.110.byml.zs";
export const modList = (
	game: FakeGame,
	starts: Readonly<Record<string, FakeValue["value"]>>
) =>
	new TextEncoder().encode(
		JSON.stringify(
			gameDataList({
				...game,
				values: Rec.map(game.values, (value, name) =>
					name in starts
						? ({ ...value, value: starts[name] } as FakeValue)
						: value
				),
			})
		)
	);
