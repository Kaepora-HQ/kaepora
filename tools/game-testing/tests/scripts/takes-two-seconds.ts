/** Waits two seconds with Link in control, then opens the chest before him, `IsTreasureBoxOpen.5005` by its hash. */
import { Effect } from "effect";
import { script } from "../../src/script.ts";

export default script({
	about: "A script with a limit of three seconds, which its two seconds fit.",
	limit: "3 seconds",
	state: { base: "test-base" },
	run: (game) =>
		Effect.gen(function* () {
			yield* Effect.sleep("2 seconds");
			yield* game.press("A");
			return yield* game.check("0x1d2a8fff", true);
		}),
});
