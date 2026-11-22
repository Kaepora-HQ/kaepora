#pragma once

#include "remake.h"
#include "seed_settings.h"
#include "soh/Enhancements/randomizer/randomizerEnums.h"

#include <set>
#include <vector>

void InitializeRandomizer();

// The options of the unmodified game into the seed context, then the settings the player chose over them, with the
// gifts of remake/gifts.tsv for what the groups ticked shuffle. Generating a seed rewrites some options, so each seed
// starts with these, as in SoH's menu.
void SetOptions(const SeedSettings& settings, const std::vector<GroupedGift>& gifts);

// SoH tricks whose rule is what the game asks. SoH's FinalizeSettings turns on these and no others.
extern const std::set<RandomizerTrick> enabledTricks;
