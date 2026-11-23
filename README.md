# Kaepora

Kaepora is a randomizer for the Switch 2 remake of Ocarina of Time. It moves the game's items to new places, so the Hookshot could be anywhere. Every seed can be finished, and the same seed gives the same game on every computer.

Website and downloads: <https://kaepora.org>

## What you need

- **Your own copy of the game**, version 2.0.0, as a `.nup` game file. Kaepora contains none of Nintendo's data, and neither do its downloads.
- **The [Lichii](https://github.com/zortos293/Lichii) emulator.** Kaepora writes a mod that Lichii loads over the game.
- **A computer** with Linux, Windows 10 or 11, or macOS 15 or later on Apple silicon.

## Download

Get the newest version from the [download page](https://kaepora.org) or from [Releases](https://github.com/Kaepora-HQ/kaepora/releases).

| System | File |
| --- | --- |
| Linux | `kaepora-linux-x86_64.AppImage`, or `kaepora-linux-x86_64.tar.gz` |
| Windows | `kaepora-windows-x86_64-setup.exe`, or `kaepora-windows-x86_64.zip` |
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

If the game runs slowly or stutters, see the [FAQ](FAQ.md).

### The tracker

The tracker is a window of the game's item pictures. A picture lights up when you find its item.

1. Start the game with **Play**. The tracker reads the game through Lichii, and Lichii lets it only when **Play** started the game.
2. Click **Tracker**. Under the list of seeds, Kaepora says what the tracker is doing.
3. Right-click the tracker to change the size of its pictures, its background colour, or the groups it shows.

To show the tracker on stream with OBS:

1. Add a source that captures a window, and choose the window **Kaepora Tracker**. On Windows the source is **Window Capture**. On macOS it is **macOS Screen Capture**, with its method set to **Window Capture**. On Linux it is **Window Capture (PipeWire)** under Wayland, or **Window Capture (Xcomposite)** under X11.
2. Key the background colour out with a **Color Key** filter, or choose a background colour that suits your layout.
3. Do not minimise the tracker. OBS cannot capture a minimised window.

## What a seed changes

A seed moves the items that chests hold. With the default settings, it also moves what characters and minigames hand you, the items that lie in the open, the songs, and the stones and medallions among the bosses and Rauru, as Ship of Harkinian's default settings do. Its settings decide what joins the shuffle and how the game starts. Adult Link can put the Master Sword back at any time, to be a child again before the Forest Temple. [What is randomized](RANDOMIZED.md) has every part, and <https://kaepora.org/randomized> shows the same page with the current counts.

## Build from source

You need CMake 3.28 or later, Ninja, a C++20 compiler (GCC or Clang), Python 3, and the libraries nlohmann_json, spdlog and zstd. The window also needs Qt 6 with Widgets and Network. Without Qt, everything except the window still builds. On Windows and macOS the build downloads the three libraries itself. On every system it downloads ARM's astc-encoder, which decodes the game's pictures.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

The build is a Release build unless you name another type, as with `-DCMAKE_BUILD_TYPE=Debug`.

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
