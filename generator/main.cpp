// Generates one seed and writes its spoiler log.
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/settings.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/3drando/playthrough.hpp"
#include "soh/Enhancements/Lang/Lang.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/util.h"
#include "remake.h"
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <string>

// SpoilerLog_Write fills this.
extern nlohmann::ordered_json jsonData;

// Each option's callback rewrites console variables and other options, and SoH runs it when the menu adds the
// option's widget. Running them in menu order gives the option state the menu leaves behind.
static void RunMenuCallbacks(const Rando::OptionGroup& group) {
    if (group.GetContainsType() == Rando::OptionGroupType::SUBGROUP) {
        for (const Rando::OptionGroup* subGroup : group.GetSubGroups()) {
            RunMenuCallbacks(*subGroup);
        }
    } else {
        for (Rando::Option* option : group.GetOptions()) {
            option->RunCallback();
        }
    }
}

// The randomizer steps of SoH's start-up: OTRGlobals::Initialize, then the randomizer menu
// (SohMenuRandomizer.cpp), then ShipInit::InitAll. InitAll would also register in-game hooks, so only the
// location registrations run here.
static void Initialize() {
    Lang::LoadLangs();
    OTRGlobals::Instance = new OTRGlobals();
    auto ctx = Rando::Context::CreateInstance();
    OTRGlobals::Instance->gRandoContext = ctx;
    OTRGlobals::Instance->gRandomizer = std::make_shared<Randomizer>();
    ctx->InitStaticData();
    Rando::Settings::GetInstance()->AssignContext(ctx);
    Rando::StaticData::InitItemTable();

    auto settings = Rando::Settings::GetInstance();
    settings->CreateOptions();
    for (auto group : { RSG_MENU_SIDEBAR_LOGIC_ACCESS, RSG_MENU_SIDEBAR_DUNGEONS, RSG_MENU_SIDEBAR_SHUFFLES,
                        RSG_MENU_SIDEBAR_HINTS_TRAPS }) {
        RunMenuCallbacks(settings->GetOptionGroup(group));
    }

    Rando::StaticData::RegisterSongLocations();
    Rando::StaticData::RegisterBeehiveLocations();
    Rando::StaticData::RegisterCowLocations();
    Rando::StaticData::RegisterFishLocations();
    Rando::StaticData::RegisterFairyLocations();
    Rando::StaticData::RegisterPotLocations();
    Rando::StaticData::RegisterFreestandingLocations();
    Rando::StaticData::RegisterSilverLocations();
    Rando::StaticData::RegisterGrassLocations();
    Rando::StaticData::RegisterCrateLocations();
    Rando::StaticData::RegisterRockLocations();
    Rando::StaticData::RegisterTreeLocations();
    Rando::StaticData::RegisterSignLocations();
    Rando::StaticData::RegisterWonderItemLocations();
    Rando::StaticData::RegisterBeggarLocations();
    Rando::StaticData::RegisterFishermansHatLocation();
    Rando::StaticData::RegisterIcicleLocations();
    Rando::StaticData::RegisterRedIceLocations();
    Rando::StaticData::RegisterFireGoronLocations();
    Rando::StaticData::InitHashMaps();
    ctx->AddExcludedOptions();
}

// No location is excluded and no trick is enabled, so both sets are empty.
static bool Generate(const std::string& seed, const std::vector<RemakeChest>& chests) {
    Rando::Settings::GetInstance()->SetAllToContext();
    auto ctx = Rando::Context::GetInstance();
    ctx->SetSeedString(seed);
    ctx->SetSeed(SohUtils::Hash(seed));
    ctx->ClearItemLocations();
    return Playthrough::Playthrough_Init(ctx->GetSeed(), {}, {}, chests) > 0;
}

int main(int argc, char** argv) {
    if (argc != 3 || argv[1][0] == '\0') {
        std::fprintf(stderr, "usage: %s <seed> <spoiler.json>\n", argv[0]);
        return 2;
    }

    Initialize();
    const std::vector<RemakeChest> chests = LoadRemakeChests();
    if (!Generate(argv[1], chests)) {
        std::fprintf(stderr, "generation failed\n");
        return 1;
    }
    std::ofstream(argv[2]) << jsonData.dump(4) << std::endl;
    return 0;
}
