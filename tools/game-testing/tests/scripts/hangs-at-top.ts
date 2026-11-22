/** A script whose module never finishes loading. */
import { Effect } from "effect";
import { script } from "../../src/script.ts";

await Effect.runPromise(Effect.never);

export default script({
	about: "A script that never declares itself is stopped.",
	state: { base: "test-base" },
	run: () => Effect.void,
});
