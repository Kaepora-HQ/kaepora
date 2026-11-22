/** Kills its own process once its game has started. */
import { Effect } from "effect";
import { script } from "../../src/script.ts";

export default script({
	about: "A script whose process dies leaves its place.",
	state: { base: "test-base" },
	run: () =>
		Effect.sync(() => {
			process.kill(process.pid, "SIGKILL");
		}),
});
