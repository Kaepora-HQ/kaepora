/** Opens the chest Link stands before, `IsTreasureBoxOpen.5005` by its hash, and checks that it opened. */
import { Effect } from "effect";
import { script } from "../../src/script.ts";

export default script({
	about: "The chest Link stands before opens to A.",
	state: { base: "test-base" },
	run: (game) =>
		Effect.gen(function* () {
			yield* game.press("A");
			const opened = yield* game.check("0x1d2a8fff", true);
			return { opened, shot: yield* game.screenshot("after-chest") };
		}),
});
