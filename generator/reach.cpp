// Answers one question about the logic: with the given items at the given locations, and every other location holding
// what the unmodified game gives there, can Link ever collect what is at the target location? Link holds every item
// given for Link's Pocket from the start.
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/3drando/fill.hpp"
#include "remake.h"
#include "setup.h"

#include <cstdio>
#include <exception>
#include <cstdlib>
#include <string>
#include <vector>

static RandomizerCheck Location(const std::string& name) {
    auto found = Rando::StaticData::locationNameToEnum.find(name);
    if (found == Rando::StaticData::locationNameToEnum.end()) {
        std::fprintf(stderr, "unknown location \"%s\"\n", name.c_str());
        std::exit(2);
    }
    return found->second;
}

static RandomizerGet Item(const std::string& name) {
    auto found = Rando::StaticData::itemNameToEnum.find(name);
    if (found == Rando::StaticData::itemNameToEnum.end()) {
        std::fprintf(stderr, "unknown item \"%s\"\n", name.c_str());
        std::exit(2);
    }
    return found->second;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc % 2 != 1) {
        std::fprintf(stderr, "usage: %s <settings> <location> [<location> <item>]...\n", argv[0]);
        return 2;
    }

    InitializeRandomizer();
    std::vector<RemakeChest> chests;
    try {
        SetOptions(ParseSeedSettings(nlohmann::json::parse(argv[1])).lens);
        chests = LoadRemakeChests();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    auto ctx = Rando::Context::GetInstance();
    ctx->FinalizeSettings({}, {});
    for (const RemakeChest* chest : SetUpWorld(chests)) {
        ctx->GetItemLocation(chest->location)->SetPlacedItem(chest->sohItem);
    }
    std::vector<RandomizerGet> pocket;
    for (int i = 3; i < argc; i += 2) {
        RandomizerCheck location = Location(argv[i]);
        if (location == RC_LINKS_POCKET) {
            pocket.push_back(Item(argv[i + 1]));
        } else {
            ctx->GetItemLocation(location)->SetPlacedItem(Item(argv[i + 1]));
        }
    }
    RandomizerCheck target = Location(argv[2]);

    ctx->GetLogic()->Reset();
    for (RandomizerGet item : pocket) {
        Rando::StaticData::RetrieveItem(item).ApplyEffect();
    }
    ReachabilitySearch({});
    std::puts(ctx->GetItemLocation(target)->IsAddedToPool() ? "yes" : "no");
    return 0;
}
