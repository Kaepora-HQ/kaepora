/**
 * A fake spawner that answers the podman commands a place runs, as podman 6.2 answered them on the main machine. Its
 * containers are records in memory, and nothing runs. A name is held until its container is removed, and of several
 * creates of one name only one wins, as measured with eight at once. A test can answer any command its own way.
 */
import {
	Array as Arr,
	Deferred,
	Effect,
	Formatter,
	Layer,
	Option,
	Record as Rec,
	Redacted,
	Ref,
	Sink,
	String as Str,
	Stream,
} from "effect";
import type { ChildProcess } from "effect/process";
import { ChildProcessSpawner } from "effect/process";

export interface FakeContainer {
	/** Every argument of the create that made it. */
	readonly args: readonly string[];
	readonly exit: Deferred.Deferred<number>;
	readonly id: string;
	readonly labels: Readonly<Record<string, string>>;
	readonly name: string;
}

export interface FakePodman {
	/** Every podman command run, in order. */
	readonly calls: Effect.Effect<readonly (readonly string[])[]>;
	/** The containers podman holds, by name. */
	readonly containers: Effect.Effect<ReadonlyMap<string, FakeContainer>>;
	readonly layer: Layer.Layer<ChildProcessSpawner.ChildProcessSpawner>;
	/** Adds a container, as a holder that went away leaves it behind. */
	readonly leave: (
		name: string,
		labels: Readonly<Record<string, string>>
	) => Effect.Effect<FakeContainer>;
	/** Each container removed, with the seconds `rm` gave it to stop. */
	readonly removed: Effect.Effect<
		readonly { readonly id: string; readonly grace: number }[]
	>;
}

export interface Answer {
	readonly code: number;
	readonly stderr?: string;
	readonly stdout?: string;
}

const encoder = new TextEncoder();

const values = (args: readonly string[], name: string) =>
	Arr.flatMap(args, (arg, at) =>
		arg === name ? Arr.fromOption(Arr.get(args, at + 1)) : []
	);

const value = (args: readonly string[], name: string) =>
	Option.getOrThrow(Arr.head(values(args, name)));

const handle = (answer: Effect.Effect<Answer>) =>
	Effect.gen(function* () {
		const done = yield* Effect.cached(answer);
		const output = (pick: (answered: Answer) => string | undefined) =>
			Stream.unwrap(
				Effect.map(done, (answered) =>
					Stream.make(encoder.encode(pick(answered) ?? ""))
				)
			);
		return ChildProcessSpawner.makeHandle({
			pid: ChildProcessSpawner.ProcessId(0),
			exitCode: Effect.map(done, (answered) =>
				ChildProcessSpawner.ExitCode(answered.code)
			),
			isRunning: Effect.succeed(false),
			kill: () => Effect.void,
			stdin: Sink.drain,
			stdout: output((answered) => answered.stdout),
			stderr: output((answered) => answered.stderr),
			all: Stream.empty,
			getInputFd: () => Sink.drain,
			getOutputFd: () => Stream.empty,
			unref: Effect.succeed(Effect.void),
		});
	});

export const fakePodman = (options: {
	/** An answer of the test's own for a command, in place of the fake's. */
	readonly answer?: (args: readonly string[]) => Option.Option<Answer>;
	/** The devices podman discovers: `nvidia.com/gpu=all` when NVIDIA's Container Toolkit wrote its list. */
	readonly devices: readonly string[];
	/** What a container's command does once `podman start` has run, such as write its log and exit. */
	readonly onStart?: (container: FakeContainer) => Effect.Effect<void>;
}) =>
	Effect.gen(function* () {
		const containers = yield* Ref.make(new Map<string, FakeContainer>());
		const removed = yield* Ref.make<{ id: string; grace: number }[]>([]);
		const calls = yield* Ref.make<(readonly string[])[]>([]);
		let made = 0;

		const make = Effect.fnUntraced(function* (
			name: string,
			labels: Readonly<Record<string, string>>,
			args: readonly string[]
		) {
			made += 1;
			const container: FakeContainer = {
				id: `container-${made}`,
				name,
				labels,
				args,
				exit: yield* Deferred.make<number>(),
			};
			return container;
		});

		const hold = (container: FakeContainer) =>
			Ref.modify(containers, (held) => {
				const holder = held.get(container.name);
				return holder === undefined
					? [Option.none(), new Map(held).set(container.name, container)]
					: [Option.some(holder), held];
			});

		const byId = (id: string) =>
			Effect.map(Ref.get(containers), (held) =>
				Option.getOrThrow(
					Arr.findFirst([...held.values()], (container) => container.id === id)
				)
			);

		const podman = Effect.fnUntraced(function* (args: readonly string[]) {
			yield* Ref.update(calls, (ran) => [...ran, args]);
			const own = options.answer?.(args) ?? Option.none();
			if (Option.isSome(own)) {
				return own.value;
			}
			const [verb] = args;
			const last = Arr.lastNonEmpty(args as [string, ...string[]]);
			switch (verb) {
				case "info":
					return {
						code: 0,
						stdout: `${Formatter.formatJson(Arr.map(options.devices, (id) => ({ source: "cdi", id })))}\n`,
					};
				case "image":
					return { code: 0 };
				case "create": {
					const labels = Rec.fromEntries(
						Arr.map(values(args, "--label"), (label) => {
							const [key = "", ...rest] = Str.split(label, "=");
							return [key, Arr.join(rest, "=")] as const;
						})
					);
					const container = yield* make(value(args, "--name"), labels, args);
					const holder = yield* hold(container);
					return Option.match(holder, {
						onNone: () => ({ code: 0, stdout: `${container.id}\n` }),
						onSome: (other) => ({
							code: 125,
							stderr: `Error: creating container storage: the container name "${container.name}" is already in use by ${other.id}. You have to remove that container to be able to reuse that name: that name is already in use, or use --replace to instruct Podman to do so.\n`,
						}),
					});
				}
				case "start": {
					const container = yield* byId(last);
					yield* Effect.forkDetach(options.onStart?.(container) ?? Effect.void);
					return { code: 0, stdout: `${container.id}\n` };
				}
				case "wait": {
					const container = yield* byId(last);
					const status = yield* Deferred.await(container.exit);
					return { code: 0, stdout: `${status}\n` };
				}
				case "rm": {
					const held = [...(yield* Ref.get(containers)).values()];
					const container = Arr.findFirst(held, (each) => each.id === last);
					if (Option.isSome(container)) {
						yield* Ref.update(removed, (gone) => [
							...gone,
							{ id: last, grace: Number(value(args, "--time")) },
						]);
						yield* Deferred.succeed(container.value.exit, 143);
						yield* Ref.update(containers, (left) => {
							const kept = new Map(left);
							kept.delete(container.value.name);
							return kept;
						});
					}
					return { code: 0 };
				}
				case "ps": {
					const listed = Arr.map(
						[...(yield* Ref.get(containers)).values()],
						(container) => ({
							Id: container.id,
							Names: [container.name],
							Labels: {
								...container.labels,
								"org.opencontainers.image.version": "26.04",
							},
						})
					);
					return { code: 0, stdout: `${Formatter.formatJson(listed)}\n` };
				}
				default:
					throw new Error(`the fake podman has no ${Arr.join(args, " ")}`);
			}
		});

		const spawn = (command: ChildProcess.Command) =>
			Effect.suspend(() => {
				if (
					command._tag !== "StandardCommand" ||
					command.command !== "podman"
				) {
					throw new Error("the fake spawner runs only podman");
				}
				return handle(
					podman(
						Arr.map(command.args, (arg) =>
							Redacted.isRedacted(arg) ? Redacted.value(arg) : arg
						)
					)
				);
			});

		const fake: FakePodman = {
			calls: Ref.get(calls),
			containers: Ref.get(containers),
			leave: (name, labels) => Effect.tap(make(name, labels, []), hold),
			layer: Layer.succeed(
				ChildProcessSpawner.ChildProcessSpawner,
				ChildProcessSpawner.make(spawn)
			),
			removed: Ref.get(removed),
		};
		return fake;
	});
