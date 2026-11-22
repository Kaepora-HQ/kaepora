/**
 * - `tables/spots.tsv`: spots where Link stands on the floor before a subject, each from a save the game wrote
 *   there, where standing him before the subject's actor puts him in a wall or a ceiling.
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
	Record as Rec,
	Schema,
	String as Str,
} from "effect";
import { Age } from "../contract/state.ts";
import { stem } from "../data/dump.ts";
import type { Point } from "../data/game-data.ts";

export interface Spot {
	/** Degrees: 0 faces the scene's +Z, 90 its +X. */
	readonly facing: number;
	readonly name: string;
	readonly position: Point;
	readonly room: number | undefined;
	readonly scene: string;
}

export interface Base {
	readonly about: string;
	readonly age: Age;
	/** The SHA-256 of its caption.sav. */
	readonly caption: string;
	readonly hour: "day" | "night";
	readonly name: string;
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
	name: Text,
	scene: Text,
	room: Text,
	x: Finite,
	y: Finite,
	z: Finite,
	facing: Finite,
});
const BaseRow = Schema.Struct({
	name: Text,
	hour: Schema.Literals(["day", "night"]),
	age: Age,
	progress: Text,
	caption: Text,
	about: Text,
});
const ItemRow = Schema.Struct({ remake_item: Text, soh_item: Text });
const PlacedRow = Schema.Struct({ location: Text, hash: Text });
const FlowRow = Schema.Struct({ location: Text, flow: Text });
const ShopRow = Schema.Struct({ location: Text, slot: Int, shopkeepers: Text });

const readTable = Effect.fn("OwnTables.read")(function* <
	S extends Schema.Top & { readonly DecodingServices: never },
>(path: string, row: S) {
	const fs = yield* FileSystem.FileSystem;
	const text = yield* fs.readFileString(path).pipe(Effect.orDie);
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
	return yield* Schema.decodeUnknownEffect(Schema.Array(row))(rows).pipe(
		Effect.orDie
	);
});

const byName = <A extends { readonly name: string }>(rows: readonly A[]) =>
	new Map(Arr.map(rows, (row) => [row.name, row]));

export class OwnTables extends Context.Service<
	OwnTables,
	{
		readonly spots: ReadonlyMap<string, Spot>;
		readonly bases: ReadonlyMap<string, Base>;
		readonly tiers: readonly (readonly string[])[];
		readonly checks: ReadonlyMap<string, Check>;
	}
>()("game-testing/save/tables/OwnTables") {
	static readonly layer = Layer.effect(
		OwnTables,
		Effect.gen(function* () {
			const spots = yield* readTable(`${tables}/spots.tsv`, SpotRow);
			const bases = yield* readTable(`${tables}/bases.tsv`, BaseRow);
			const items = yield* readTable(`${remake}/items.tsv`, ItemRow);
			const placed = yield* Effect.forEach(
				["chests", "loose_items", "skulltulas"],
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
				spots: byName(
					Arr.map(spots, (row) => ({
						name: row.name,
						scene: row.scene,
						room: row.room === "" ? undefined : Number(row.room),
						position: [row.x, row.y, row.z] as const,
						facing: row.facing,
					}))
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
