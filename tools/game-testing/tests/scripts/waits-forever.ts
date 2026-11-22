/** Waits for a chest that never opens, `IsTreasureBoxOpen.6006` by its hash, past its own limit. */
import { script } from "../../src/script.ts";

export default script({
	about: "A script past its limit is stopped.",
	limit: "4 seconds",
	state: { base: "test-base" },
	run: (game) => game.wait("0x46dd3676", true, { within: "10 minutes" }),
});
