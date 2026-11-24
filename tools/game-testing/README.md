# Game testing

This tool runs a look in the game for you. You write a short script: a start state and the steps to take. The tool
builds the save, starts Ocarina of Time in the Lichii emulator on the game machine, puts Link in control, runs your
steps, and answers with an outcome, the values you read, and pictures. Your machine needs Bun and this package. It
needs no game file, no save and no emulator.

## Run a look

Your environment gives you a command that reaches the game machine, such as `gt`. It sets `GAME_TESTING_URL` and
`GAME_TESTING_TOKEN` and runs `bun src/main.ts` from this package. Put your script anywhere, such as your own scratch
folder, then:

```sh
gt import            # the line your script starts with
gt run look.ts       # the start, your steps, and the outcome
gt plan look.ts      # what the start state writes, without a game
gt search Slingshot  # the names a state or a script takes
gt spots             # every chest, and the spot a look starts on before it
gt bases             # every base and named start a look can start from
gt runs              # past runs on the game machine
```

The game machine runs one game at a time. A start waits up to 20 minutes for the game before it, then about 25
seconds until Link can move. A whole look takes 35 to 90 seconds once it starts.

## The smallest script

```ts
import { Effect, script } from "<this package>/src/script.ts";

export default script({
	about: "Mido's top left chest opens to A.",
	state: {
		base: "new-game-autosave",
		location: "KF Mido Top Left Chest",
		reset: ["KF Mido Top Left Chest"],
	},
	run: (game) =>
		Effect.gen(function* () {
			const chest = "IsTreasureBoxOpen.12786193094758345086";
			const presses = yield* game.pressUntil("A", chest, true);
			const opened = yield* game.check(chest, true);
			return { presses, opened, shot: yield* game.screenshot("after") };
		}),
});
```

Its first line imports `script` and `Effect` from this package by its absolute path, which `gt import` prints. Your
script then needs no packages of its own. The script is type-checked against the game's names before any game
starts, so a misspelled name comes back in seconds.

The game writes an item's flag only when the last page of the item's text closes, so a script presses A until the
flag holds, as above. One press is never enough for an item: the chest above took four.

A script's steps may take 5 minutes of play, counted from Link in control. Give `limit: "10 minutes"` beside `state`
for a longer one. A script past its limit is stopped and ends `broken`.

## The start state

Only `base` is needed. Every other word changes the base save.

```ts
state: {
	base: "early-adult",                  // a base or a named start, as gt bases lists them
	location: "GV Chest",                 // where Link stands: a check, a spot or a landing
	age: "child",                         // the sword and shield swap with it
	withItems: ["Item_Hammer", { item: "Item_Token_Gold", count: 30 }], // added to what the base holds
	withoutItems: ["Item_Range_Bow"],     // taken away, with its quiver, arrows and button
	equipped: ["Item_Hammer"],            // on its button or slot
	reset: ["GV Chest"],                  // back as on a first visit: a check, a scene, or a scene and room
	story: { quests: { GatheringTokenGold: "CollectToken" }, events: ["Dm_Kakariko_0000"] },
	set: { "PlayerStatus.CurrentRupee": 50 }, // saved values by name, written last
	startValues: "base",                  // the base's own values, not the mod's start values
}
```

`reset: ["GV Chest"]` puts a chest, a room or a scene back as on a first visit.

`gt bases` lists every base a look can start from. Start from a new or early base: the complete game has every story
flag set, which hides what a place waits for.

`location` can also be `{ scene, room, position: [x, y, z], facing }` in the game's own coordinates, with `room` only
inside a dungeon and `facing` in degrees, 0 facing the scene's +Z.

## Where Link starts

A location that is a check stands Link on its spot. A spot is a row of `tables/spots.tsv`, or a spot recorded on the
game machine that git does not have yet, which wins. A spot serves one age when its scene's name has `_Child` or
`_Adult` in it, and both ages otherwise, so a check can have a spot for each age. A check with no spot stands Link one
unit before its chest or actor, on the side the actor faces. A check with no actor, or with two for one age, has no
such spot, and is refused until it has a spot of its own. `gt plan` says which kind of spot a start stands on: proven
once a look passed from it, found in a save or a running game, or worked out, and the last look from it. Look at the
first picture: a chest behind rocks needs a spot of its own.

`gt spots` lists every chest by area, with the kind of spot each age starts on. It lists chests only: a spot recorded
by another name works, and `gt search` finds it.

## Record a spot

A spot comes from the game itself. `gt record` reads a running game through its debug server, which Lichii serves when
it starts with `EDEN_NX2_DEBUG_SERVER=127.0.0.1:6545`, and sends nothing that changes the game:

```sh
gt record                                  # watch: each chest Link opens keeps the spot he opened it from
gt record "Kak 10 Gold Skulltula Reward"   # keep where Link stands now, under a check's or a spot's name
gt record --new "Kak Windmill Ledge"       # under a name no check or spot has yet
gt record --game 127.0.0.1:21198           # another debug server
```

A recorded spot goes to the game machine's recorded spots, a SQLite file in its data folder
(`~/.local/share/game-testing/recorded.sqlite`), and the next look starts on it. A chest's spot faces the chest, and
Link must stand within 1.5 units of it across the floor, or nothing is kept. A spot by name keeps the way Link faces,
and his room inside a dungeon. A name that is no check and no spot is refused with the nearest names, unless `--new`
says it is new. The watcher cannot see the six trap chests, which keep no value that says they are open: record them
by name. When the game machine does not answer, the watcher holds the spot and sends it again until it is kept.

## Prove a spot

```sh
gt prove "GV Chest" "Deku Tree Map Chest"   # one look a chest, about 40 seconds each, one at a time
gt prove --base early-child "ZD Chest"      # each look from another base save
```

For each chest a look puts it back closed, starts Link on its spot and presses A until it opens. It starts from
`new-game-autosave`, or from `early-adult` for a chest only an adult stands before, unless `--base` names another. The
look's outcome is kept as a mark on the spot it started on, in the game machine's recorded spots: a look that passed
proves the spot, and `gt spots` and `gt plan` say when a look from it failed. A failed look says the look failed, not
that the spot is wrong: a chest that appears once a room's enemies are beaten, or rocks before it, fail a look from the
right spot. A mark holds while its spot stands where the look started.

## Move recorded spots into git

Spots and marks recorded on the game machine move into git with one command, run in this package in the worktree you
commit in, with `GAME_TESTING_URL` and `GAME_TESTING_TOKEN` set as `gt` sets them:

```sh
bun src/main.ts move-spots
```

It writes them into this package's `tables/spots.tsv`, a mark as its spot's `look`, takes them out of the game
machine's recorded spots, and prints that table's path. Commit the table.

## Choose a start

`base` takes one of three things:

```ts
base: "new-game-autosave"                      // a base: a save the game wrote, kept under a name
base: "adult-forest-temple-door"               // a named start: a base and words, under one name
base: { run: "20261123-231622-oyml" }          // the last save the game wrote in a run
base: { run: "20261123-231622-oyml", save: 1 } // the run's save-1
```

`gt bases` lists every base and named start: its age and hour, where Link stands, what it holds, and what it was made
from. Read it to choose a start:

```text
kokiri-mido-chest-open  base  child, day, in KokiriForest_Child room 0 at (-52.9, 112.3, -41.4) facing 25
    A child before Mido's top left chest, opened, in Mido's house in Kokiri Forest; holding nothing else.
    made from save 1 of run 20261123-231622-oyml
adult-forest-temple-door  start  adult, day, at SacredForestMeadow_Adult/OutsideOfForestTemple
    An adult outside the Forest Temple's door with the Hookshot, who learnt the Minuet of the Forest there, on the early adult.
    made from early-adult with {"location":"SacredForestMeadow_Adult/OutsideOfForestTemple","withItems":["Item_Hookshot","Item_Melody_ForestMinuet"],"story":{"events":["Dm_MelodyGet_Forest"]}}
```

A look that needs a long walk to reach its start pays for the walk once. Run the walk as a look, then start the next
look from that run's last save, or name the save. A start from a run's save reaches control as fast as any other. The
plan and the record say which run and which save a start came from, under `base`. Save 0 is the save the tool built
for the run's start, which the game did not write, so it is refused.

### Name a base

```sh
gt name-base --run 20261123-231622-oyml --hour day --about "A child before Mido's chest, opened." kokiri-mido-chest-open
gt name-base --folder <a save's folder> --hour night --about "Link in the Lost Woods by night." lost-woods-night
```

A name is lowercase words joined by hyphens, and no other base or start has it. `--hour` is `day` or `night`, as the
run's last picture shows. `--save <n>` names another save of the run than its last. `--folder` reads that folder's
`progress.sav`, `caption.sav` and, if it has one, `option.sav`, and nothing else: a base without `option.sav` takes the
new game's. The base's age is read from its save.

The base works at once: `base: "<name>"` starts from it, `gt search` finds it, and `gt bases` says it is recorded on the
game machine and not yet in git. Its three files go to the game machine's data folder, `bases/<name>/`, beside
`row.json`, which holds its row: the SHA-256 of each file, its age and hour, its line, and the run and save it came
from. A start checks each file against its row.

The files are the game's data and never go into git. The row does, with one command, run in this package in the
worktree you commit in, with `GAME_TESTING_URL` and `GAME_TESTING_TOKEN` set as `gt` sets them:

```sh
bun src/main.ts move-bases
```

It adds each recorded row that this package's `tables/bases.tsv` lacks, and prints that table's path. Commit the table.
Once the game machine runs that commit, git's row is the one it reads. Another game machine needs the base's folder,
`bases/<name>/` under its data folder, copied from this one, and the row checks the copy.

A base keeps the run it came from: that run's record holds the run's own base, its words and every step, so a base's
whole history reads back. Nothing removes a run.

### Named starts

A named start is a base and the words a look adds to it, under one name, in `tables/starts.json`. Many looks share one,
and it costs no save file:

```json
{
	"name": "adult-forest-temple-door",
	"about": "An adult outside the Forest Temple's door with the Hookshot, who learnt the Minuet of the Forest there, on the early adult.",
	"base": "early-adult",
	"location": "SacredForestMeadow_Adult/OutsideOfForestTemple",
	"withItems": ["Item_Hookshot", "Item_Melody_ForestMinuet"],
	"story": { "events": ["Dm_MelodyGet_Forest"] }
}
```

Look at a new start's first picture: a place can hold a scene that waits for the player. Link arrives outside the Forest
Temple's door into Sheik's lesson, so this start gives the Minuet and marks the lesson's scene played.

Its `base` is a row of `tables/bases.tsv`, never another start or a run's save. A look that names it adds its own words.
A word that holds one value, such as `location`, `age` or one name in `set`, is the look's where both give it, and the
lists of both join. Words of the two that contradict each other, such as one item in `withItems` and `withoutItems`, are
refused as in one state. `gt plan` names the start and its base. A start is added by a commit.

## Find a name

`gt search <text>` finds every name a state or a script takes, with its kind: `value`, `item`, `quest`, `event`,
`scene`, `landing`, `check`, `spot` or `base`, which finds the named starts too. Add `--kind item` to narrow it. A name
the game does not have is refused with its nearest names.

## The steps

Each call is one step of the run's record.

- `yield* game.press("A")`, or `game.press(["ZL", "A"], { frames: 10 })`.
- `yield* game.stick({ stick: "Left", x: 0, y: 1 }, { frames: 30 })`. The stick moves Link relative to the camera.
- `yield* game.idle(60)` lets 60 frames pass, about a second.
- `const held = yield* game.read(name)`, and `yield* game.set(name, value)`.
- `yield* game.wait(name, value, { within: "15 seconds" })`.
- `yield* game.check(name, value)` proves an effect: the value holds it now and did not at the start. `game.still`
  proves a value stayed as it started.
- `const shot = yield* game.screenshot("label")`. Return a picture to keep its name in the result.
- `const presses = yield* game.pressUntil("A", name, value)` reads the value, then presses and lets 60 frames pass
  until it holds: 15 presses at most. A value that already holds takes no press. It answers how many presses it took,
  and its step lists what each press read. Give `{ times, every }` for others, `times` from 1 to 200. When the presses
  run out, the run ends `failed`, as a wait that ran out.
- `const talk = yield* game.completeDialog()` reads a talk, a sign or an item's text to its end. It takes a picture,
  presses A and lets 30 frames pass, until a dialog it saw open is over and Link can move again: 40 presses at most.
  Its first press may open the dialog. It answers `presses` and `pictures`, the pictures' names in order, and each
  press is a step with its picture. Give `{ button, times, every }` for others, `times` from 1 to 200.
  - With nothing to open, it waits 2 seconds after its first press, then the run ends `failed`, saying no dialog
    opened. A talk, an item's text and a sign each open within 2 frames of the press.
  - With instant text each picture holds a whole page. At the game's own text speed a press does not hurry the text,
    so a picture can catch a page half written and a page takes several presses: give more `times`.
  - A question takes whatever A chooses, the first answer.
  - Its pictures are 360 lines, about 0.56 MB each, so a talk of 25 presses copies about 14 MB to your machine, once,
    with the run's record.

## What an outcome means

- `passed`: every check held.
- `failed`: a check did not hold, or a wait ran out. The game said no.
- `unchecked`: the script made no check, so it proved nothing.
- `broken`: the run could not ask the game. Read the error under it. The start state may be refused, the game may not
  have loaded the save as written, or the script failed or ran past its limit. A second run tells a flaky machine
  from a fault.
- `skipped`: the script hands over to a person, and nobody is there.

`run` exits 0 only when every script passed or was skipped.

## The game a look plays

By default a look plays the game as a Kaepora player has it, with nothing shuffled:

- Text shows a page at once, through Lichii's instant text add-on. `textSpeed: "game"` keeps the game's own speed.
- With no mod named, the game machine's own mod is used. It is what every Kaepora seed carries:
  - it skips the scenes a player can skip, and the scene at each new area;
  - a weapon brings its container, such as the quiver with the Fairy Bow;
  - bombs can be lifted without the Goron's Bracelet;
  - lifting, diving and a dungeon's items work as in a seed;
  - the Door of Time, the Master Sword's pedestal and Sheik's place there work as in a seed;
  - a child who stored no sword gets none when he becomes a child again;
  - the first arrival in Zora's Domain or the Market moves no quest on;
  - the game's tutorial messages and Saria's greeting are skipped.
- The mod's start values reach the save. Each value the mod changes from the game's own new game is written where the
  base still holds the game's own start, as a seed's new game has it, so a look meets no tutorial its player skips. A
  value play moved in the base keeps the base's. `gt plan` lists them under `From the mod <name>, as its new game
  starts:`, and a word of the state wins over them. A place `reset` puts back takes the mod's start too.
- A value the mod adds to the game's list, such as a seed's `Kaepora_Once_` flags, has no record in a base. The game
  starts it at the mod's default, as a new game does, and `gt plan` lists it under the mod by its hash, `0x...`.
- `startValues: "base"` keeps the base's own values in place of the mod's: for a save older than its mod on purpose, or
  when the game rewrites a start value on load and refuses the start.

`mod: "none"` plays the game as it ships, with instant text still on unless you also give `textSpeed: "game"`.

Give your look's own mod as `mod: { folder: "<the folder the patcher wrote>" }`, absolute or from the script's own
folder. Its files travel with the run, and its start values reach the save as the default mod's do. A mod over 63 MiB
as sent is refused before anything is sent, and a file too large for the game's size table is refused before the game
starts. A file is too large only where the game's own table gives files of its kind their size plus one overhead.
For any other kind, such as NPC schedules, what a larger file needs is not known, so the start goes on and the run's
record names the file under `unmeasured`. Build the mod with the current patcher. An older mod skips no scenes, and many older mods change the bytes
Lichii's instant text patches, so Lichii leaves instant text out.

## Where the pictures and saves are

`run` prints each run's outcome and, under it, `record and pictures: <folder>`. The folder is on your machine and
holds `record.json` and `picture-1.png` and on. The record keeps every step and the result, the mod the game had, and
`textSpeed`, the text speed the game had. When the copy to your machine fails, the line under the outcome says why,
and `gt runs` still lists the run on the game machine.

The folder also keeps every save of the run, whole, in the order they were written: `save-0-progress.sav` and
`save-0-caption.sav` are the save built for the start, with `save-0-option.sav`, the options it started with, and
`save-1` on are each autosave and menu save the game wrote, also when the run fails, breaks or stops. The record lists
each under `saves`: its kind, its slot, how many steps had run when it appeared, the press of `pressUntil` it followed
when it appeared inside one, and the SHA-256 of each file. A save is about 160 KB. The record also keeps the start's
base, its named start if it had one, and its plan: each value it wrote, with where it came from. A start that fails
before Link can move keeps only the built save.
