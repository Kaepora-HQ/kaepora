// The patch to the game's program that every mod carries.
//
// Link's lift level never drops. Picking up a strength item sets the level to the item's grade, so the bracelet
// after either pair of gauntlets, or the Silver Gauntlets after the Golden, would lower what Link can lift. With the
// patch the level takes an item's grade only when the grade is at least the level.
//
// A boss key, map or compass counts from any chest. The large chest's event flow sets a dungeon's own value for one,
// and the small chest's does not, so from a small chest the boss door would stay chained and the map grey. With the
// patch the function that adds an item to Link's pouch sets the value for these three as the large chest does.
#pragma once

#include <string>
#include <string_view>

// The build ID of the program the patch changes, in hex. It names the patch's file, as the emulator applies a patch file
// only to the program whose build ID its name gives.
constexpr std::string_view PatchedBuildId = "E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348";

// The patch as the IPS32 file the emulator reads from a mod's exefs folder.
std::string ProgramPatch();
