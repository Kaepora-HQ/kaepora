# Kaepora

Kaepora is a randomizer for the Switch 2 remake of Ocarina of Time. It moves the game's items to new places, so the Hookshot could be anywhere. Every seed can be finished, and the same seed gives the same game on every computer.

Website and downloads: <https://kaepora.vercel.app>

## What you need

- **Your own copy of the game**, version 2.0.0, as a `.nup` game file. Kaepora contains none of Nintendo's data, and neither do its downloads.
- **The [Lichii](https://github.com/zortos293/Lichii) emulator.** Kaepora writes a mod that Lichii loads over the game.
- **A computer** with Linux, Windows 10 or 11, or macOS 15 or later on Apple silicon.

## Download

Get the newest version from the [download page](https://kaepora.vercel.app) or from [Releases](https://github.com/PixelHabits/kaepora/releases).

| System | File |
| --- | --- |
| Linux | `kaepora-linux-x86_64.AppImage`, or `kaepora-linux-x86_64.tar.gz` |
| Windows | `kaepora-windows-x86_64.zip` |
| macOS | `kaepora-macos-arm64.zip` |

On a Mac, macOS blocks Kaepora the first time you open it. The download page has the three steps that let it through.

## Make a seed and play it

1. Open Kaepora and click **Choose…** to pick your game file. You do this once.
2. Type a seed, or click **Random**. A seed is any text. Capital letters do not matter.
3. To change what the seed shuffles, click **Settings…**.
4. Click **Create seed**.
5. Select the seed in the list and click **Use this seed**. Kaepora copies the seed into Lichii's mods folder.
6. Click **Play**, and start a new game.

Kaepora keeps every seed you make. A save you started on an older seed still has its seed: select it and click **Use this seed** again before you load that save.

If you get stuck, **Open spoiler log** shows where every item went and one route that finishes the game.

## What a seed changes

- **Chests.** A seed moves the items that chests hold. Songs, gifts from characters, shop goods and Gold Skulltula tokens stay where the game has them.
- **Settings.** You choose which dungeons are shuffled, whether keys, maps and compasses stay in their own chests or move within their dungeon, whether four goods in every shop join the shuffle, and whether the Lens of Truth is ever needed. You can also change how the game starts: an open Kokiri Forest, the Rainbow Bridge already standing, no trials in Ganon's Castle, doors that need no keys, and every map and compass from the start.
- **Strength.** In the unmodified game, a weaker strength item picked up after a stronger one lowers what Link can lift. With a seed it never goes down.
- **A few doors and story steps.** Four dungeon doors start open, and three story scenes no longer set a quest back. Without these changes, some orders of play would leave a seed that cannot be finished.

## Build from source

You need CMake 3.28 or later, Ninja, a C++20 compiler (GCC or Clang), Python 3, and the libraries nlohmann_json, spdlog and zstd. The window also needs Qt 6 with Widgets and Network. Without Qt, everything except the window still builds. On Windows and macOS the build downloads the three libraries itself.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Some tests read the game file. To run them, name your game file when you configure:

```sh
cmake -S . -B build -G Ninja -DKAEPORA_GAME_NUP=/path/to/game.nup
```

The build makes four programs:

| Program | What it does |
| --- | --- |
| `kaepora` | The window that players use. |
| `kaepora-seedgen` | Makes a seed: its spoiler log, and the plan of what goes where. |
| `kaepora-patch` | Writes a plan into a mod folder that Lichii loads. |
| `kaepora-reach` | Asks the logic whether a location can be reached. The tests use it. |

## Where the logic comes from

Kaepora's logic is the randomizer of [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright), changed where the remake differs from the original game. The files under `shipwright/` come from Ship of Harkinian and libultraship and keep their own notices. `tools/upstream.py` follows their changes.

## Licence

Kaepora's own code is under the MIT licence. See `LICENSE`.

Kaepora is a fan project. It is not affiliated with Nintendo.
