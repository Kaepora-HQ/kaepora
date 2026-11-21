#include "generate.h"

#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/rng.h"
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
#include <set>
#include <stdexcept>

static void AddPlacement(nlohmann::ordered_json& placements, RandomizerCheck location) {
    placements[Rando::StaticData::GetLocation(location)->GetName()] =
        Rando::Context::GetInstance()->GetItemLocation(location)->GetPlacedItemName().GetEnglish();
}

static std::string SpoilerLog(const std::string& seed, const SeedSettings& settings,
                              const std::vector<RemakeChest>& chests, const std::vector<RemakeShopSlot>& shops,
                              const std::vector<RemakeGift>& gifts, const std::vector<RemakeSkulltula>& skulltulas,
                              const std::vector<RemakeSongPlace>& songs, const std::vector<StoneHint>& hints,
                              const std::vector<CharacterHintText>& characterHints) {
    nlohmann::ordered_json log = { { "kaepora", KAEPORA_VERSION },
                                   { "seed", seed },
                                   { "settings", ToJson(settings) },
                                   { "locations", nlohmann::ordered_json::object() },
                                   { "playthrough", nlohmann::ordered_json::object() },
                                   { "hints", nlohmann::ordered_json::object() },
                                   { "hints from characters", nlohmann::ordered_json::object() } };
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
    for (const RemakeGift& gift : gifts) {
        AddPlacement(log["locations"], gift.location);
    }
    for (const RemakeSkulltula& skulltula : skulltulas) {
        AddPlacement(log["locations"], skulltula.location);
    }
    for (const RemakeSongPlace& place : songs) {
        AddPlacement(log["locations"], place.location);
    }
    for (const std::vector<RandomizerCheck>& sphere : ctx->playthroughLocations) {
        nlohmann::ordered_json steps = nlohmann::ordered_json::object();
        for (RandomizerCheck location : sphere) {
            // Any ten tokens buy the same Skulltula House reward, so a token where the game has it tells a player
            // nothing.
            if (ctx->GetItemLocation(location)->GetPlacedRandomizerGet() != RG_GOLD_SKULLTULA_TOKEN ||
                log["locations"].contains(Rando::StaticData::GetLocation(location)->GetName())) {
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
    for (const CharacterHintText& hint : characterHints) {
        log["hints from characters"][Rando::StaticData::GetLocation(hint.hint)->GetName()].push_back(hint.plain);
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

// SoH's shopsanity takes a shop's items in the order 7, 5, 8, 6, 3, 1, 4, 2. Its sixth is the Kokiri Shop's Deku Shield,
// and SoH's logic counts only a shield Link can buy, so a count stops at five.
static constexpr int ShopsanityOrder[] = { 7, 5, 8, 6, 3 };

// The slots a count shuffles, in table order: the first items of each shop in SoH's order, as many as the count.
// A random number per shop draws each shop's count, one to five, as the table reaches its item 1.
static std::vector<RemakeShopSlot> ShuffledSlots(const std::vector<RemakeShopSlot>& table, ShopCount count) {
    std::vector<RemakeShopSlot> shuffled;
    int shopCount = 0;
    for (const RemakeShopSlot& slot : table) {
        if (slot.number == 1) {
            shopCount = count == ShopCount::RandomPerShop ? static_cast<int>(Random(1, std::size(ShopsanityOrder) + 1))
                                                          : static_cast<int>(count);
        }
        if (std::ranges::find(ShopsanityOrder, slot.number) - std::begin(ShopsanityOrder) < shopCount) {
            shuffled.push_back(slot);
        }
    }
    return shuffled;
}

static bool Shuffled(const RemakeSkulltula& skulltula, const SeedSettings& settings) {
    const auto dungeon = std::ranges::find(Dungeons, skulltula.actor.Folder(), &Dungeon::folder);
    if (dungeon != std::end(Dungeons) && !settings.shuffledDungeons.contains(*dungeon)) {
        return false;
    }
    switch (settings.skulltulas) {
        case Skulltulas::Vanilla:
            return false;
        case Skulltulas::InDungeons:
            return Rando::StaticData::GetLocation(skulltula.location)->IsDungeon();
        case Skulltulas::InTheOverworld:
            return Rando::StaticData::GetLocation(skulltula.location)->IsOverworld();
        case Skulltulas::All:
            return true;
    }
    return false;
}

// The dungeon of each boss whose place hands over a stone or a medallion. Rauru's place is in none.
static constexpr std::pair<RandomizerCheck, std::string_view> BossDungeons[] = {
    { RC_QUEEN_GOHMA, "Deku Tree" },     { RC_KING_DODONGO, "Dodongo's Cavern" }, { RC_BARINADE, "Jabu-Jabu's Belly" },
    { RC_PHANTOM_GANON, "Forest Temple" }, { RC_VOLVAGIA, "Fire Temple" },         { RC_MORPHA, "Water Temple" },
    { RC_BONGO_BONGO, "Shadow Temple" }, { RC_TWINROVA, "Spirit Temple" },
};

// A dungeon left out of the shuffle keeps what its boss hands over, as it keeps its chests.
static bool KeptInPlace(const RemakeGift& reward, const SeedSettings& settings) {
    const auto boss = std::ranges::find(BossDungeons, reward.location, &std::pair<RandomizerCheck, std::string_view>::first);
    return boss != std::end(BossDungeons) &&
           !settings.shuffledDungeons.contains(*std::ranges::find(Dungeons, boss->second, &Dungeon::name));
}

template <typename T> static std::vector<T> Joined(std::vector<T> first, const std::vector<T>& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

template <typename Place> static void GiveFiller(Place& place) {
    place.item = std::string(FillerName);
    place.sohItem = Filler;
}

GeneratedSeed Generate(const std::string& text, const SeedSettings& settings) {
    // SoH's start-up runs once per process. The tables are built in, so a table that fails to load fails every call.
    [[maybe_unused]] static const bool started = (InitializeRandomizer(), true);
    static const std::vector<RemakeChest> table = LoadRemakeChests();
    static const std::vector<RemakeChest> looseItemTable = LoadRemakeLooseItems();
    static const std::vector<RemakeShopSlot> shopTable = LoadRemakeShops();
    static const std::vector<RemakeGift> giftTable = LoadRemakeGifts(table);
    static const std::vector<RemakeGift> rewardTable = LoadRemakeRewards(table);
    // The fill puts no stone or medallion in a shop slot.
    [[maybe_unused]] static const bool shopGoodsChecked =
        (CheckShopGoods(Joined(table, looseItemTable), giftTable), true);
    static const std::vector<RemakeSkulltula> skulltulaTable = LoadRemakeSkulltulas();
    static const std::vector<RemakeSongPlace> songTable = LoadRemakeSongs();
    static const RemakeHintTables hintTables = LoadRemakeHintTables(Joined(table, looseItemTable),
                                                                    ShuffledSlots(shopTable, ShopCount::FivePerShop),
                                                                    Joined(giftTable, rewardTable), skulltulaTable,
                                                                    songTable);

    std::string seed = text;
    std::ranges::transform(seed, seed.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::vector<RemakeChest> chests = table;
    for (RemakeChest& chest : chests) {
        chest.shuffled = chest.shuffled && !KeptInPlace(chest, settings);
    }
    // The places that hand over a stone or a medallion give through an event flow's call, which the patcher writes as
    // it writes a gift's.
    std::vector<RemakeGift> gifts = settings.gifts == Gifts::Shuffled ? giftTable : std::vector<RemakeGift>{};
    if (settings.rewards == Rewards::Shuffled) {
        std::ranges::copy_if(rewardTable, std::back_inserter(gifts),
                             [&](const RemakeGift& reward) { return !KeptInPlace(reward, settings); });
    }
    // SoH's pool puts junk in place of a started item. A kept place that names a started step gives it again, which
    // adds nothing, so it is excluded: the logic counts junk there.
    for (const std::string_view copy : StartedCopies(settings)) {
        const auto names = [&](const auto& place) { return place.item == copy; };
        if (const auto chest =
                std::ranges::find_if(chests, [&](const RemakeChest& chest) { return chest.shuffled && names(chest); });
            chest != chests.end()) {
            GiveFiller(*chest);
        } else if (const auto gift = std::ranges::find_if(gifts, names); gift != gifts.end()) {
            GiveFiller(*gift);
        }
    }
    std::vector<RemakeSkulltula> skulltulas;
    std::ranges::copy_if(skulltulaTable, std::back_inserter(skulltulas),
                         [&](const RemakeSkulltula& skulltula) { return Shuffled(skulltula, settings); });
    std::vector<RemakeChest> looseItems;
    if (settings.looseItems == LooseItems::Shuffled) {
        std::ranges::copy_if(looseItemTable, std::back_inserter(looseItems),
                             [&](const RemakeChest& looseItem) { return !KeptInPlace(looseItem, settings); });
    }
    const std::vector<RemakeChest> locations = Joined(chests, looseItems);

    SetOptions(settings);
    // The seed alone starts the random numbers. Settings that resolve at random draw first, so placements depend on
    // the seed and on what generation does, not on how the options are listed.
    Random_Init(SohUtils::Hash(seed));
    const std::vector<RemakeShopSlot> shops =
        settings.shops ? ShuffledSlots(shopTable, settings.shops->count) : std::vector<RemakeShopSlot>{};
    const std::vector<RemakeSongPlace> songs =
        settings.songs == Songs::ShuffledAmongThemselves ? songTable : std::vector<RemakeSongPlace>{};
    auto ctx = Rando::Context::GetInstance();
    ctx->ClearItemLocations();
    if (Playthrough::Playthrough_Init(GivenAgain(settings, chests, giftTable), enabledTricks, chests, looseItems, shops,
                                      gifts, skulltulas, songs) <= 0) {
        throw std::runtime_error("generation failed");
    }
    const std::map<RandomizerCheck, std::string> items =
        PlacedRemakeItems(locations, shops, gifts, skulltulas, songs);
    const std::vector<CharacterHintText> characterHints = settings.hintsFromCharacters == HintsFromCharacters::GiveHints
                                                              ? CharacterHints(hintTables, items, settings)
                                                              : std::vector<CharacterHintText>{};
    std::set<RandomizerCheck> named;
    std::vector<MessageText> messages = PrizeLines(hintTables, items);
    for (const CharacterHintText& hint : characterHints) {
        named.insert(hint.place);
        messages.push_back(hint.message);
    }
    const std::vector<StoneHint> hints = settings.gossipStones == GossipStones::GiveHints
                                             ? PickHints(hintTables, items, named)
                                             : std::vector<StoneHint>{};
    return { SpoilerLog(seed, settings, locations, shops, gifts, skulltulas, songs, hints, characterHints),
             RemakePlan(seed, chests, looseItems, shops, gifts, skulltulas, songs, settings, items, hints, messages) };
}
