/**
 * The child's side of a script run. It imports the script, says what the script declares, starts its game through
 * `Game`, says when Link can move, runs the body, ends the run with how the body ended, and says the run's outcome.
 * Each of those is one line of its output, for the runner.
 */
import {
	Config,
	Console,
	Context,
	Effect,
	FileSystem,
	Layer,
	Path,
	Schema,
	String as Str,
} from "effect";
import type { Outcome } from "../contract/record.ts";
import type { RunId } from "../contract/values.ts";
import { Game } from "../game.ts";
import type { Declared } from "../script.ts";
import { load } from "../script.ts";
import type { ScriptEvent } from "./runner.ts";
import { EventLine, inlineFolder, packageRoot } from "./runner.ts";

const say = (event: ScriptEvent) =>
	Effect.flatMap(Schema.encodeEffect(EventLine)(event), (line) =>
		Console.log(line)
	);

const declaring = ({ run: _, ...declaration }: Declared): ScriptEvent => ({
	_tag: "declared",
	declaration,
});

/** Says what a script declares, and nothing more. */
export const declare = Effect.fn("Scripts.declare")(function* (script: string) {
	const declared = yield* load(script);
	yield* say(declaring(declared));
});

export const work = Effect.fn("Scripts.work")(function* (options: {
	/** Its host: the socket's path of one on this machine, or the URL of a far machine's `serve`, whose token the environment holds. */
	readonly host: string;
	readonly script: string;
	readonly run: RunId;
	readonly attemptOf: RunId | undefined;
}) {
	const fs = yield* FileSystem.FileSystem;
	const path = yield* Path.Path;
	const declared = yield* load(options.script);
	yield* say(declaring(declared));
	if (declared.person === "here") {
		return yield* say({
			_tag: "ended",
			outcome: {
				_tag: "skipped",
				reason: "it hands over to a person, and run has nobody to hand over to",
			},
		});
	}
	const source = yield* Effect.orDie(fs.readFileString(options.script));
	const digest = new Bun.CryptoHasher("sha256").update(source).digest("hex");
	const kept = path.relative(packageRoot, options.script);
	const inline = path.dirname(path.resolve(options.script)) === inlineFolder;
	const outcome = yield* Effect.scoped(
		Effect.gen(function* () {
			const token = yield* Config.option(Config.Redacted("GAME_TESTING_TOKEN"));
			const client = yield* Layer.build(
				Str.startsWith("/")(options.host)
					? Game.layer(options.host)
					: Game.layerRemote(options.host, token)
			);
			const game = Context.get(client, Game);
			const running = yield* game.start({
				run: options.run,
				attemptOf: options.attemptOf,
				owner: inline ? "run_script" : kept,
				state: declared.state,
				mod: declared.mod,
				script: inline ? { digest, source } : { path: kept, digest },
			});
			yield* say({ _tag: "started" });
			const ended = yield* Effect.exit(declared.run(running));
			return yield* game.finish(running, ended);
		})
	).pipe(
		Effect.catch((error) =>
			Effect.succeed<Outcome>({
				_tag: "broken",
				reason: "the run could not start or finish",
				error: error.message,
			})
		)
	);
	yield* say({ _tag: "ended", outcome });
});
