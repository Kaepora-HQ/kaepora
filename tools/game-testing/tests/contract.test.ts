import { describe, expect, it } from "@voila.dev/effect-bun-test";
import { Effect, Exit, Schema } from "effect";
import { Instant } from "../src/contract/record.ts";
import { Buttons, Frames } from "../src/contract/values.ts";

describe("the contracts", () => {
	it("refuses L or R held with Plus and Minus, Lichii's stop and restart hotkeys", () => {
		const decode = Schema.decodeUnknownExit(Buttons);
		expect(Exit.isFailure(decode(["L", "Plus", "Minus"]))).toBe(true);
		expect(Exit.isFailure(decode(["A", "R", "Minus", "Plus"]))).toBe(true);
		expect(Exit.isSuccess(decode(["Plus", "Minus"]))).toBe(true);
		expect(Exit.isSuccess(decode(["L", "Plus"]))).toBe(true);
	});

	it("Buttons.make throws on the hotkey chord", () => {
		expect(() => Buttons.make(["R", "Plus", "Minus"])).toThrow();
		expect([...Buttons.make(["A"])]).toEqual(["A"]);
	});

	it("refuses an input longer than the server's limit holds at 15 frames a second", () => {
		const decode = Schema.decodeUnknownExit(Frames);
		expect(Exit.isSuccess(decode(13_350))).toBe(true);
		expect(Exit.isFailure(decode(13_351))).toBe(true);
	});

	it.effect(
		"a run record's time that is not an instant fails as a schema error",
		() =>
			Effect.gen(function* () {
				const error = yield* Effect.flip(
					Schema.decodeEffect(Instant)("Tuesday at noon")
				);
				expect(error._tag).toBe("SchemaError");
			})
	);
});
