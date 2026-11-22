/** Opens the chest Link stands before, and checks one it did not open, `IsTreasureBoxOpen.6006` by its hash. */
import { Effect } from "effect";
import { script } from "../../src/script.ts";

export default script({
	about: "A check of a chest that stays shut fails the run.",
	state: { base: "test-base" },
	run: (game) =>
		Effect.gen(function* () {
			yield* game.press("A");
			return yield* game.check("0x46dd3676", true);
		}),
});
