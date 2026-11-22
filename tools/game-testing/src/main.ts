/** The process entry. Logs go to stderr, because stdout carries what a command prints. */
import { BunRuntime, BunServices } from "@effect/platform-bun";
import { Effect, Layer, Logger } from "effect";
import { Command } from "effect/cli";
import { UserDirectories } from "effect/persistence";
import { cli, version } from "./cli.ts";
import { OwnTables } from "./save/tables.ts";
import * as Telemetry from "./telemetry.ts";

Command.run(cli, { version }).pipe(
	Effect.provide(
		Layer.mergeAll(
			Telemetry.layer(version),
			UserDirectories.layer,
			OwnTables.layer,
			Layer.succeed(Logger.LogToStderr, true)
		).pipe(Layer.provideMerge(BunServices.layer))
	),
	BunRuntime.runMain
);
