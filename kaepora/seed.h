#pragma once

#include "generator/seed_settings.h"

#include <filesystem>
#include <string>

// Generates the seed with the settings and writes its folder: the mod under romfs/ and exefs/, which the emulator loads
// over the game, and spoiler.json. Throws when the folder exists or a step fails, and leaves no folder behind. The
// generator is one global state, so calls must not overlap.
void MakeSeed(const std::string& seed, const SeedSettings& settings, const std::filesystem::path& nup,
              const std::filesystem::path& seedDir);
