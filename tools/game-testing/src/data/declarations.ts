/**
 * The declarations that fill `Register` in `src/names.ts` from the game's tables, so a script's value names and values
 * are checked as it is typed and before its game starts. A machine with the game writes them to
 * `generated/names.d.ts`, where the package's own check and an editor read them; a caller of a far machine writes that
 * machine's to its own cache. Their names are read from the game's files, so git ignores both.
 */
import {
	Array as Arr,
	Effect,
	FileSystem,
	Formatter,
	Option,
	Path,
} from "effect";
import { UserDirectories } from "effect/persistence";
import { OwnTables } from "../save/tables.ts";
import type { GameValue } from "./game-data.ts";
import { GameData } from "./game-data.ts";

/** The package's `generated` folder, by a path with no `..`: a type check finds a file by the text of its path. */
const namesFolder = Bun.fileURLToPath(
	new URL("../../generated", import.meta.url)
);
export const namesFile = `${namesFolder}/names.d.ts`;

/** What a type check includes for this machine's own names. */
export const namesHere = `${namesFolder}/**/*.ts`;

/** The module the declarations fill, by its path, for a file that does not sit in the package. */
const namesModule = Bun.fileURLToPath(new URL("../names.ts", import.meta.url));

const quoted = (text: string) => Formatter.formatJson(text);

const union = (names: readonly string[]) =>
	names.length === 0 ? "never" : Arr.join(Arr.map(names, quoted), " | ");

/** The TypeScript type of what reading a value gives, for the types the game's memory is read for. */
const typeOf = (value: GameValue): Option.Option<string> => {
	switch (value.type) {
		case "Bool":
			return Option.some("boolean");
		case "Int":
		case "UInt":
		case "Float":
			return Option.some("number");
		case "Enum":
			return Option.some(union(value.choices));
		case "String64":
			return Option.some("string");
		default:
			return Option.none();
	}
};

/** What fills `Register`, from the game's tables and our own: the declaration file's body. */
export const namesText = Effect.gen(function* () {
	const data = yield* GameData;
	const own = yield* OwnTables;
	const valueNames = yield* data.names("value");
	const found = yield* Effect.forEach(valueNames, data.find, {
		concurrency: 1,
	});
	const values = Arr.flatMap(Arr.getSomes(found), (value) =>
		Option.match(typeOf(value), {
			onNone: () => [],
			onSome: (type) => [`\t\t\treadonly ${quoted(value.name)}: ${type};`],
		})
	);
	const questNames = yield* data.names("quest");
	const quests = yield* Effect.forEach(
		questNames,
		(quest) =>
			Effect.map(data.find(`Step_${quest}`), (step) =>
				Option.match(step, {
					onNone: () => [],
					onSome: (value) => [
						`\t\t\treadonly ${quoted(quest)}: ${union(value.choices)};`,
					],
				})
			),
		{ concurrency: 1 }
	);
	const items = yield* data.names("item");
	const scenes = yield* data.names("scene");
	const landings = yield* data.names("landing");
	return Arr.join(
		[
			"\t\treadonly values: {",
			...values,
			"\t\t};",
			`\t\treadonly items: ${union(items)};`,
			"\t\treadonly quests: {",
			...Arr.flatten(quests),
			"\t\t};",
			`\t\treadonly scenes: ${union(scenes)};`,
			`\t\treadonly landings: ${union(landings)};`,
			`\t\treadonly checks: ${union([...own.checks.keys()])};`,
		],
		"\n"
	);
});

/**
 * Writes a declaration file of `names`' body to `file`, for the names module at `module`. It is written beside and
 * renamed over the last, so a type check never reads half of one.
 */
const writeDeclarations = Effect.fnUntraced(function* (
	fs: FileSystem.FileSystem,
	path: Path.Path,
	file: string,
	names: string,
	module: string
) {
	yield* fs.makeDirectory(path.dirname(file), { recursive: true });
	yield* fs.writeFileString(
		`${file}.new`,
		Arr.join(
			[
				"// Written by `game-testing names` from the game's tables.",
				`import ${quoted(module)};`,
				"",
				`declare module ${quoted(module)} {`,
				"\tinterface Register {",
				names,
				"\t}",
				"}",
				"",
			],
			"\n"
		)
	);
	yield* fs.rename(`${file}.new`, file);
	yield* Effect.logInfo(`Wrote the game's names to ${file}.`);
}, Effect.orDie);

export const writeNames = Effect.gen(function* () {
	const fs = yield* FileSystem.FileSystem;
	const path = yield* Path.Path;
	const names = yield* namesText;
	yield* writeDeclarations(fs, path, namesFile, names, "../src/names.ts");
});

/** Where a caller of a far machine writes that machine's names: this machine's cache. */
export const farNamesFile = Effect.map(
	Effect.flatMap(UserDirectories.UserDirectories, (directories) =>
		Effect.orDie(directories.cache("game-testing"))
	),
	(folder) => `${folder}/names.d.ts`
);

/** Writes a far machine's names to `file`, for a type check of a script that runs against it, and gives the file. */
export const writeFarDeclarations = (
	fs: FileSystem.FileSystem,
	path: Path.Path,
	file: string,
	names: string
) => Effect.as(writeDeclarations(fs, path, file, names, namesModule), file);
