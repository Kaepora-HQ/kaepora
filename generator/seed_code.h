#pragma once

#include "seed_settings.h"

#include <string>
#include <string_view>
#include <variant>

// What decides a seed's game beside Kaepora's version.
struct SharedSeed {
    std::string seed;
    SeedSettings settings;
};
// A code another version wrote. Its settings cannot be read here.
struct FromAnotherVersion {
    std::string version;
};
// Text that is not a code this version can read.
struct Unreadable {};

// The code that carries the seed, the settings and this version, in upper case.
std::string ToSeedCode(const std::string& seed, const SeedSettings& settings);

// A code ignores case.
std::variant<SharedSeed, FromAnotherVersion, Unreadable> ParseSeedCode(std::string_view text);
