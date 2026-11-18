// A patch to the game's program that keeps Link's lift level from dropping. Picking up a strength item sets the level
// to the item's grade, so the bracelet after either pair of gauntlets, or the Silver Gauntlets after the Golden, would
// lower what Link can lift. With the patch the level takes an item's grade only when the grade is at least the level.
#pragma once

#include <string>
#include <string_view>

// The build ID of the program the patch changes, in hex. It names the patch's file, as the emulator applies a patch file
// only to the program whose build ID its name gives.
constexpr std::string_view PatchedBuildId = "E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348";

// The patch as the IPS32 file the emulator reads from a mod's exefs folder.
std::string ProgramPatch();
