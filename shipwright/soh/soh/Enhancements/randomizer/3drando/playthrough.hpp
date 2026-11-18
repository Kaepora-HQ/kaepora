#pragma once

#include <set>
#include "../SeedContext.h"
#include "generator/remake.h"

namespace Playthrough {
int Playthrough_Init(uint32_t seed, std::set<RandomizerCheck> excludedLocations,
                     std::set<RandomizerTrick> enabledTricks, const std::vector<RemakeChest>& chests,
                     const std::vector<RemakeShopSlot>& shops);
} // namespace Playthrough