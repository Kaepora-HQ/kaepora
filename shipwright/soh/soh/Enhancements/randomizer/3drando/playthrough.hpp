#pragma once

#include <set>
#include "../SeedContext.h"
#include "generator/remake.h"

namespace Playthrough {
// Draws from the random numbers as they stand, so the caller starts them.
int Playthrough_Init(std::set<RandomizerCheck> excludedLocations, std::set<RandomizerTrick> enabledTricks,
                     const std::vector<RemakeChest>& chests, const std::vector<RemakeChest>& looseItems,
                     const RemakeShops& shops, const std::vector<RemakeGift>& gifts,
                     const std::vector<RemakeSkulltula>& skulltulas, const std::vector<RemakeSongPlace>& songs);
} // namespace Playthrough