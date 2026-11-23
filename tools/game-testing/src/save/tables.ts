/**
 * - `tables/spots.tsv`: where Link starts for a location by name, a check or a spot of its own, with how each was found
 *   and the look that started there. `Spots` reads it with the recorded table beside it.
 * - `tables/bases.tsv`: the base saves, which stay on machines with the game.
 * - Tiers from `remake/items.tsv`: the items that stand for one SoH item, in the table's order, which puts the
 *   Hookshot before the Longshot. The wallets and scales stand there too, highest first; the pouch table joins
 *   those, and its tiers are used before these.
 */
import {
	Array as Arr,
	Context,
	Effect,
	FileSystem,
	Layer,
	Order,
	Record as Rec,
	Schema,
	String as Str,
} from "effect";
import type { NameMatch } from "../contract/rpc.ts";
import type { Spot } from "../contract/spots.ts";
import { Found, Look, SpotName } from "../contract/spots.ts";
import { Age } from "../contract/state.ts";
import type { NameKind } from "../contract/values.ts";
import { RunId } from "../contract/values.ts";
import { stem } from "../data/dump.ts";
import type { GameData } from "../data/game-data.ts";

export interface Base {
	readonly about: string;
	readonly age: Age;
	/** The SHA-256 of its caption.sav. */
	readonly caption: string;
	readonly hour: "day" | "night";
	readonly name: string;
	/** The SHA-256 of its option.sav, empty for a base whose option.sav was not kept. */
	readonly option: string;
	/** The SHA-256 of its progress.sav. */
	readonly progress: string;
}

export interface Stock {
	readonly keeper: string;
	readonly slot: number;
}

export interface Check {
	readonly flows: readonly string[];
	readonly name: string;
	readonly placements: readonly string[];
	readonly stock: readonly Stock[];
}

const tables = `${import.meta.dir}/../../tables`;
const remake = `${import.meta.dir}/../../../../remake`;

const Text = Schema.String;
const Finite = Schema.FiniteFromString;
const Int = Schema.FiniteFromString.check(Schema.isInt());

const SpotRow = Schema.Struct({
	name: SpotName,
	scene: Text,
	room: Schema.Union([Schema.Literal(""), Int]),
	x: Finite,
	y: Finite,
	z: Finite,
	facing: Finite,
	found: Found,
	evidence: Text,
	look: Schema.Union([
		Schema.Literal(""),
		Schema.TemplateLiteralParser([Look.fields.outcome, " ", RunId]),
	]),
	note: Text,
});
const BaseRow = Schema.Struct({
	name: Text,
	hour: Schema.Literals(["day", "night"]),
	age: Age,
	progress: Text,
	caption: Text,
	option: Text,
	about: Text,
});
const ItemRow = Schema.Struct({ remake_item: Text, soh_item: Text });
const PlacedRow = Schema.Struct({ location: Text, hash: Text });
const FlowRow = Schema.Struct({ location: Text, flow: Text });
const ShopRow = Schema.Struct({ location: Text, slot: Int, shopkeepers: Text });

/** A table's rows from its text: a header of column names, then one line a row, cells split by tabs. */
const parseTable = Effect.fnUntraced(function* <
	S extends Schema.Top & { readonly DecodingServices: never },
>(text: string, row: S) {
	const [header = "", ...lines] = Arr.filter(
		Str.split(text, "\n"),
		Str.isNonEmpty
	);
	const columns = Str.split(header, "\t");
	const rows: unknown = Arr.map(lines, (line) => {
		const cells = Str.split(line, "\t");
		return Rec.fromEntries(
			Arr.map(columns, (column, i) => [column, cells[i] ?? ""])
		);
	});
	return yield* Schema.decodeUnknownEffect(Schema.Array(row))(rows);
});

const readTable = Effect.fn("OwnTables.read")(function* <
	S extends Schema.Top & { readonly DecodingServices: never },
>(path: string, row: S) {
	const fs = yield* FileSystem.FileSystem;
	const text = yield* fs.readFileString(path).pipe(Effect.orDie);
	return yield* parseTable(text, row).pipe(Effect.orDie);
});

/** The spots a table's text holds. A look is `passed <run>` or `failed <run>`. */
export const parseSpots = Effect.fnUntraced(function* (text: string) {
	const rows = yield* parseTable(text, SpotRow);
	return Arr.map(
		rows,
		(row): Spot => ({
			name: row.name,
			scene: row.scene,
			...(row.room === "" ? {} : { room: row.room }),
			position: [row.x, row.y, row.z],
			facing: row.facing,
			found: row.found,
			evidence: row.evidence,
			...(row.look === ""
				? {}
				: { look: { outcome: row.look[0], run: row.look[2] } }),
			note: row.note,
		})
	);
});

const byName = <A extends { readonly name: string }>(rows: readonly A[]) =>
	new Map(Arr.map(rows, (row) => [row.name, row]));

export class OwnTables extends Context.Service<
	OwnTables,
	{
		/** The tracked spots, `tables/spots.tsv`. */
		readonly spots: readonly Spot[];
		/** Every chest check of `remake/chests.tsv`, by its spoiler log name, once, in the table's order. */
		readonly chests: readonly string[];
		readonly bases: ReadonlyMap<string, Base>;
		readonly tiers: readonly (readonly string[])[];
		readonly checks: ReadonlyMap<string, Check>;
	}
>()("game-testing/save/tables/OwnTables") {
	static readonly layer = Layer.effect(
		OwnTables,
		Effect.gen(function* () {
			const fs = yield* FileSystem.FileSystem;
			const spots = yield* fs
				.readFileString(`${tables}/spots.tsv`)
				.pipe(Effect.flatMap(parseSpots), Effect.orDie);
			const bases = yield* readTable(`${tables}/bases.tsv`, BaseRow);
			const items = yield* readTable(`${remake}/items.tsv`, ItemRow);
			const placed = yield* Effect.forEach(
				["chests", "loose_items", "skulltulas"] as const,
				(table) => readTable(`${remake}/${table}.tsv`, PlacedRow),
				{ concurrency: 1 }
			);
			const flows = yield* Effect.forEach(
				["gifts", "rewards"],
				(table) => readTable(`${remake}/${table}.tsv`, FlowRow),
				{ concurrency: 1 }
			);
			const shops = yield* readTable(`${remake}/shops.tsv`, ShopRow);
			const checks = new Map<string, Check>();
			const add = (name: string, part: Partial<Check>) => {
				if (name === "") {
					return;
				}
				const check = checks.get(name) ?? {
					name,
					placements: [],
					flows: [],
					stock: [],
				};
				checks.set(name, {
					name,
					placements: [...check.placements, ...(part.placements ?? [])],
					flows: [...check.flows, ...(part.flows ?? [])],
					stock: [...check.stock, ...(part.stock ?? [])],
				});
			};
			for (const row of Arr.flatten(placed)) {
				add(row.location, { placements: row.hash === "" ? [] : [row.hash] });
			}
			for (const row of Arr.flatten(flows)) {
				add(row.location, { flows: [stem(row.flow)] });
			}
			for (const row of shops) {
				add(row.location, {
					stock: Arr.map(Str.split(row.shopkeepers, ","), (keeper) => ({
						keeper: Str.split(keeper, ":")[0] as string,
						slot: row.slot,
					})),
				});
			}
			const groups = Arr.groupBy(items, (row) => row.soh_item);
			return OwnTables.of({
				spots,
				chests: Arr.dedupe(
					Arr.filter(
						Arr.map(placed[0], (row) => row.location),
						Str.isNonEmpty
					)
				),
				bases: byName(bases),
				tiers: Arr.flatMapNullishOr(Rec.values(groups), (group) =>
					group.length > 1
						? Arr.map(group, (row) => row.remake_item)
						: undefined
				),
				checks,
			});
		})
	);
}

const byLength = Order.combine(
	Order.mapInput(Order.Number, (match: NameMatch) => match.name.length),
	Order.mapInput(Order.String, (match: NameMatch) => match.name)
);

/**
 * Every name a state or a body takes whose name or line holds `text`, shortest first: the game's own, and the checks,
 * spots and bases of Kaepora's tables.
 */
export const searchNames = Effect.fn("searchNames")(function* (
	data: GameData["Service"],
	own: OwnTables["Service"],
	spots: readonly Spot[],
	text: string,
	options: { readonly kind?: NameKind | undefined; readonly limit: number }
) {
	const fromGame = yield* data.search(text, {
		limit: options.limit,
		...(options.kind === undefined ? {} : { kind: options.kind }),
	});
	const wanted = Str.toLowerCase(text);
	const ours: readonly NameMatch[] = [
		...Arr.map([...own.checks.keys()], (name) => ({
			kind: "check",
			name,
			about: "a check by its spoiler log name",
		})),
		...Arr.map(spots, (spot) => ({
			kind: "spot",
			name: spot.name,
			about: spot.scene,
		})),
		...Arr.map([...own.bases.values()], (base) => ({
			kind: "base",
			name: base.name,
			about: base.about,
		})),
	];
	const found = Arr.filter(
		ours,
		(match) =>
			(options.kind === undefined || match.kind === options.kind) &&
			Arr.some([match.name, match.about], (part) =>
				Str.includes(wanted)(Str.toLowerCase(part))
			)
	);
	return Arr.take(Arr.sort([...fromGame, ...found], byLength), options.limit);
});
