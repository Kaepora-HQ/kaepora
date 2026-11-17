#include "generate.h"

#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/3drando/playthrough.hpp"
#include "soh/util.h"
#include "remake.h"
#include "setup.h"

#include <format>
#include <stdexcept>

// Adds the location's name and the name of the item placed there.
static void AddPlacement(nlohmann::ordered_json& placements, RandomizerCheck location) {
    placements[Rando::StaticData::GetLocation(location)->GetName()] =
        Rando::Context::GetInstance()->GetItemLocation(location)->GetPlacedItemName().GetEnglish();
}

// Kaepora's version, the seed, the item in each shuffled chest in table order, then the spheres of the route the logic
// found to finish the seed.
static std::string SpoilerLog(const std::string& seed, const std::vector<RemakeChest>& chests) {
    nlohmann::ordered_json log = { { "kaepora", KAEPORA_VERSION },
                                   { "seed", seed },
                                   { "locations", nlohmann::ordered_json::object() },
                                   { "playthrough", nlohmann::ordered_json::object() } };
    for (const RemakeChest& chest : chests) {
        if (chest.shuffled) {
            AddPlacement(log["locations"], chest.location);
        }
    }
    const auto& spheres = Rando::Context::GetInstance()->playthroughLocations;
    for (size_t sphere = 0; sphere < spheres.size(); ++sphere) {
        for (RandomizerCheck location : spheres[sphere]) {
            AddPlacement(log["playthrough"][std::format("sphere {:02}", sphere)], location);
        }
    }
    return log.dump(4) + "\n";
}

GeneratedSeed Generate(const std::string& seed) {
    // SoH's start-up runs once per process. The tables are built in, so a table that fails to load fails every call.
    [[maybe_unused]] static const bool started = (InitializeRandomizer(), true);
    static const std::vector<RemakeChest> chests = LoadRemakeChests();

    SetUnmodifiedGameOptions();
    auto ctx = Rando::Context::GetInstance();
    ctx->SetSeed(SohUtils::Hash(seed));
    ctx->ClearItemLocations();
    // No location is excluded and no trick is enabled, so both sets are empty.
    if (Playthrough::Playthrough_Init(ctx->GetSeed(), {}, {}, chests) <= 0) {
        throw std::runtime_error("generation failed");
    }
    return { SpoilerLog(seed, chests), RemakePlan(seed, chests) };
}
