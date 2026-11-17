#include <spdlog/spdlog.h>

#include "playthrough.hpp"
#include "fill.hpp"
#include "../location_access.h"
#include "../rng.h"
#include "spoiler_log.hpp"
#include "soh/Enhancements/debugger/performanceTimer.h"

namespace Playthrough {

int Playthrough_Init(uint32_t seed, std::set<RandomizerCheck> excludedLocations,
                     std::set<RandomizerTrick> enabledTricks) {
    // The seed alone starts the random numbers. Settings that resolve at random draw first, so placements depend on
    // the seed and on what generation does, not on how the options are listed.
    Random_Init(seed);

    auto ctx = Rando::Context::GetInstance();
    ctx->overrides.clear();
    ctx->ItemReset();
    ctx->GetLogic()->Reset();
    StartPerformanceTimer(PT_REGION_RESET);
    Regions::AccessReset();
    StopPerformanceTimer(PT_REGION_RESET);

    ctx->FinalizeSettings(excludedLocations, enabledTricks);
    ctx->SetHash(std::to_string(seed));

    int ret = Fill();
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

// used for generating a lot of seeds at once
int Playthrough_Repeat(std::set<RandomizerCheck> excludedLocations, std::set<RandomizerTrick> enabledTricks,
                       int count /*= 1*/) {
    SPDLOG_INFO("GENERATING {} SEEDS", count);
    auto ctx = Rando::Context::GetInstance();
    uint32_t repeatedSeed = 0;
    for (int i = 0; i < count; i++) {
        char seedString[11];
        for (size_t i = 0; i < 10; i++) {
            seedString[i] = '0' + ShipUtils::Random(0, 10);
        }
        seedString[10] = '\0';
        ctx->SetSeedString(std::string(seedString));
        repeatedSeed = SohUtils::Hash(ctx->GetSeedString());
        ctx->SetSeed(repeatedSeed);
        SPDLOG_DEBUG("testing seed: %d", repeatedSeed);
        ClearProgress();
        Playthrough_Init(ctx->GetSeed(), excludedLocations, enabledTricks);
        SPDLOG_INFO("Seeds Generated: {}", i + 1);
    }

    return 1;
}
} // namespace Playthrough