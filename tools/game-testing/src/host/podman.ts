import type { Effect } from "effect";
import type { ChildProcessSpawner } from "effect/process";
import { ChildProcess } from "effect/process";
import type { Ran } from "../process.ts";
import { ran } from "../process.ts";

export const podman = (
	spawner: ChildProcessSpawner.ChildProcessSpawner["Service"],
	args: readonly string[]
): Effect.Effect<Ran> => ran(spawner, ChildProcess.make("podman", args));
