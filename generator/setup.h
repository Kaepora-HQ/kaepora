#pragma once

#include "remake.h"
#include "seed_settings.h"
#include "soh/Enhancements/randomizer/randomizerEnums.h"

#include <set>
#include <string_view>
#include <vector>

void InitializeRandomizer();

// The options of the unmodified game into the seed context, then the settings the player chose over them. Generating a
// seed rewrites some options, so each seed starts with these, as in SoH's menu.
void SetOptions(const SeedSettings& settings);

// The items that places name for the steps the settings start with and the steps under them.
std::vector<std::string_view> StartedCopies(const SeedSettings& settings);

// The kept places that name a started step or a step under it: the chests not shuffled, and the gifts while gifts are
// vanilla. Each gives that step again, which adds nothing, where SoH's logic would count the next step. As SoH's
// excluded locations they hold junk.
std::set<RandomizerCheck> GivenAgain(const SeedSettings& settings, const std::vector<RemakeChest>& chests,
                                     const std::vector<RemakeGift>& gifts);

// SoH tricks whose rule is what the game asks. SoH's FinalizeSettings turns on these and no others.
extern const std::set<RandomizerTrick> enabledTricks;
