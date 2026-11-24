/**
 * The command line. `run` is a host: it serves the scripts it starts as children. The other commands read the game's
 * tables, the places or the records on this machine, and each builds only what it reads, on the platform and our own
 * tables, which the entry point gives.
 */
import {
	Array as Arr,
	Config,
	Console,
	Context,
	Duration,
	Effect,
	FileSystem,
	Formatter,
	Layer,
	Option,
	Path,
	Record as Rec,
	Result,
	Schema,
	String as Str,
} from "effect";
import { McpServer } from "effect/ai";
import { Argument, CliError, Command, Flag } from "effect/cli";
import { UserDirectories } from "effect/persistence";
import type { Catalog } from "./contract/bases.ts";
import { Hour, keptText, NewBase } from "./contract/bases.ts";
import type { Origin, PlannedEdit, StatePlan } from "./contract/plan.ts";
import { baseText, originText } from "./contract/plan.ts";
import type { Outcome } from "./contract/record.ts";
import type { ChestSpots, StartSpot } from "./contract/spots.ts";
import { SpotName } from "./contract/spots.ts";
import type { Location } from "./contract/state.ts";
import { StartState } from "./contract/state.ts";
import { NameKind, RunId } from "./contract/values.ts";
import { namesHere, writeNames } from "./data/declarations.ts";
import { GameData } from "./data/game-data.ts";
import type { DebugServer } from "./emulator/emulator.ts";
import type { Keep } from "./emulator/recorder.ts";
import { recordHere, watchChests } from "./emulator/recorder.ts";
import { TokenRequired } from "./errors.ts";
import { farUrl, token, tokenFile } from "./far.ts";
import { bases as basesHere, host, tables } from "./host/host.ts";
import { Places } from "./host/places.ts";
import { Runs } from "./host/runs.ts";
import { Machine } from "./machine.ts";
import {
	instructions,
	protocols,
	registrations,
	serverName,
} from "./mcp/server.ts";
import { readMod, scriptMod } from "./mod.ts";
import { Bases } from "./save/bases.ts";
import { markSpot } from "./save/marks.ts";
import { chestSpots, intoTracked, keepPlayed, Spots } from "./save/spots.ts";
import { StartStates } from "./save/state.ts";
import {
	baseLine,
	OwnTables,
	readSpots,
	searchNames,
	spotsText,
	trackedBases,
	trackedSpots,
} from "./save/tables.ts";
import { proveScript } from "./scripts/prove.ts";
import type { ScriptRun } from "./scripts/runner.ts";
import {
	declarationCommand,
	declarationOf,
	Scripts,
	workerCommand,
} from "./scripts/runner.ts";
import { declare, work } from "./scripts/worker.ts";
import { serve as serveOn } from "./serve/server.ts";

const farMachine = (url: string) =>
	Machine.layerRemote(url).pipe(Layer.provide(Runs.layer));

/**
 * Runs `here` on this machine, or, with `GAME_TESTING_URL` set, `far` against that machine's `serve`, whose
 * connection closes when it ends.
 */
const hereOrFar = <A, E, R, B, E2, R2>(
	here: Effect.Effect<A, E, R>,
	far: (machine: Machine["Service"]) => Effect.Effect<B, E2, R2>
) =>
	Effect.flatMap(
		farUrl,
		Option.match({
			onNone: (): Effect.Effect<
				A | B,
				E | E2 | Layer.Error<ReturnType<typeof farMachine>>,
				R | Exclude<R2, Machine> | Layer.Services<ReturnType<typeof farMachine>>
			> => here,
			onSome: (url) => Machine.use(far).pipe(Effect.provide(farMachine(url))),
		})
	);

/** The package's version, which `mcp` and `serve` name to their clients. */
export const version = "0.0.0";

/** A handler's failure as the command line shows it: its message. */
const shown = <A, E extends { readonly message: string }, R>(
	effect: Effect.Effect<A, E, R>
) =>
	Effect.mapError(effect, (error) =>
		CliError.UserError.make({ cause: error, userMessage: error.message })
	);

/**
 * Scripts, their machine and this machine's runs: `here`, or with `GAME_TESTING_URL` the far machine's, whose
 * connection is made at the first call.
 */
const scriptsOn = <E, R>(here: Layer.Layer<Scripts | Machine | Runs, E, R>) =>
	Layer.unwrap(
		Effect.map(
			farUrl,
			(
				url
			): Layer.Layer<
				Scripts | Machine | Runs,
				E | Layer.Error<ReturnType<typeof Scripts.layerRemote>>,
				R | Layer.Services<ReturnType<typeof Scripts.layerRemote>>
			> => (Option.isSome(url) ? Scripts.layerRemote(url.value) : here)
		)
	);

const scriptsHere = Scripts.layer.pipe(Layer.provideMerge(host));

const ended = (outcome: Outcome) =>
	outcome._tag === "passed" || outcome._tag === "skipped";

const why = (outcome: Outcome) => {
	switch (outcome._tag) {
		case "failed":
		case "skipped":
		case "stopped":
			return [outcome.reason];
		case "broken":
			return Arr.filter([outcome.reason, outcome.error], Str.isNonEmpty);
		case "unchecked":
			return ["it ended with no check, so it proved nothing"];
		default:
			return [];
	}
};

/** A run's outcome, why, and where its record and pictures are on this machine. */
const report = (
	script: string,
	ran: ScriptRun,
	folder: (run: RunId) => string
) =>
	Effect.forEach(
		[
			Arr.join(
				Arr.filter([ran.outcome._tag, script, ran.run ?? ""], Str.isNonEmpty),
				"  "
			),
			...Arr.map(why(ran.outcome), (line) => `    ${line}`),
			...(ran.run === undefined
				? []
				: [
						`    ${ran.notCopied ?? `record and pictures: ${folder(ran.run)}`}`,
					]),
		],
		(line) => Console.log(line),
		{ concurrency: 1, discard: true }
	);

/** One run of a script, reported as `label`: a script that fails before its game ends is a broken run. */
const attemptRun = (
	runner: Scripts["Service"],
	records: Runs["Service"],
	script: string,
	label: string,
	attemptOf?: RunId
) =>
	runner.run(script, { attemptOf }).pipe(
		Effect.catch(
			(failed): Effect.Effect<ScriptRun> =>
				Effect.succeed(
					failed._tag === "ScriptFailed"
						? {
								outcome: {
									_tag: "broken",
									reason: `the script failed (${failed.reason})`,
									error: failed.detail,
								},
								...(failed.run === undefined ? {} : { run: failed.run }),
							}
						: {
								outcome: {
									_tag: "broken",
									reason: "the game machine could not be asked",
									error: failed.message,
								},
							}
				)
		),
		Effect.tap((ran) => report(label, ran, records.folder))
	);

const run = Command.make(
	"run",
	{
		scripts: Argument.File("script", { mustExist: true }).pipe(
			Argument.variadic({ min: 1 })
		),
		parallel: Flag.Int("parallel").pipe(
			Flag.withDescription(
				"How many scripts run at once, each in a place of its own."
			),
			Flag.withDefault(1)
		),
		rerunBroken: Flag.Boolean("rerun-broken").pipe(
			Flag.withDefault(false),
			Flag.withDescription(
				"Runs a script once more when its run is broken, and reports both runs. The exit status counts each script's last run."
			)
		),
	},
	({ scripts, parallel, rerunBroken }) =>
		Effect.gen(function* () {
			const built = yield* shown(Layer.build(scriptsOn(scriptsHere)));
			// Asked here first, so a machine that cannot be reached says why on stderr.
			yield* shown(Effect.scoped(Context.get(built, Machine).connection));
			const runner = Context.get(built, Scripts);
			const records = yield* Runs;
			const attempt = (script: string, attemptOf?: RunId) =>
				attemptRun(runner, records, script, script, attemptOf);
			const last = yield* Effect.forEach(
				scripts,
				Effect.fnUntraced(function* (script) {
					const first = yield* attempt(script);
					return rerunBroken &&
						first.outcome._tag === "broken" &&
						first.run !== undefined
						? yield* attempt(script, first.run)
						: first;
				}),
				{ concurrency: parallel }
			);
			const missed = Arr.filter(last, (ran) => !ended(ran.outcome)).length;
			if (missed > 0) {
				return yield* CliError.UserError.make({
					cause: missed,
					userMessage: `${missed} of ${last.length} scripts neither passed nor were skipped.`,
				});
			}
		}).pipe(Effect.scoped, Effect.provide(Runs.layer))
).pipe(
	Command.withDescription(
		"Runs scripts against this machine's game, each in a place of its own, and keeps a record of each run. With GAME_TESTING_URL and GAME_TESTING_TOKEN set, the scripts run here against the game of the machine whose serve the URL reaches, which keeps the records."
	)
);

/**
 * A script's declared state and mod, after its type check against the game's names, `gameNames`, or a state written as
 * JSON, which names no mod.
 */
const declaredIn = Effect.fnUntraced(function* (
	file: string,
	gameNames: string
) {
	const fs = yield* FileSystem.FileSystem;
	const path = yield* Path.Path;
	if (Str.endsWith(".json")(file)) {
		const text = yield* Effect.orDie(fs.readFileString(file));
		const state = yield* Effect.orDie(
			Schema.decodeEffect(Schema.fromJsonString(StartState))(text)
		);
		return { state, mod: undefined };
	}
	const declaration = yield* declarationOf(file, gameNames);
	const folder = scriptMod(path, file, declaration.mod);
	const mod =
		folder === undefined || folder === "none"
			? folder
			: yield* readMod(fs, path, folder.folder);
	return { state: declaration.state, mod };
});

const fromMod = (from: Origin) => from._tag === "mod";

const nameOf = (edit: PlannedEdit) =>
	edit._tag === "key" ? `key ${edit.key}` : edit.name;

/** What the base held, or for a value the mod adds, that the base holds none. */
const wasOf = (edit: PlannedEdit) =>
	edit._tag === "added"
		? "added by the mod: the base has no record, so the game starts it so"
		: `was ${String(edit.base)}`;

const spotKinds: Readonly<Record<StartSpot["kind"], string>> = {
	given: "a place the state gives",
	landing: "a landing of the game",
	"worked out": "a worked-out spot",
	found: "a found spot",
	proven: "a proven spot",
};

/** Where a start stands Link, and what kind of spot that is. */
const standing = (spot: StartSpot) =>
	`Link stands in ${spot.scene}${spot.room === undefined ? "" : ` room ${spot.room}`} at (${Arr.join(Arr.map(spot.position, String), ", ")}), facing ${spot.facing}: ${spotKinds[spot.kind]}, ${spot.about}`;

/**
 * A plan as `plan` prints it: the base, where Link stands, each value the words write, each a place it resets leaves alone, and then under a
 * heading of its own what the mod's new game starts with.
 */
const printPlan = (planned: StatePlan) =>
	Effect.gen(function* () {
		const words = Arr.filter(planned.edits, (edit) => !fromMod(edit.from));
		const started = Arr.filter(planned.edits, (edit) => fromMod(edit.from));
		const left = Arr.filter(planned.leftAlone, (each) => !fromMod(each.from));
		const moved = Arr.filter(planned.leftAlone, (each) => fromMod(each.from));
		if (planned.start !== undefined) {
			yield* Console.log(
				`From the start ${planned.start.name}: ${planned.start.about}`
			);
		}
		const { from } = planned.base;
		const kept =
			"name" in planned.base && from !== undefined ? `, ${keptText(from)}` : "";
		yield* Console.log(
			`${planned.start === undefined ? "From" : "On"} ${baseText(planned.base)}${kept}: ${planned.base.about}`
		);
		if (planned.location !== undefined) {
			yield* Console.log(standing(planned.location));
		}
		for (const edit of words) {
			yield* Console.log(
				`${nameOf(edit)} = ${String(edit.to)}    ${wasOf(edit)}, from ${originText(edit.from)}`
			);
		}
		for (const each of left) {
			yield* Console.log(
				`${each.name} left alone: ${originText(each.from)} names it, and it is not the place's own`
			);
		}
		const mod = Arr.head([...started, ...moved]);
		if (Option.isNone(mod)) {
			return;
		}
		yield* Console.log(
			`From ${originText(mod.value.from)}, as its new game starts:`
		);
		for (const edit of started) {
			yield* Console.log(
				`${nameOf(edit)} = ${String(edit.to)}    ${wasOf(edit)}`
			);
		}
		for (const each of moved) {
			yield* Console.log(
				`${each.name} left alone: the mod's new game starts it otherwise, and play moved it in the base`
			);
		}
	});

const plan = Command.make(
	"plan",
	{ file: Argument.File("script-or-state", { mustExist: true }) },
	({ file }) =>
		shown(
			Effect.flatMap(
				hereOrFar(
					Effect.gen(function* () {
						const states = yield* StartStates;
						const { state, mod } = yield* declaredIn(file, namesHere);
						return yield* states.plan(state, mod);
					}).pipe(Effect.provide(tables)),
					Effect.fnUntraced(function* (machine) {
						const names = yield* machine.names;
						const { state, mod } = yield* declaredIn(file, names);
						return yield* machine.call((client) =>
							client.Plan({ state, ...(mod === undefined ? {} : { mod }) })
						);
					})
				),
				printPlan
			)
		)
).pipe(
	Command.withDescription(
		"Prints every value a start state writes, what the base held and the word it came from, without a game. With GAME_TESTING_URL, the far machine plans it."
	)
);

const places = Command.make("places", {}, () =>
	shown(
		Effect.flatMap(
			hereOrFar(
				Effect.flatMap(Places, (machine) => machine.list).pipe(
					Effect.provide(Places.layer)
				),
				(machine) => machine.call((client) => client.Places())
			),
			(listed) =>
				Effect.forEach(
					listed,
					(place) =>
						Console.log(
							place.holder === undefined
								? `${place.place}  free`
								: `${place.place}  held by ${place.holder.owner} since ${place.holder.since}, run ${place.holder.run}`
						),
					{ concurrency: 1, discard: true }
				)
		)
	)
).pipe(
	Command.withDescription(
		"Lists this machine's places and who holds each; with GAME_TESTING_URL, the far machine's."
	)
);

const search = Command.make(
	"search",
	{
		text: Argument.String("text"),
		kind: Flag.Literals("kind", NameKind.literals).pipe(Flag.optional),
		limit: Flag.Int("limit").pipe(Flag.withDefault(40)),
	},
	({ text, kind, limit }) =>
		shown(
			Effect.flatMap(
				hereOrFar(
					Effect.gen(function* () {
						const data = yield* GameData;
						const own = yield* OwnTables;
						const kept = yield* Effect.all(
							{
								spots: Spots.use((service) => service.all),
								bases: Bases.use((service) => service.all),
							},
							{ concurrency: 1 }
						);
						return yield* searchNames(data, own, kept, text, {
							limit,
							kind: Option.getOrUndefined(kind),
						});
					}).pipe(Effect.provide([GameData.layer, Spots.layer, basesHere])),
					(machine) =>
						machine.call((client) =>
							client.Search({
								text,
								limit,
								...Option.match(kind, {
									onNone: () => ({}),
									onSome: (named) => ({ kind: named }),
								}),
							})
						)
				),
				(found) =>
					Effect.forEach(
						found,
						(match) =>
							Console.log(`${match.kind}  ${match.name}  ${match.about}`),
						{ concurrency: 1, discard: true }
					)
			)
		)
).pipe(
	Command.withDescription(
		"Finds every name a state or a script takes that contains the text: saved values, items, quests, scenes, landings, event settings, checks, spots and bases. With GAME_TESTING_URL, the far machine finds them."
	)
);

const kindOrder: readonly ChestSpots["spots"][number]["kind"][] = [
	"proven",
	"found",
	"worked out",
];

/** A chest's best spot, of any age, or none. */
const bestOf = (chest: ChestSpots) =>
	Arr.findFirst(kindOrder, (kind) =>
		Arr.some(chest.spots, (spot) => spot.kind === kind)
	).pipe(Option.getOrElse(() => "none"));

/** How many of `chests` have a best spot of each kind, and a look that failed, in words. */
const counted = (chests: readonly ChestSpots[]) => {
	const failed = Arr.filter(chests, (chest) =>
		Arr.some(chest.spots, (spot) => spot.look?.outcome === "failed")
	).length;
	return Arr.join(
		[
			...Arr.flatMapNullishOr([...kindOrder, "none"], (kind) => {
				const count = Arr.filter(
					chests,
					(chest) => bestOf(chest) === kind
				).length;
				return count === 0 ? undefined : `${count} ${kind}`;
			}),
			...(failed === 0
				? []
				: [`${failed} with a look from a spot that failed`]),
		],
		", "
	);
};

/** Every chest and the spot each age starts on, from the machine that plans looks. */
const listing = hereOrFar(
	Effect.gen(function* () {
		const data = yield* GameData;
		const own = yield* OwnTables;
		const kept = yield* Spots;
		return yield* chestSpots(data, own, kept);
	}).pipe(Effect.provide([GameData.layer, Spots.layer])),
	(machine) => machine.call((client) => client.Spots())
);

const spots = Command.make("spots", {}, () =>
	shown(
		Effect.flatMap(
			listing,
			Effect.fnUntraced(function* (chests) {
				const areas = Arr.groupBy(chests, (chest) => chest.area);
				for (const [area, inArea] of Rec.toEntries(areas)) {
					yield* Console.log(
						`${area === "" ? "No chest in the remake" : area}: ${counted(inArea)}`
					);
					for (const chest of inArea) {
						if (chest.spots.length === 0) {
							yield* Console.log(`  none        ${chest.name}`);
						}
						for (const spot of chest.spots) {
							yield* Console.log(
								`  ${Str.padEnd(10)(spot.kind)}  ${chest.name}  ${spot.age}: ${spot.about}`
							);
						}
					}
				}
				yield* Console.log(`${chests.length} chests: ${counted(chests)}.`);
			})
		)
	)
).pipe(
	Command.withDescription(
		"Lists every chest by area, with the spot each age starts on: proven once a look passed from it, found in a save or a running game, worked out before its actor, or none. With GAME_TESTING_URL, the far machine's spots, which are the ones its looks use."
	)
);

/** A debug server by its address: `host:port`, or a Unix socket's path. */
const debugServer = (address: string) => {
	if (Str.includes("/")(address)) {
		return Effect.succeed<DebugServer>({ path: address });
	}
	const url = URL.parse(`tcp://${address}`);
	return url === null || url.port === ""
		? Effect.fail(
				CliError.UserError.make({
					cause: address,
					userMessage: `--game takes host:port, such as 127.0.0.1:6545, or a socket's path, not ${address}.`,
				})
			)
		: Effect.succeed<DebugServer>({
				host: url.hostname,
				port: Number(url.port),
			});
};

/**
 * Runs `use` with the keeper of the machine that plans looks: this one, or with GAME_TESTING_URL the far one, whose
 * connection is made again after a loss, so a server restarted between two spots takes the second.
 */
const keeping = <A, E, R>(
	use: (keep: Keep<KeepFailure>) => Effect.Effect<A, E, R>
) =>
	Effect.gen(function* () {
		const url = yield* farUrl;
		if (Option.isNone(url)) {
			return yield* Effect.gen(function* () {
				const data = yield* GameData;
				const own = yield* OwnTables;
				const kept = yield* Spots;
				return yield* use((played) => keepPlayed(data, own, kept, played));
			}).pipe(Effect.provide([GameData.layer, Spots.layer]));
		}
		return yield* Machine.use((machine) =>
			use((played) =>
				machine.call((client) => client.RecordSpot({ spot: played }))
			)
		).pipe(Effect.provide(farMachine(url.value)));
	});

/** Why a spot was not kept: a refusal, or a machine that did not answer, which `RemoteUnavailable` names. */
interface KeepFailure {
	readonly _tag: string;
	readonly message: string;
}

const record = Command.make(
	"record",
	{
		name: Argument.String("name").pipe(
			Argument.withSchema(SpotName),
			Argument.optional
		),
		game: Flag.String("game").pipe(
			Flag.withDescription(
				"The running game's debug server: host:port, as EDEN_NX2_DEBUG_SERVER gives it, or a socket's path."
			),
			Flag.withDefault("127.0.0.1:6545")
		),
		isNew: Flag.Boolean("new").pipe(
			Flag.withDescription(
				"Keeps a name that is no check and no spot yet as a new spot. Without it such a name is refused with the nearest names."
			),
			Flag.withDefault(false)
		),
	},
	({ name, game, isNew }) =>
		shown(
			Effect.gen(function* () {
				const server = yield* debugServer(game);
				return yield* keeping((keep) =>
					Option.match(name, {
						onSome: (named) =>
							Effect.flatMap(recordHere(server, named, keep, isNew), (spot) =>
								Console.log(
									`kept ${spot.name}: ${spot.scene} at (${Arr.join(Arr.map(spot.position, String), ", ")}), facing ${spot.facing.toFixed(1)}`
								)
							),
						onNone: () =>
							Effect.gen(function* () {
								const own = yield* OwnTables;
								const chests = Arr.flatMapNullishOr(own.chests, (chest) => {
									const hashes = own.checks.get(chest)?.placements ?? [];
									return hashes.length === 0
										? undefined
										: { name: chest, hashes };
								});
								yield* Console.log(
									`Watching the game at ${game}: a chest Link opens keeps the spot he opened it from. Stop with Ctrl+C.`
								);
								return yield* watchChests(server, chests, keep, (line) =>
									Console.log(line)
								);
							}),
					})
				);
			})
		)
).pipe(
	Command.withDescription(
		"Reads a running game and keeps spots from it, sending nothing that changes the game. With a name, it keeps where Link stands now under that name, for a gift, a character or a place no chest marks; a name that is no check and no spot needs --new. Without one, it watches, and each chest Link opens keeps the spot he opened it from. The spots go to the recorded spots of the machine that plans looks: with GAME_TESTING_URL the far one, where they work at once. A spot that machine cannot take is held and sent again."
	)
);

const moveSpots = Command.make("move-spots", {}, () =>
	shown(
		Effect.gen(function* () {
			const fs = yield* FileSystem.FileSystem;
			const recorded = yield* hereOrFar(
				Spots.use((kept) => kept.recorded).pipe(Effect.provide(Spots.layer)),
				(machine) => machine.call((client) => client.Recorded())
			);
			if (recorded.spots.length + recorded.marks.length === 0) {
				return yield* Console.log(
					"Nothing is recorded that git does not have."
				);
			}
			const tracked = yield* readSpots(trackedSpots);
			yield* Effect.orDie(
				fs.writeFileString(
					trackedSpots,
					spotsText(intoTracked(tracked, recorded))
				)
			);
			yield* hereOrFar(
				Spots.use((kept) => kept.forget(recorded)).pipe(
					Effect.provide(Spots.layer)
				),
				(machine) =>
					machine.call((client) => client.Forget({ recordings: recorded }))
			);
			for (const spot of recorded.spots) {
				yield* Console.log(`moved the spot ${spot.name}  ${spot.scene}`);
			}
			for (const mark of recorded.marks) {
				yield* Console.log(
					`moved the mark ${mark.name}  a look from it ${mark.look.outcome}, run ${mark.look.run}`
				);
			}
			yield* Console.log(
				`They are in ${trackedSpots} now, and no longer recorded there: commit it.`
			);
		})
	)
).pipe(
	Command.withDescription(
		"Moves the spots and marks recorded on the machine that runs looks, with GAME_TESTING_URL the far one, into the tables/spots.tsv of the package it runs from, takes them out of what is recorded there, and prints that table's path."
	)
);

/** Marks a run's spot with its outcome on the machine that keeps the run. */
const marking = (id: RunId) =>
	hereOrFar(
		Effect.gen(function* () {
			const data = yield* GameData;
			const own = yield* OwnTables;
			const kept = yield* Spots;
			const ran = yield* Runs.use((records) => records.get(id));
			return yield* markSpot(data, own, kept, ran);
		}).pipe(Effect.provide([GameData.layer, Spots.layer, Runs.layer])),
		(machine) => machine.call((client) => client.MarkSpot({ run: id }))
	);

const prove = Command.make(
	"prove",
	{
		checks: Argument.String("chest").pipe(Argument.variadic({ min: 1 })),
		base: Flag.String("base").pipe(
			Flag.withDescription(
				"The base save each look starts from. Unless given, new-game-autosave, or early-adult for a chest only an adult stands before."
			),
			Flag.optional
		),
	},
	({ checks, base }) =>
		Effect.gen(function* () {
			const built = yield* shown(Layer.build(scriptsOn(scriptsHere)));
			// Asked here first, so a machine that cannot be reached says why on stderr.
			yield* shown(Effect.scoped(Context.get(built, Machine).connection));
			const runner = Context.get(built, Scripts);
			const records = yield* Runs;
			const own = yield* OwnTables;
			const fs = yield* FileSystem.FileSystem;
			const directories = yield* UserDirectories.UserDirectories;
			const folder = `${yield* Effect.orDie(directories.cache("game-testing"))}/prove`;
			yield* Effect.orDie(fs.makeDirectory(folder, { recursive: true }));
			const listed = yield* shown(listing);
			const unproven = yield* Effect.forEach(
				checks,
				Effect.fnUntraced(function* (check) {
					const chest = Arr.findFirst(listed, (each) => each.name === check);
					const hashes = own.checks.get(check)?.placements ?? [];
					if (Option.isNone(chest) || hashes.length === 0) {
						yield* Console.log(
							`not run  ${check}: it is no chest of the remake`
						);
						return true;
					}
					const adultOnly = Arr.every(
						chest.value.spots,
						(spot) => spot.age === "adult"
					);
					const from = Option.getOrElse(base, () =>
						adultOnly ? "early-adult" : "new-game-autosave"
					);
					// A folder of its own for each look, so two proofs of one chest at once each run their own.
					const ownFolder = yield* Effect.orDie(
						fs.makeTempDirectoryScoped({ directory: folder })
					);
					const file = `${ownFolder}/${Str.replace(/[^\w]+/gu, "-")(check)}.ts`;
					yield* Effect.orDie(
						fs.writeFileString(file, proveScript({ check, base: from, hashes }))
					);
					const ran = yield* attemptRun(runner, records, file, check);
					if (ran.run === undefined) {
						return true;
					}
					const mark = yield* Effect.result(marking(ran.run));
					yield* Console.log(
						Result.match(mark, {
							onSuccess: (marked) =>
								marked.look.outcome === "passed"
									? "    marked: its spot is proven"
									: "    marked: a look from its spot failed, which can be the start's state, such as a chest not there yet",
							onFailure: (refused) => `    not marked: ${refused.message}`,
						})
					);
					return ran.outcome._tag !== "passed";
				}, Effect.scoped),
				{ concurrency: 1 }
			);
			const missed = Arr.filter(unproven, (each) => each).length;
			if (missed > 0) {
				return yield* CliError.UserError.make({
					cause: missed,
					userMessage: `${missed} of ${checks.length} chests did not open from their spots.`,
				});
			}
		}).pipe(Effect.scoped, Effect.provide(Runs.layer))
).pipe(
	Command.withDescription(
		"Proves chests' spots by looks: each chest is put back closed, Link starts on its spot, and A opens it. A run that passed marks the spot proven and one that failed marks it failed, in the spots of the machine that ran it. About 40 seconds a chest, one at a time."
	)
);

/** Where Link stands, as a line says it: a location's name, or a scene, its room, the place and the facing. */
const standText = (stands: typeof Location.Type | undefined) => {
	if (stands === undefined) {
		return "its files are not where looks run";
	}
	if (typeof stands === "string") {
		return `at ${stands}`;
	}
	const [x, y, z] = Arr.map(stands.position, (axis) => axis.toFixed(1));
	return `in ${stands.scene}${stands.room === undefined ? "" : ` room ${stands.room}`} at (${x}, ${y}, ${z}) facing ${stands.facing.toFixed(0)}`;
};

/** The catalog as `bases` prints it: each base, then each start, with its line and what it was made from. */
const printCatalog = (catalog: Catalog) =>
	Effect.forEach(
		[
			...Arr.flatMap(catalog.bases, (base) => [
				`${base.name}  base  ${base.age}, ${base.hour}, ${standText(base.stands)}`,
				`    ${base.about}`,
				`    made from ${base.from === undefined ? "a save the game wrote" : keptText(base.from)}${base.recorded ? "; recorded on the game's machine, not yet in git" : ""}`,
			]),
			...Arr.flatMap(catalog.starts, ({ start, age, hour, stands }) => {
				const { name, about, base, ...words } = start;
				return [
					`${name}  start  ${age}, ${hour}, ${standText(stands)}`,
					`    ${about}`,
					`    made from ${base} with ${Formatter.formatJson(words)}`,
				];
			}),
		],
		(line) => Console.log(line),
		{ concurrency: 1, discard: true }
	);

const bases = Command.make("bases", {}, () =>
	shown(
		Effect.flatMap(
			hereOrFar(
				Bases.use((kept) => kept.catalog).pipe(Effect.provide(basesHere)),
				(machine) => machine.call((client) => client.Bases())
			),
			printCatalog
		)
	)
).pipe(
	Command.withDescription(
		"Lists every base and every named start a state's base can name: its age and hour, where Link stands, what it holds and what it was made from. With GAME_TESTING_URL, the far machine's."
	)
);

/** A save's files from a folder: its progress.sav and caption.sav, and its option.sav if it has one. Nothing else is read. */
const folderFiles = Effect.fnUntraced(function* (folder: string) {
	const fs = yield* FileSystem.FileSystem;
	const option = `${folder}/option.sav`;
	return {
		progress: yield* fs.readFile(`${folder}/progress.sav`),
		caption: yield* fs.readFile(`${folder}/caption.sav`),
		...((yield* fs.exists(option))
			? { option: yield* fs.readFile(option) }
			: {}),
	};
});

const nameBase = Command.make(
	"name-base",
	{
		name: Argument.String("name").pipe(
			Argument.withDescription("Lowercase words joined by hyphens.")
		),
		about: Flag.String("about").pipe(
			Flag.withDescription("One line on what the save holds and where Link is.")
		),
		hour: Flag.Literals("hour", Hour.literals).pipe(
			Flag.withDescription(
				"Whether the save is by day or by night, as its pictures show."
			)
		),
		run: Flag.String("run").pipe(Flag.withSchema(RunId), Flag.optional),
		save: Flag.Int("save").pipe(
			Flag.withDescription(
				"The run's save by its number in the run's folder; the last save the game wrote unless given."
			),
			Flag.optional
		),
		folder: Flag.Directory("folder", { mustExist: true }).pipe(
			Flag.withDescription(
				"A folder that holds a save's progress.sav and caption.sav, and its option.sav if it has one."
			),
			Flag.optional
		),
	},
	({ name, about, hour, run: from, save, folder }) =>
		shown(
			Effect.gen(function* () {
				const refused = (userMessage: string) =>
					Effect.fail(CliError.UserError.make({ cause: name, userMessage }));
				if (Option.isSome(from) && Option.isSome(folder)) {
					return yield* refused(
						"Give one save to name: --run or --folder, not both."
					);
				}
				const source = yield* Option.match(from, {
					onSome: (ran) =>
						Effect.succeed({
							run: ran,
							...Option.match(save, {
								onNone: () => ({}),
								onSome: (number) => ({ save: number }),
							}),
						}),
					onNone: () =>
						Option.match(folder, {
							onSome: folderFiles,
							onNone: () =>
								refused("Give the save to name: --run, or --folder."),
						}),
				});
				const request = yield* Schema.decodeEffect(NewBase)({
					name,
					about,
					hour,
					source,
				});
				const named = yield* hereOrFar(
					Bases.use((kept) => kept.name(request)).pipe(
						Effect.provide(basesHere)
					),
					(machine) => machine.call((client) => client.NameBase(request))
				);
				yield* Console.log(
					`${named.name}  base  ${named.age}, ${named.hour}, made from ${named.from === undefined ? "a save the game wrote" : keptText(named.from)}. base: "${named.name}" starts from it now.`
				);
			})
		)
).pipe(
	Command.withDescription(
		"Keeps a save the game wrote as a base under a name, with a line on what it holds: a run's save, the last the game wrote unless --save is given, or a save in a folder, such as one from play. The base works at once on the machine that runs looks, with GAME_TESTING_URL the far one, and move-bases puts its row into git. A folder without option.sav takes the new game's."
	)
);

const moveBases = Command.make("move-bases", {}, () =>
	shown(
		Effect.gen(function* () {
			const fs = yield* FileSystem.FileSystem;
			const own = yield* OwnTables;
			const catalog = yield* hereOrFar(
				Bases.use((kept) => kept.catalog).pipe(Effect.provide(basesHere)),
				(machine) => machine.call((client) => client.Bases())
			);
			const moving = Arr.filter(
				catalog.bases,
				(base) => base.recorded && !own.bases.has(base.name)
			);
			if (moving.length === 0) {
				return yield* Console.log(
					"Nothing is recorded that git does not have."
				);
			}
			const table = yield* fs.readFileString(trackedBases);
			yield* fs.writeFileString(
				trackedBases,
				`${table}${Arr.join(
					Arr.map(moving, ({ recorded: _, stands: __, ...base }) =>
						baseLine(base)
					),
					"\n"
				)}\n`
			);
			for (const base of moving) {
				yield* Console.log(`${base.name}  moved`);
			}
			yield* Console.log(
				`They are in ${trackedBases} now: commit it. Their files stay on the game machine, in its data folder's bases/<name>/.`
			);
		})
	)
).pipe(
	Command.withDescription(
		"Moves the rows of the bases named on the machine that runs looks, with GAME_TESTING_URL the far one, into the tables/bases.tsv of the package it runs from, and prints that table's path."
	)
);

const runs = Command.make(
	"runs",
	{
		script: Flag.String("script").pipe(Flag.optional),
		limit: Flag.Int("limit").pipe(Flag.withDefault(20)),
	},
	({ script, limit }) =>
		Effect.gen(function* () {
			const filter = Option.match(script, {
				onNone: () => ({ limit }),
				onSome: (path) => ({ script: path, limit }),
			});
			const found = yield* shown(
				hereOrFar(
					Effect.flatMap(Runs, (records) => records.list(filter)).pipe(
						Effect.provide(Runs.layer)
					),
					(machine) => machine.call((client) => client.Runs(filter))
				)
			);
			for (const summary of found.runs) {
				yield* Console.log(
					Arr.join(
						Arr.filter(
							[
								summary.id,
								summary.outcome._tag,
								summary.script ?? "",
								summary.milliseconds === undefined
									? ""
									: Duration.format(Duration.millis(summary.milliseconds)),
							],
							Str.isNonEmpty
						),
						"  "
					)
				);
			}
			if (found.unreadable > 0) {
				yield* Console.log(
					`${found.unreadable} more records could not be read: a record written by another version of the tool, or one being written.`
				);
			}
		})
).pipe(
	Command.withDescription(
		"Lists past runs on this machine, newest first; with GAME_TESTING_URL, the far machine's runs."
	)
);

const names = Command.make("names", {}, () =>
	shown(
		hereOrFar(
			writeNames.pipe(Effect.provide(GameData.layer)),
			(machine) => machine.names
		)
	)
).pipe(
	Command.withDescription(
		"Writes generated/names.d.ts from the game's tables, so scripts' names and values are checked as they are typed. With GAME_TESTING_URL, from the far machine's."
	)
);

const worker = Command.make(
	workerCommand,
	{
		host: Flag.String("host"),
		run: Flag.String("run").pipe(Flag.withSchema(RunId)),
		attemptOf: Flag.String("attempt-of").pipe(
			Flag.withSchema(RunId),
			Flag.optional
		),
		script: Argument.File("script", { mustExist: true }),
	},
	({ host: where, script, run: id, attemptOf }) =>
		work({
			host: where,
			script,
			run: id,
			attemptOf: Option.getOrUndefined(attemptOf),
		})
).pipe(Command.unlisted);

const declaration = Command.make(
	declarationCommand,
	{ script: Argument.File("script", { mustExist: true }) },
	({ script }) => declare(script)
).pipe(Command.unlisted);

/**
 * A host's services for `mcp` and `serve`, with the generated names written first, so a body's misspelled name fails
 * its type check on this machine.
 */
const hosting = Layer.effectDiscard(writeNames).pipe(
	Layer.provideMerge(Scripts.layer.pipe(Layer.provideMerge(host)))
);

const serve = Command.make("serve", {}, () =>
	shown(
		Effect.gen(function* () {
			const set = yield* token;
			if (Option.isNone(set)) {
				return yield* TokenRequired.make({ file: yield* tokenFile });
			}
			const required = set.value;
			const port = yield* Config.Port("GAME_TESTING_PORT").pipe(
				Config.withDefault(7480)
			);
			return yield* Layer.launch(
				serveOn({ port, token: required, version }).pipe(Layer.provide(hosting))
			);
		})
	)
).pipe(
	Command.withDescription(
		"Serves this machine's games to callers on other machines, on 127.0.0.1 only: the commands over a WebSocket at /rpc, which game-testing run uses with GAME_TESTING_URL, and the MCP tools over HTTP at /mcp. It needs GAME_TESTING_TOKEN, and every request must carry it as Authorization: Bearer <token>. The token is as strong as a shell on this machine: whoever holds it can send a script, and the script runs here as you. A tunnel you run gives it reach, such as ssh -L or tailscale serve. GAME_TESTING_PORT sets the port, 7480 unless set."
	)
);

// The host is built inside the command, so a start that fails says why through the command line, on stderr: stdout
// carries MCP's messages only. A far machine is first asked by the first tool that needs it, which answers why it
// cannot be reached.
const mcp = Command.make("mcp", {}, () =>
	shown(
		Layer.launch(
			McpServer.layerStdio({
				name: serverName,
				version,
				protocols,
				instructions,
				registrations,
			}).pipe(Layer.provide(scriptsOn(hosting)))
		)
	)
).pipe(
	Command.withDescription(
		"Serves the game as MCP tools over stdio, for an agent on this machine. Its games run in this machine's places. With GAME_TESTING_URL set, they run on the machine whose serve the URL reaches, with the token from GAME_TESTING_TOKEN or else game-testing/token in the config folder ($XDG_CONFIG_HOME, else ~/.config; on macOS ~/Library/Application Support); each run's record, pictures and saves are copied here once it ends."
	)
);

export const cli = Command.make("game-testing").pipe(
	Command.withDescription(
		"Puts Ocarina of Time in a state, drives it, reads the result and keeps a record."
	),
	Command.withSubcommands([
		run,
		plan,
		places,
		search,
		spots,
		record,
		moveSpots,
		prove,
		bases,
		nameBase,
		moveBases,
		runs,
		names,
		mcp,
		serve,
		worker,
		declaration,
	])
);
