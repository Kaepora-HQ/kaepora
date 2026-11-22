#pragma once

#include "remake.h"
#include "seed_settings.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

struct GeneratedSeed {
    std::string spoilerLog;      // the text of the spoiler log file
    nlohmann::ordered_json plan; // what the patcher reads
};

// Places the items for the seed and proves the seed can be finished. Starts SoH's randomizer on the first call.
// Seed text ignores case: the seed is the text in upper case, so owl and OWL are one seed, OWL. Throws when the seed
// cannot be generated. The randomizer is one global state, so calls must not overlap.
GeneratedSeed Generate(const std::string& text, const SeedSettings& settings);

// The kept places that name a step the settings start with or a step under it: the table's chests that the table or the
// settings keep in place, and the gifts of the groups left out. Each gives that step again, which adds nothing, where
// SoH's logic would count the next step. As SoH's excluded locations they hold junk.
std::set<RandomizerCheck> GivenAgain(const SeedSettings& settings, const std::vector<RemakeChest>& table,
                                     const std::vector<GroupedGift>& gifts);
