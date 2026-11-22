/** Presses A and checks nothing. */
import { script } from "../../src/script.ts";

export default script({
	about: "A script that proves nothing.",
	state: { base: "test-base" },
	run: (game) => game.press("A"),
});
