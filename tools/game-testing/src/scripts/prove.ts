/**
 * The look that proves a chest's spot: Link starts on it with the chest put back closed, A is pressed until one of the
 * chest's actors records it open, and the check proves it opened. Each actor of a chest keeps its own record.
 */
import { Array as Arr, Formatter } from "effect";
import { packageRoot } from "./runner.ts";

/** The most presses one chest takes: a page of the item's text closes with each, and a large chest's item has more. */
const presses = 15;

/**
 * Presses A until the chest's record holds open. A check with two chests, one each side of a scene's seam, presses until
 * either does, as Link stands before one of them.
 */
const opening = (
	flags: readonly string[],
	text: (value: unknown) => string
) => {
	const [only, ...more] = flags;
	if (only !== undefined && more.length === 0) {
		return `yield* game.pressUntil("A", ${text(only)}, true, { times: ${presses} });
			const which = ${text(only)};`;
	}
	return `const flags = ${text(flags)} as const;
			const open = Effect.gen(function* () {
				for (const flag of flags) {
					if ((yield* game.read(flag)) === true) {
						return flag;
					}
				}
				return undefined;
			});
			for (let pressed = 0; pressed < ${presses} && (yield* open) === undefined; pressed += 1) {
				yield* game.press("A");
				yield* game.idle(60);
			}
			const which = (yield* open) ?? flags[0];`;
};

export const proveScript = (options: {
	readonly check: string;
	readonly base: string;
	readonly hashes: readonly string[];
}) => {
	const text = (value: unknown) => Formatter.formatJson(value);
	const flags = Arr.map(options.hashes, (hash) => `IsTreasureBoxOpen.${hash}`);
	return `import { Effect, script } from ${text(`${packageRoot}/src/script.ts`)};

export default script({
	about: ${text(`${options.check} opens to A from its spot.`)},
	state: {
		base: ${text(options.base)},
		location: ${text(options.check)},
		reset: [${text(options.check)}],
	},
	run: (game) =>
		Effect.gen(function* () {
			const start = yield* game.screenshot("start");
			${opening(flags, text)}
			const opened = yield* game.check(which, true);
			return { opened, start, after: yield* game.screenshot("after") };
		}),
});
`;
};
