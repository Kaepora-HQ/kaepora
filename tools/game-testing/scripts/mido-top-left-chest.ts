/** The top left chest in Mido's house opens to A for a Link who stands before it. */
import { Effect } from "effect";
import { script } from "../src/script.ts";

const chest = "IsTreasureBoxOpen.12786193094758345086";

export default script({
	about:
		"The top left chest in Mido's house opens to A for a Link who stands before it.",
	state: {
		base: "new-game-autosave",
		location: "KF Mido Top Left Chest",
		reset: ["KF Mido Top Left Chest"],
	},
	run: (game) =>
		Effect.gen(function* () {
			yield* game.press("A");
			// The game writes the chest open once the last page of the item's text closes, so A goes on until then.
			let presses = 1;
			while ((yield* game.read(chest)) !== true && presses < 10) {
				yield* game.idle(60);
				yield* game.press("A");
				presses += 1;
			}
			const opened = yield* game.check(chest, true);
			return { opened, shot: yield* game.screenshot("after-chest") };
		}),
});
