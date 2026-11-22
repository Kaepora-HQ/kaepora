/**
 * `generated/names.d.ts`, which fills `Register` in `src/names.ts` from the game's tables, so a script's value names
 * and values are checked as it is typed and before its game starts. Its names are read from the game's files, so git
 * ignores the folder.
 */
import { Array as Arr, Effect, FileSystem, Formatter, Option } from "effect";
import { OwnTables } from "../save/tables.ts";
import type { GameValue } from "./game-data.ts";
import { GameData } from "./game-data.ts";

const namesFolder = `${import.meta.dir}/../../generated`;
export const namesFile = `${namesFolder}/names.d.ts`;

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

export const writeNames = Effect.gen(function* () {
	const data = yield* GameData;
	const own = yield* OwnTables;
	const fs = yield* FileSystem.FileSystem;
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
	const text = Arr.join(
		[
			"// Written by `game-testing names` from the game's tables.",
			'import "../src/names.ts";',
			"",
			'declare module "../src/names.ts" {',
			"\tinterface Register {",
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
			"\t}",
			"}",
			"",
		],
		"\n"
	);
	yield* Effect.orDie(
		fs.makeDirectory(namesFolder, {
			recursive: true,
		})
	);
	yield* Effect.orDie(fs.writeFileString(namesFile, text));
	yield* Effect.logInfo(`Wrote ${values.length} values to ${namesFile}.`);
});
