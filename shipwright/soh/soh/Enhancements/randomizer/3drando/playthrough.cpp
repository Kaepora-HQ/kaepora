#include <spdlog/spdlog.h>

#include "playthrough.hpp"
#include "fill.hpp"
#include "../location_access.h"
#include "../rng.h"

namespace Playthrough {

int Playthrough_Init(uint32_t seed, std::set<RandomizerCheck> excludedLocations,
                     std::set<RandomizerTrick> enabledTricks, const std::vector<RemakeChest>& chests,
                     const std::vector<RemakeShopSlot>& shops) {
    // The seed alone starts the random numbers. Settings that resolve at random draw first, so placements depend on
    // the seed and on what generation does, not on how the options are listed.
    Random_Init(seed);

    auto ctx = Rando::Context::GetInstance();
    ctx->ItemReset();
    ctx->GetLogic()->Reset();
    Regions::AccessReset();

    ctx->FinalizeSettings(excludedLocations, enabledTricks);

    int ret = Fill(chests, shops);
    if (ret < 0) {
        return ret;
    }
    return 1;
}

} // namespace Playthrough