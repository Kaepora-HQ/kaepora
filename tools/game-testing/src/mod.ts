/**
 * A mod as the library moves it: read from its folder on the machine that has it, into its name and files, which
 * travel to the game's machine whole.
 */
import type { FileSystem, Path } from "effect";
import { Array as Arr, Effect, Option, Order, Schema } from "effect";
import { Mod, modLimit } from "./contract/state.ts";
import { ModTooLarge, ModUnreadable } from "./errors.ts";

/** A file's bytes as a start sends them: base64, four characters for every three bytes, beside its path. */
const sent = (file: Mod["files"][number]) =>
	Math.ceil(file.bytes.length / 3) * 4 + file.path.length;

/**
 * Every file under `folder`, in path order, with the folder's own name as the mod's. The mod is checked as the game's
 * machine checks a start, so a mod read here passes there, and refused when larger than a start can carry.
 */
export const readMod = Effect.fn("readMod")(function* (
	fs: FileSystem.FileSystem,
	path: Path.Path,
	folder: string
) {
	const paths = yield* fs
		.readDirectory(folder, { recursive: true })
		.pipe(
			Effect.mapError(() => ModUnreadable.make({ folder, reason: "missing" }))
		);
	const files = yield* Effect.forEach(
		Arr.sort(paths, Order.String),
		Effect.fnUntraced(function* (file) {
			const at = path.join(folder, file);
			const info = yield* fs.stat(at);
			return info.type === "File"
				? Option.some({ path: file, bytes: yield* fs.readFile(at) })
				: Option.none();
		}, Effect.orDie),
		{ concurrency: 1 }
	);
	const found = Arr.getSomes(files);
	if (!Arr.isReadonlyArrayNonEmpty(found)) {
		return yield* ModUnreadable.make({ folder, reason: "empty" });
	}
	const bytes = Arr.reduce(found, 0, (total, file) => total + sent(file));
	if (bytes > modLimit) {
		return yield* ModTooLarge.make({ folder, bytes, limit: modLimit });
	}
	return yield* Schema.decodeEffect(Mod)({
		name: path.basename(folder),
		files: found,
	}).pipe(
		Effect.mapError(() => ModUnreadable.make({ folder, reason: "unnamed" }))
	);
});

/** SHA-256 over a mod's files, each path then its bytes, in path order. */
export const digestOf = (mod: Mod) => {
	const hash = new Bun.CryptoHasher("sha256");
	for (const file of Arr.sort(
		mod.files,
		Order.mapInput(Order.String, (each: Mod["files"][number]) => each.path)
	)) {
		hash.update(file.path);
		hash.update(file.bytes);
	}
	return hash.digest("hex");
};
