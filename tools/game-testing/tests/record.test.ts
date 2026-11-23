import { describe, expect, it } from "@voila.dev/effect-bun-test";
import { Array as Arr, Effect, Queue, Ref, String as Str } from "effect";
import type { PlayedSpot, Spot } from "../src/contract/spots.ts";
import type { Keep } from "../src/emulator/recorder.ts";
import { recordHere, watchChests } from "../src/emulator/recorder.ts";
import { RemoteUnavailable } from "../src/errors.ts";
import type { FakeLichii, FakeValue } from "./fake-lichii.ts";
import { fakeLichii } from "./fake-lichii.ts";

const closed: FakeValue = { type: "Bool", value: false };

/** Link in the Forest Temple's room 17, facing +X, with four chests' actors closed. */
const game: Readonly<Record<string, FakeValue>> = {
	Sequence_CurrentBanc: { type: "String64", value: "ForestTemple" },
	DungeonRoom: { type: "Int", value: 17 },
	"PlayerStatus.CurrentPos": { type: "Vector3", value: [5, 8, 6] },
	"PlayerStatus.CurrentDir": { type: "Vector3", value: [1, 0, 0] },
	"IsTreasureBoxOpen.5005": closed,
	"IsTreasureBoxOpen.2002": closed,
	"IsTreasureBoxOpen.3003": closed,
	"IsTreasureBoxOpen.6006": closed,
	"IsTreasureBoxOpen.7007": closed,
};

const chests = [
	{ name: "Fake Bow Chest", hashes: ["5005"] },
	{ name: "Fake Pot Chest", hashes: ["2002"] },
	{ name: "Fake Far Chest", hashes: ["3003"] },
	// One check, two chests: the one Link opens is the one its spot stands before.
	{ name: "Fake Grotto Chest", hashes: ["7007", "6006"] },
	// A trap chest keeps a per-actor key, and no IsTreasureBoxOpen value.
	{ name: "Fake Trap Chest", hashes: ["4004"] },
];

/**
 * A keeper that keeps every spot sent, as the machine with the game would, after `unanswered` sends find it gone. Each
 * spot kept also goes on `kept`, which a test takes from to wait for the next.
 */
const keeper = (unanswered = 0) =>
	Effect.gen(function* () {
		const tries = yield* Ref.make(0);
		const kept = yield* Queue.unbounded<PlayedSpot>();
		const keep = (played: PlayedSpot) =>
			Effect.gen(function* () {
				if ((yield* Ref.getAndUpdate(tries, (n) => n + 1)) < unanswered) {
					return yield* RemoteUnavailable.make({
						url: "http://127.0.0.1:7480",
						reason: "closed",
					});
				}
				yield* Queue.offer(kept, played);
				const spot: Spot = {
					name: played.name,
					scene: played.scene,
					position: played.position,
					facing: played.facing,
					found: "play",
					evidence: "a running game",
					note: "",
				};
				return spot;
			});
		return { tries, kept, keep };
	});

/**
 * Starts the watcher on the live clock, and gives what it says as it says it. It has taken its first look at the
 * chests once it says it watches, so a chest opened after that is new to it.
 */
const watching = (lichii: FakeLichii, keep: Keep<RemoteUnavailable>) =>
	Effect.gen(function* () {
		const said = yield* Queue.unbounded<string>();
		yield* Effect.forkScoped(
			watchChests({ path: lichii.path }, chests, keep, (line) =>
				Effect.asVoid(Queue.offer(said, line))
			)
		);
		yield* heard(said, "watching ");
		return said;
	});

/** The next line the watcher says that starts with `start`, after the ones it says before it. */
const heard = (
	said: Queue.Queue<string>,
	start: string
): Effect.Effect<string> =>
	Queue.take(said).pipe(
		Effect.filterOrElse(Str.startsWith(start), () => heard(said, start))
	);

describe("the recorder", () => {
	it.effect(
		"record keeps where Link stands now under a name, and the way he faces, through a socket or a loopback port",
		() =>
			Effect.gen(function* () {
				const lichii = yield* fakeLichii({ values: game });
				const { kept, keep } = yield* keeper();
				yield* recordHere({ path: lichii.path }, "Fake Spot", keep);
				yield* recordHere(
					{ host: "127.0.0.1", port: lichii.port },
					"Fake New Spot",
					keep,
					true
				);
				const here = {
					scene: "ForestTemple",
					room: 17,
					position: [5, 8, 6] as const,
					facing: 90,
				};
				expect(yield* Queue.takeAll(kept)).toEqual([
					{ name: "Fake Spot", ...here },
					{ name: "Fake New Spot", ...here, new: true },
				]);
			})
	);

	it.live(
		"the watcher sends the chest Link opened, nothing for a load that opens several at once, and only reads",
		() =>
			Effect.gen(function* () {
				const lichii = yield* fakeLichii({ values: game });
				const { kept, keep } = yield* keeper();
				const said = yield* watching(lichii, keep);
				yield* lichii.put("IsTreasureBoxOpen.6006", true);
				const opened = yield* Queue.take(kept);
				// Both in one step, as a load writes them before the watcher reads again.
				yield* Effect.uninterruptible(
					Effect.andThen(
						lichii.put("IsTreasureBoxOpen.2002", true),
						lichii.put("IsTreasureBoxOpen.3003", true)
					)
				);
				yield* heard(said, "2 chests opened at once");
				yield* lichii.put("IsTreasureBoxOpen.5005", true);
				const next = yield* Queue.take(kept);
				const methods = Arr.dedupe(
					Arr.map(yield* lichii.requests, (request) => request.method)
				);
				expect(methods).toEqual(["search_memory", "read_memory"]);
				const here = {
					scene: "ForestTemple",
					room: 17,
					position: [5, 8, 6] as const,
					facing: 90,
				};
				expect([opened, next]).toEqual([
					{ name: "Fake Grotto Chest", ...here, chest: "6006" },
					{ name: "Fake Bow Chest", ...here, chest: "5005" },
				]);
			}),
		{ timeout: 20_000 }
	);

	it.live(
		"the watcher holds a spot the keeping machine cannot take, and sends it again until it is kept",
		() =>
			Effect.gen(function* () {
				const lichii = yield* fakeLichii({ values: game });
				const { tries, kept, keep } = yield* keeper(1);
				const said = yield* watching(lichii, keep);
				yield* lichii.put("IsTreasureBoxOpen.5005", true);
				yield* heard(said, "held Fake Bow Chest");
				const sent = yield* Queue.take(kept);
				expect(sent.name).toBe("Fake Bow Chest");
				expect(yield* Ref.get(tries)).toBe(2);
			}),
		{ timeout: 20_000 }
	);
});
