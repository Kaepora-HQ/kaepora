// A seed's folder: the mod and the spoiler log of one seed.
#pragma once

#include <filesystem>
#include <string>

// Generates the seed and writes its folder: the mod under romfs/, which the emulator loads over the game, and
// spoiler.json. Throws when the folder exists or a step fails, and leaves no folder behind. The generator is one
// global state, so calls must not overlap.
void MakeSeed(const std::string& seed, const std::filesystem::path& nup, const std::filesystem::path& seedDir);
