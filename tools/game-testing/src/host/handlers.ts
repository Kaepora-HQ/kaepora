/**
 * The host's side of `GameRpcs`. `Open` holds a game in its stream's scope and announces it, and ends once the run is
 * finished; every other command looks its session up and calls the running game.
 */
import { hostname } from "node:os";
import { Effect, Schedule, Stream } from "effect";
import type { SessionEvent } from "../contract/rpc.ts";
import { contract, GameRpcs } from "../contract/rpc.ts";
import { namesText } from "../data/declarations.ts";
import { GameData } from "../data/game-data.ts";
import { VersionMismatch } from "../errors.ts";
import { markSpot } from "../save/marks.ts";
import { chestSpots, keepPlayed, Spots } from "../save/spots.ts";
import { StartStates } from "../save/state.ts";
import { OwnTables, searchNames } from "../save/tables.ts";
import { Places } from "./places.ts";
import { Runs } from "./runs.ts";
import { Sessions } from "./sessions.ts";

/** How often a held game's `Open` stream says it is alive. */
const beatEvery = "5 seconds";

export const handlers = GameRpcs.toLayer(
	Effect.gen(function* () {
		const sessions = yield* Sessions;
		const places = yield* Places;
		const runs = yield* Runs;
		const data = yield* GameData;
		const own = yield* OwnTables;
		const spots = yield* Spots;
		const states = yield* StartStates;
		const machine = hostname();
		const count = (yield* places.list).length;
		// Read from the game's tables once, the first time a caller asks.
		const names = yield* Effect.cached(namesText);
		const beats: Stream.Stream<SessionEvent> = Stream.fromSchedule(
			Schedule.spaced(beatEvery)
		).pipe(Stream.as({ _tag: "beat" }));

		return GameRpcs.of({
			Hello: ({ contract: theirs }) =>
				theirs === contract
					? Effect.succeed({ machine, places: count, gameData: true })
					: Effect.fail(
							VersionMismatch.make({
								machine,
								machineSpeaks: contract,
								callerSpeaks: theirs,
							})
						),
			Places: () => places.list,
			Open: (open) =>
				Stream.unwrap(
					Effect.map(sessions.hold(open), (game) =>
						Stream.concat(
							Stream.succeed<SessionEvent>({
								_tag: "started",
								session: game.session,
								run: game.run,
								place: game.place,
								readBack: game.readBack,
							}),
							Stream.interruptWhen(beats, game.finished)
						)
					)
				),
			Press: ({ session, buttons, frames }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.press(buttons, frames)
				),
			Stick: ({ session, position, frames }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.stick(position, frames)
				),
			Idle: ({ session, frames }) =>
				Effect.flatMap(sessions.get(session), (game) => game.idle(frames)),
			PressUntil: ({ session, buttons, value, expected, times, every }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.pressUntil(buttons, value, expected, times, every)
				),
			CompleteDialog: ({ session, button, times, every }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.completeDialog(button, times, every)
				),
			Wait: ({ session, value, expected, within }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.wait(value, expected, within)
				),
			Read: ({ session, values }) =>
				Effect.flatMap(sessions.get(session), (game) => game.read(values)),
			Set: ({ session, values }) =>
				Effect.flatMap(sessions.get(session), (game) => game.set(values)),
			Check: ({ session, form, value, expected }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.check(form, value, expected)
				),
			Screenshot: ({ session, label }) =>
				Effect.flatMap(sessions.get(session), (game) => game.screenshot(label)),
			HandOver: ({ session, say, within }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.handOver(say, within)
				),
			Note: ({ session, text }) =>
				Effect.flatMap(sessions.get(session), (game) => game.note(text)),
			Stop: ({ session, reason }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.end({ _tag: "stopped", reason })
				),
			Finish: ({ session, result, failure }) =>
				Effect.flatMap(sessions.get(session), (game) =>
					game.finish({
						...(result === undefined ? {} : { result }),
						...(failure === undefined ? {} : { failure }),
					})
				),
			Plan: ({ state, mod }) =>
				Effect.map(
					states.plan(state, mod),
					({ base, edits, leftAlone, location }) => ({
						base,
						edits,
						leftAlone,
						...(location === undefined ? {} : { location }),
					})
				),
			Names: () => names,
			Search: ({ text, kind, limit }) =>
				Effect.flatMap(spots.all, (kept) =>
					searchNames(data, own, kept, text, { limit, kind })
				),
			Spots: () => chestSpots(data, own, spots),
			RecordSpot: ({ spot }) => keepPlayed(data, own, spots, spot),
			Recorded: () => spots.recorded,
			Forget: ({ recordings }) => spots.forget(recordings),
			MarkSpot: ({ run }) =>
				Effect.flatMap(runs.get(run), (record) =>
					markSpot(data, own, spots, record)
				),
			Runs: ({ script, limit }) => runs.list({ script, limit }),
			Run: ({ run }) => runs.get(run),
			RunFile: ({ run, path }) => runs.file(run, path),
		});
	})
);
