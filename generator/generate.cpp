#include "generate.h"

#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/3drando/playthrough.hpp"
#include "soh/util.h"
#include "hints.h"
#include "remake.h"
#include "setup.h"
#include "version.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <iterator>
#include <stdexcept>

static void AddPlacement(nlohmann::ordered_json& placements, RandomizerCheck location) {
    placements[Rando::StaticData::GetLocation(location)->GetName()] =
        Rando::Context::GetInstance()->GetItemLocation(location)->GetPlacedItemName().GetEnglish();
}

static std::string SpoilerLog(const std::string& seed, const SeedSettings& settings,
                              const std::vector<RemakeChest>& chests, const std::vector<RemakeShopSlot>& shops,
                              const std::vector<StoneHint>& hints) {
    nlohmann::ordered_json log = { { "kaepora", KAEPORA_VERSION },
                                   { "seed", seed },
                                   { "settings", ToJson(settings) },
                                   { "locations", nlohmann::ordered_json::object() },
                                   { "playthrough", nlohmann::ordered_json::object() },
                                   { "hints", nlohmann::ordered_json::object() } };
    for (const RemakeChest& chest : chests) {
        if (chest.shuffled) {
            AddPlacement(log["locations"], chest.location);
        }
    }
    auto ctx = Rando::Context::GetInstance();
    for (const RemakeShopSlot& slot : shops) {
        const Rando::ItemLocation* location = ctx->GetItemLocation(slot.location);
        log["locations"][Rando::StaticData::GetLocation(slot.location)->GetName()] = {
            { "item", location->GetPlacedItemName().GetEnglish() }, { "price", location->GetPrice() }
        };
    }
    for (const std::vector<RandomizerCheck>& sphere : ctx->playthroughLocations) {
        nlohmann::ordered_json steps = nlohmann::ordered_json::object();
        for (RandomizerCheck location : sphere) {
            // Every token stays where the game has it, and any ten buy the same Skulltula House reward, so the route
            // names no token.
            if (ctx->GetItemLocation(location)->GetPlacedRandomizerGet() != RG_GOLD_SKULLTULA_TOKEN) {
                AddPlacement(steps, location);
            }
        }
        if (!steps.empty()) {
            log["playthrough"][std::format("sphere {:02}", log["playthrough"].size())] = std::move(steps);
        }
    }
    for (const StoneHint& hint : hints) {
        log["hints"][Rando::StaticData::GetLocation(hint.stone.location)->GetName()] = hint.plain;
    }
    return log.dump(4) + "\n";
}

static bool KeptInPlace(const RemakeChest& chest, const SeedSettings& settings) {
    // The prize always asks for the Lens, so a seed that never asks for the Lens keeps it as the game has it.
    if (chest.location == RC_MARKET_TREASURE_CHEST_GAME_REWARD && settings.lens == Lens::NeverNeeded) {
        return true;
    }
    const auto dungeon = std::ranges::find(Dungeons, chest.Folder(), &Dungeon::folder);
    if (dungeon != std::end(Dungeons) && !settings.shuffledDungeons.contains(*dungeon)) {
        return true;
    }
    switch (Rando::StaticData::RetrieveItem(chest.sohItem).GetItemType()) {
        case ITEMTYPE_SMALLKEY:
        case ITEMTYPE_BOSSKEY:
            return settings.keys == DungeonItems::InTheirOwnChests;
        case ITEMTYPE_MAP:
        case ITEMTYPE_COMPASS:
            return settings.mapsAndCompasses == DungeonItems::InTheirOwnChests;
        default:
            return false;
    }
}

// SoH's shopsanity takes a shop's items in the order 7, 5, 8, 6, 3, 1, 4, 2, so four per shop are items 5 to 8.
static bool Shuffled(const RemakeShopSlot& slot, const SeedSettings& settings) {
    return settings.shops == Shops::FourPerShopShuffled && slot.number >= 5;
}

static std::vector<RemakeShopSlot> ShuffledShops(const std::vector<RemakeShopSlot>& table,
                                                 const SeedSettings& settings) {
    std::vector<RemakeShopSlot> shops;
    std::ranges::copy_if(table, std::back_inserter(shops),
                         [&](const RemakeShopSlot& slot) { return Shuffled(slot, settings); });
    return shops;
}

GeneratedSeed Generate(const std::string& text, const SeedSettings& settings) {
    // SoH's start-up runs once per process. The tables are built in, so a table that fails to load fails every call.
    [[maybe_unused]] static const bool started = (InitializeRandomizer(), true);
    static const std::vector<RemakeChest> table = LoadRemakeChests();
    static const std::vector<RemakeShopSlot> shopTable = LoadRemakeShops();
    static const RemakeHintTables hintTables =
        LoadRemakeHintTables(table, ShuffledShops(shopTable, { .shops = Shops::FourPerShopShuffled }));

    std::string seed = text;
    std::ranges::transform(seed, seed.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::vector<RemakeChest> chests = table;
    for (RemakeChest& chest : chests) {
        chest.shuffled = chest.shuffled && !KeptInPlace(chest, settings);
    }
    const std::vector<RemakeShopSlot> shops = ShuffledShops(shopTable, settings);

    SetOptions(settings);
    auto ctx = Rando::Context::GetInstance();
    ctx->ClearItemLocations();
    // No location is excluded.
    if (Playthrough::Playthrough_Init(SohUtils::Hash(seed), {}, enabledTricks, chests, shops) <= 0) {
        throw std::runtime_error("generation failed");
    }
    const std::map<RandomizerCheck, std::string> items = PlacedRemakeItems(chests, shops);
    const std::vector<StoneHint> hints =
        settings.gossipStones == GossipStones::GiveHints ? PickHints(hintTables, items) : std::vector<StoneHint>{};
    return { SpoilerLog(seed, settings, chests, shops, hints), RemakePlan(seed, chests, shops, settings, items, hints) };
}
