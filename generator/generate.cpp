#include "generate.h"

#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/3drando/playthrough.hpp"
#include "soh/util.h"
#include "remake.h"
#include "setup.h"

#include <stdexcept>

// SpoilerLog_Write fills this.
extern nlohmann::ordered_json jsonData;

GeneratedSeed Generate(const std::string& seed) {
    // SoH's start-up runs once per process. The tables are built in, so a table that fails to load fails every call.
    [[maybe_unused]] static const bool started = (InitializeRandomizer(), true);
    static const std::vector<RemakeChest> chests = LoadRemakeChests();

    auto ctx = Rando::Context::GetInstance();
    ctx->SetSeedString(seed);
    ctx->SetSeed(SohUtils::Hash(seed));
    ctx->ClearItemLocations();
    // No location is excluded and no trick is enabled, so both sets are empty.
    if (Playthrough::Playthrough_Init(ctx->GetSeed(), {}, {}, chests) <= 0) {
        throw std::runtime_error("generation failed");
    }
    return { jsonData.dump(4) + "\n", RemakePlan(seed, chests) };
}
