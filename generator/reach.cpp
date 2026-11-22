// Answers one question about the logic: with the given items at the given locations, and every other location holding
// what the unmodified game gives there or, where it gives a started step again, junk as in the seed, can Link ever
// collect what is at the target location? A shop slot's item may
// come as the spoiler log gives it, with the slot's price.
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/3drando/fill.hpp"
#include "generate.h"
#include "remake.h"
#include "setup.h"

#include <cstdio>
#include <exception>
#include <cstdlib>
#include <string>
#include <utility>
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
        std::fprintf(stderr, "usage: %s <settings> <location> [<location> <item or {\"item\", \"price\"}>]...\n",
                     argv[0]);
        return 2;
    }

    InitializeRandomizer();
    SeedSettings settings;
    std::vector<RemakeChest> chests;
    std::vector<GroupedGift> gifts;
    try {
        settings = ParseSeedSettings(nlohmann::json::parse(argv[1]));
        chests = LoadRemakeChests();
        gifts = LoadRemakeGifts(chests);
        SetOptions(settings, gifts);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    auto ctx = Rando::Context::GetInstance();
    ctx->FinalizeSettings(GivenAgain(settings, chests, gifts), enabledTricks);
    for (const RemakeChest* chest : SetUpWorld(chests, {}, {}, {}, {})) {
        ctx->GetItemLocation(chest->location)->SetPlacedItem(chest->sohItem);
    }
    bool pocketTaken = false;
    for (int i = 3; i < argc; i += 2) {
        const RandomizerCheck location = Location(argv[i]);
        if (location == RC_LINKS_POCKET && std::exchange(pocketTaken, true)) {
            std::fprintf(stderr, "Link's Pocket takes one item, and it was given two\n");
            return 2;
        }
        Rando::ItemLocation* placed = ctx->GetItemLocation(location);
        if (argv[i + 1][0] == '{') {
            const nlohmann::json sale = nlohmann::json::parse(argv[i + 1]);
            placed->SetPlacedItem(Item(sale.at("item").get<std::string>()));
            placed->SetCustomPrice(sale.at("price").get<uint16_t>());
        } else {
            placed->SetPlacedItem(Item(argv[i + 1]));
        }
    }
    RandomizerCheck target = Location(argv[2]);

    ctx->GetLogic()->Reset();
    ReachabilitySearch({});
    std::puts(ctx->GetItemLocation(target)->IsAddedToPool() ? "yes" : "no");
    return 0;
}
