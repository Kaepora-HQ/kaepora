#include <spdlog/spdlog.h>

#include "playthrough.hpp"
#include "fill.hpp"
#include "../location_access.h"

namespace Playthrough {

int Playthrough_Init(std::set<RandomizerCheck> excludedLocations, std::set<RandomizerTrick> enabledTricks,
                     const std::vector<RemakeChest>& chests, const std::vector<RemakeChest>& looseItems,
                     const RemakeShops& shops, const std::vector<RemakeGift>& gifts,
                     const std::vector<RemakeSkulltula>& skulltulas, const std::vector<RemakeSongPlace>& songs) {
    auto ctx = Rando::Context::GetInstance();
    ctx->ItemReset();
    ctx->GetLogic()->Reset();
    Regions::AccessReset();

    ctx->FinalizeSettings(excludedLocations, enabledTricks);

    int ret = Fill(chests, looseItems, shops, gifts, skulltulas, songs);
    if (ret < 0) {
        return ret;
    }
    return 1;
}

} // namespace Playthrough