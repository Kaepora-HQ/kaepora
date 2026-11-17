// One seed, generated in memory, for kaepora-seedgen and the window.
#pragma once

#include "seed_settings.h"

#include <nlohmann/json.hpp>

#include <string>

struct GeneratedSeed {
    std::string spoilerLog;      // the text of the spoiler log file
    nlohmann::ordered_json plan; // what the patcher reads
};

// Places the items for the seed and proves the seed can be finished. Starts SoH's randomizer on the first call.
// Throws when the seed cannot be generated. The randomizer is one global state, so calls must not overlap.
GeneratedSeed Generate(const std::string& seed, const SeedSettings& settings);
