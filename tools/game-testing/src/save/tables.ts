/**
 * - `tables/spots.tsv`: where Link starts for a location by name, a check or a spot of its own, with how each was found
 *   and the look that started there. `Spots` reads it with the recorded table beside it.
 * - `tables/bases.tsv`: the base saves, whose files stay on machines with the game. `Bases` reads it with the bases
 *   recorded on this machine beside it.
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
import type { Base } from "../contract/bases.ts";
import { About, BaseName, Hour } from "../contract/bases.ts";
import type { NameMatch } from "../contract/rpc.ts";
import type { Spot } from "../contract/spots.ts";
import { Found, Look, SpotName } from "../contract/spots.ts";
import { Age } from "../contract/state.ts";
import type { NameKind } from "../contract/values.ts";
import { RunId } from "../contract/values.ts";
import { stem } from "../data/dump.ts";
import type { GameData } from "../data/game-data.ts";

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

const tables = Bun.fileURLToPath(new URL("../../tables", import.meta.url));
/** This package's tracked spots, which a commit changes. */
export const trackedSpots = `${tables}/spots.tsv`;
/** This package's tracked bases, which a commit changes. */
export const trackedBases = `${tables}/bases.tsv`;
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
/** A base's row: `run` and `save` name the run's save it was named from, or are both empty. */
const BaseRow = Schema.Struct({
	name: BaseName,
	hour: Hour,
	age: Age,
	progress: Text,
	caption: Text,
	option: Text,
	about: About,
	run: Schema.Union([Schema.Literal(""), RunId]),
	save: Schema.Union([Schema.Literal(""), Int]),
}).check(
	Schema.makeFilter((row) =>
		(row.run === "") === (row.save === "")
			? undefined
			: "a run and its save, or neither"
	)
);

/** A base as a line of `tables/bases.tsv`, its cells in the header's order. */
export const baseLine = (base: Base) =>
	Arr.join(
		[
			base.name,
			base.hour,
			base.age,
			base.progress,
			base.caption,
			base.option,
			base.from?.run ?? "",
			base.from === undefined ? "" : String(base.from.save),
			base.about,
		],
		"\t"
	);
const ItemRow = Schema.Struct({ remake_item: Text, soh_item: Text });
const PlacedRow = Schema.Struct({ location: Text, hash: Text });
const FlowRow = Schema.Struct({ location: Text, flow: Text });
const ShopRow = Schema.Struct({ location: Text, slot: Int, shopkeepers: Text });

/**
 * A table's rows: a header of column names, then one line a row, cells split by tabs. A commit writes these tables by
 * hand, so a row that is wrong stops the tool with a sentence naming the file and the line.
 */
const readTable = Effect.fn("OwnTables.read")(function* <
	S extends Schema.Top & { readonly DecodingServices: never },
>(path: string, row: S) {
	const fs = yield* FileSystem.FileSystem;
	const text = yield* fs.readFileString(path).pipe(Effect.orDie);
	const lines = Arr.filter(
		Arr.map(Str.split(text, "\n"), (line, index) => ({
			line,
			number: index + 1,
		})),
		({ line }) => Str.isNonEmpty(line)
	);
	const [header, ...rows] = lines;
	const columns = Str.split(header?.line ?? "", "\t");
	return yield* Effect.forEach(
		rows,
		({ line, number }) => {
			const cells = Str.split(line, "\t");
			return Schema.decodeEffect(row)(
				Rec.fromEntries(
					Arr.map(columns, (column, i) => [column, cells[i] ?? ""])
				)
			).pipe(
				Effect.catch((issue) =>
					Effect.die(new Error(`${path} line ${number}: ${issue.message}`))
				)
			);
		},
		{ concurrency: 1 }
	);
});

/** The spots of a spots table. A look is `passed <run>` or `failed <run>`. */
export const readSpots = Effect.fnUntraced(function* (path: string) {
	const rows = yield* readTable(path, SpotRow);
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

/** The text of a spots table: its header, then one line a spot, as `readSpots` reads it back. */
export const spotsText = (spots: readonly Spot[]) =>
	Arr.join(
		[
			Arr.join(Rec.keys(SpotRow.fields), "\t"),
			...Arr.map(spots, (spot) =>
				Arr.join(
					[
						spot.name,
						spot.scene,
						spot.room === undefined ? "" : String(spot.room),
						...Arr.map(spot.position, (axis) => axis.toFixed(3)),
						spot.facing.toFixed(1),
						spot.found,
						spot.evidence,
						spot.look === undefined
							? ""
							: `${spot.look.outcome} ${spot.look.run}`,
						spot.note,
					],
					"\t"
				)
			),
			"",
		],
		"\n"
	);

const byName = <A extends { readonly name: string }>(rows: readonly A[]) =>
	new Map(Arr.map(rows, (row) => [row.name, row]));

export class OwnTables extends Context.Service<
	OwnTables,
	{
		/** The tracked spots, `tables/spots.tsv`. */
		readonly spots: readonly Spot[];
		/** Every chest check of `remake/chests.tsv`, by its spoiler log name, once, in the table's order. */
		readonly chests: readonly string[];
		/** The tracked bases, `tables/bases.tsv`. */
		readonly bases: ReadonlyMap<string, Base>;
		readonly tiers: readonly (readonly string[])[];
		readonly checks: ReadonlyMap<string, Check>;
	}
>()("game-testing/save/tables/OwnTables") {
	static readonly layer = Layer.effect(
		OwnTables,
		Effect.gen(function* () {
			const spots = yield* readSpots(trackedSpots);
			const rows = yield* readTable(trackedBases, BaseRow);
			const bases = byName(
				Arr.map(
					rows,
					({ run, save, ...row }): Base =>
						run === "" || save === "" ? row : { ...row, from: { run, save } }
				)
			);
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
				bases,
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
	kept: { readonly spots: readonly Spot[]; readonly bases: readonly Base[] },
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
		...Arr.map(kept.spots, (spot) => ({
			kind: "spot",
			name: spot.name,
			about: spot.scene,
		})),
		...Arr.map(kept.bases, (base) => ({
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
