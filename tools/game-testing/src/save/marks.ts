/**
 * A look's outcome on the spot it started on, kept as a mark apart from the spot: a run that passed proves the spot and
 * one that failed says a look from it failed. The mark holds while the spot stands where the look started, so git
 * still decides a tracked spot.
 */
import { Effect } from "effect";
import type { RunRecord } from "../contract/record.ts";
import type { Mark } from "../contract/spots.ts";
import { samePlace } from "../contract/spots.ts";
import type { GameData } from "../data/game-data.ts";
import { SpotRefused } from "../errors.ts";
import type { Spots } from "./spots.ts";
import { spotAge } from "./spots.ts";
import { spotOf } from "./state.ts";
import type { OwnTables } from "./tables.ts";

export const markSpot = Effect.fn("markSpot")(function* (
	data: GameData["Service"],
	own: OwnTables["Service"],
	spots: Spots["Service"],
	run: RunRecord
) {
	const name = typeof run.state.location === "string" ? run.state.location : "";
	const refuse = (detail: string) =>
		SpotRefused.make({ spot: name === "" ? `run ${run.id}` : name, detail });
	const used = run.plan?.location;
	if (
		name === "" ||
		used === undefined ||
		used.kind === "given" ||
		used.kind === "landing"
	) {
		return yield* refuse(`run ${run.id} did not start on a spot by name.`);
	}
	if (run.outcome._tag !== "passed" && run.outcome._tag !== "failed") {
		return yield* refuse(
			`run ${run.id} ended ${run.outcome._tag}, which says nothing about where it started.`
		);
	}
	const age = spotAge(used.scene) === "adult" ? "adult" : "child";
	const now = yield* spotOf(data, own, spots, name, age).pipe(
		Effect.catchTags({
			StateRefused: (refused) => refuse(refused.detail),
			UnknownName: (unknown) => refuse(unknown.message),
		})
	);
	if (!samePlace(now, used)) {
		return yield* refuse(
			`its spot moved since run ${run.id}, which started on another place.`
		);
	}
	const mark: Mark = {
		name,
		scene: used.scene,
		...(used.room === undefined ? {} : { room: used.room }),
		position: used.position,
		facing: used.facing,
		look: { outcome: run.outcome._tag, run: run.id },
	};
	yield* spots.mark(mark);
	return mark;
});
