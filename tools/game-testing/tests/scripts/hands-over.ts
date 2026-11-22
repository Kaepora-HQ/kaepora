/** Asks the person at the machine to open the chest. */
import { script } from "../../src/script.ts";

export default script({
	about: "A person opens the chest.",
	person: "here",
	state: { base: "test-base" },
	run: (game) => game.handOver("Open the chest before Link."),
});
