#include <spdlog/spdlog.h>

#include "playthrough.hpp"
#include "fill.hpp"
#include "../location_access.h"
#include "../rng.h"
#include "spoiler_log.hpp"
#include "soh/Enhancements/debugger/performanceTimer.h"

namespace Playthrough {

int Playthrough_Init(uint32_t seed, std::set<RandomizerCheck> excludedLocations,
                     std::set<RandomizerTrick> enabledTricks, const std::vector<RemakeChest>& chests) {
    // The seed alone starts the random numbers. Settings that resolve at random draw first, so placements depend on
    // the seed and on what generation does, not on how the options are listed.
    Random_Init(seed);

    auto ctx = Rando::Context::GetInstance();
    ctx->overrides.clear();
    ctx->ItemReset();
    ctx->HintReset();
    ctx->GetLogic()->Reset();
    StartPerformanceTimer(PT_REGION_RESET);
    Regions::AccessReset();
    StopPerformanceTimer(PT_REGION_RESET);

    ctx->FinalizeSettings(excludedLocations, enabledTricks);
    ctx->SetHash(std::to_string(seed));

    int ret = Fill(chests);
    if (ret < 0) {
        return ret;
    }

    GenerateHash();

    // TODO: Handle different types of file output (Spoiler Log, Plando Template, Patch Files, Race Files, etc.)
    SPDLOG_INFO("Writing Spoiler Log...");
    StartPerformanceTimer(PT_SPOILER_LOG);
    SpoilerLog_Write();
    StopPerformanceTimer(PT_SPOILER_LOG);
    SPDLOG_INFO("Writing Spoiler Log Done");

    ctx->playthroughLocations.clear();
    ctx->playthroughBeatable = false;

    return 1;
}

} // namespace Playthrough