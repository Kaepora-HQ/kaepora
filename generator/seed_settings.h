#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

inline constexpr std::string_view ShuffledDungeonsSetting = "Shuffled dungeons";
inline constexpr std::string_view KeysSetting = "Small and boss keys";
inline constexpr std::string_view MapsAndCompassesSetting = "Maps and compasses";
inline constexpr std::string_view ForestSetting = "Kokiri Forest";
inline constexpr std::string_view DoorOfTimeSetting = "Door of Time";
inline constexpr std::string_view BridgeSetting = "Rainbow Bridge";
inline constexpr std::string_view TrialsSetting = "Ganon's trials";
inline constexpr std::string_view SkipSetting = "Skip";
inline constexpr std::string_view StartWithSetting = "Start with";
inline constexpr std::string_view ShopsSetting = "Shop goods";
inline constexpr std::string_view ShopPricesSetting = "Shop prices";
inline constexpr std::string_view ShopPriceSetting = "Shop price";
inline constexpr std::string_view ShopPriceRangeSetting = "Shop price range";
inline constexpr std::string_view GiftsSetting = "Gifts";
inline constexpr std::string_view RewardsSetting = "Dungeon rewards";
inline constexpr std::string_view SkulltulasSetting = "Gold Skulltulas";
inline constexpr std::string_view SongsSetting = "Songs";
inline constexpr std::string_view LooseItemsSetting = "Loose items";
inline constexpr std::string_view LensSetting = "Lens of Truth";
inline constexpr std::string_view GossipStonesSetting = "Gossip stones";
inline constexpr std::string_view HintsFromCharactersSetting = "Hints from characters";

struct Dungeon {
    std::string_view name;
    std::string_view folder;

    auto operator<=>(const Dungeon&) const = default;
};

inline constexpr Dungeon Dungeons[] = {
    { "Deku Tree", "InsideTheDekuTree" },
    { "Dodongo's Cavern", "DodongoCavern" },
    { "Jabu-Jabu's Belly", "InsideJabuJabuBelly" },
    { "Forest Temple", "ForestTemple" },
    { "Fire Temple", "FireTemple" },
    { "Water Temple", "WaterTemple" },
    { "Shadow Temple", "ShadowTemple" },
    { "Spirit Temple", "SpiritTemple" },
    { "Bottom of the Well", "BottomOfTheWell" },
    { "Ice Cavern", "IceCavern" },
    { "Gerudo Training Ground", "GerudoTrainingGround" },
    { "Ganon's Castle", "InsideGanonCastle" },
};

template <typename Value, size_t Size>
std::string_view Words(const std::pair<Value, std::string_view> (&choices)[Size], Value value) {
    return std::ranges::find(choices, value, &std::pair<Value, std::string_view>::first)->second;
}

enum class DungeonItems { AnywhereInTheirDungeon, InTheirOwnChests, GivenAtTheStart };
inline constexpr std::pair<DungeonItems, std::string_view> KeysChoices[] = {
    { DungeonItems::AnywhereInTheirDungeon, "Anywhere in their dungeon" },
    { DungeonItems::InTheirOwnChests, "In their own chests" },
    { DungeonItems::GivenAtTheStart, "Not needed" },
};
inline constexpr std::pair<DungeonItems, std::string_view> MapsAndCompassesChoices[] = {
    { DungeonItems::AnywhereInTheirDungeon, "Anywhere in their dungeon" },
    { DungeonItems::InTheirOwnChests, "In their own chests" },
    { DungeonItems::GivenAtTheStart, "Given at the start" },
};

enum class Forest { Closed, ClosedDeku, Open };
inline constexpr std::pair<Forest, std::string_view> ForestChoices[] = {
    { Forest::Closed, "Closed" },
    { Forest::ClosedDeku, "Closed Deku" },
    { Forest::Open, "Open" },
};

enum class DoorOfTime { Closed, Open };
inline constexpr std::pair<DoorOfTime, std::string_view> DoorOfTimeChoices[] = {
    { DoorOfTime::Closed, "Closed" },
    { DoorOfTime::Open, "Open" },
};

enum class Bridge { AfterTheLightArrows, AlwaysOpen };
inline constexpr std::pair<Bridge, std::string_view> BridgeChoices[] = {
    { Bridge::AfterTheLightArrows, "After the Light Arrows" },
    { Bridge::AlwaysOpen, "Always open" },
};

enum class Trials { AllSix, None };
inline constexpr std::pair<Trials, std::string_view> TrialsChoices[] = {
    { Trials::AllSix, "All six" },
    { Trials::None, "None" },
};

enum class Skip { OpeningMovie, SkippableScenes, NewAreaScenes, TutorialMessages, SariasGreeting };
inline constexpr std::pair<Skip, std::string_view> SkipChoices[] = {
    { Skip::OpeningMovie, "Opening movie" },
    { Skip::SkippableScenes, "Player skippable scenes" },
    { Skip::NewAreaScenes, "New area scenes" },
    { Skip::TutorialMessages, "Tutorial messages" },
    { Skip::SariasGreeting, "Saria's greeting" },
};

// The settings of released seeds name each of the first two skips under a setting of its own, "Skipped" or "Played".
inline constexpr std::pair<std::string_view, Skip> ReleasedSkipSettings[] = {
    { "Opening", Skip::OpeningMovie },
    { "Cutscenes", Skip::SkippableScenes },
};

// A step of an item a seed can start with. An item comes in one step or in several, and a step includes the ones
// before it. Each step is one find: the actor whose records a new game holds after it, and the item that a place of
// the game names for that step, whose copy a seed that starts with the step leaves out of the pool. The names are the
// game's English ones.
struct StartStep {
    std::string_view item;
    std::string_view choice; // the words of a step among several; empty for an item of one step
    std::string_view name;   // in the settings, the spoiler log and the seed's details
    std::string_view actor;
    std::string_view copy; // empty where no copy leaves the pool

    auto operator<=>(const StartStep&) const = default;
};

inline constexpr StartStep StartSteps[] = {
    { "Kokiri Sword", "", "Kokiri Sword", "Weapon_Sword_Kokiri", "Item_Sword_Kokiri" },
    // SoH's pool keeps its Deku Shields whatever the start.
    { "Deku Shield", "", "Deku Shield", "Weapon_Shield_Deku", "" },
    { "Hylian Shield", "", "Hylian Shield", "Weapon_Shield_Hylian", "TBox_Shield_Hylian" },
    { "Mirror Shield", "", "Mirror Shield", "Weapon_Shield_Mirror", "Item_Shield_Mirror" },
    { "Iron Boots", "", "Iron Boots", "Armor_Adult_HeavyBoot", "Item_HeavyBoot" },
    { "Hover Boots", "", "Hover Boots", "Armor_Adult_HoverBoot", "Item_HoverBoot" },
    { "Goron Tunic", "", "Goron Tunic", "Armor_Adult_Goron", "Obj_Armor_Goron" },
    { "Zora Tunic", "", "Zora Tunic", "Armor_Adult_Zora", "Obj_Armor_Zora" },
    { "Megaton Hammer", "", "Megaton Hammer", "Item_Hammer", "Item_Hammer" },
    { "Boomerang", "", "Boomerang", "Item_Boomerang", "Item_Boomerang" },
    { "Fairy Slingshot", "30 seeds", "Fairy Slingshot (30 seeds)", "Weapon_Range_SlingShot", "Item_Range_SlingShot" },
    { "Fairy Slingshot", "40 seeds", "Fairy Slingshot (40 seeds)", "Item_BulletBag_Medium", "Item_BulletBag_Medium" },
    { "Fairy Slingshot", "50 seeds", "Fairy Slingshot (50 seeds)", "Item_BulletBag_Large", "Item_BulletBag_Large" },
    { "Fairy Bow", "30 arrows", "Fairy Bow (30 arrows)", "Weapon_Range_Bow", "Item_Range_Bow" },
    { "Fairy Bow", "40 arrows", "Fairy Bow (40 arrows)", "Item_Quiver_Medium", "Item_Quiver_Medium" },
    { "Fairy Bow", "50 arrows", "Fairy Bow (50 arrows)", "Item_Quiver_Large", "Item_Quiver_Large" },
    { "Bomb Bag", "20 bombs", "Bomb Bag (20 bombs)", "Item_BombBag_Small", "Item_BombBag_Small" },
    { "Bomb Bag", "30 bombs", "Bomb Bag (30 bombs)", "Item_BombBag_Medium", "Item_BombBag_Medium" },
    { "Bomb Bag", "40 bombs", "Bomb Bag (40 bombs)", "Item_BombBag_Large", "Item_BombBag_Large" },
    { "Hookshot", "Hookshot", "Hookshot", "Item_Hookshot", "Item_Hookshot" },
    { "Hookshot", "Longshot", "Longshot", "Item_Longshot", "Item_Longshot" },
    { "Strength", "Goron Bracelet", "Goron Bracelet", "Armor_Child_Gauntlet_Goron", "Item_Gauntlet_Goron" },
    { "Strength", "Silver Gauntlets", "Silver Gauntlets", "Armor_Adult_Gauntlet_Silver", "Item_Gauntlet_Silver" },
    { "Strength", "Golden Gauntlets", "Golden Gauntlets", "Armor_Adult_Gauntlet_Gold", "Item_Gauntlet_Gold" },
    { "Scale", "Silver Scale", "Silver Scale", "Item_Scale_Silver", "Item_Scale_Silver" },
    { "Scale", "Golden Scale", "Golden Scale", "Item_Scale_Gold", "Item_Scale_Gold" },
    { "Wallet", "Adult's Wallet", "Adult's Wallet", "Item_Wallet_Medium", "Item_Wallet_Medium" },
    { "Wallet", "Giant's Wallet", "Giant's Wallet", "Item_Wallet_Large", "Item_Wallet_Large" },
    // The first stick and the first nut are ammunition, which a place may give again.
    { "Deku Sticks", "10 sticks", "Deku Sticks (10)", "Item_DekuStick", "" },
    { "Deku Sticks", "20 sticks", "Deku Sticks (20)", "Item_StickBag_Medium", "Item_StickBag_Medium" },
    { "Deku Sticks", "30 sticks", "Deku Sticks (30)", "Item_StickBag_Large", "Item_StickBag_Large" },
    { "Deku Nuts", "20 nuts", "Deku Nuts (20)", "Item_DekuNut", "" },
    { "Deku Nuts", "30 nuts", "Deku Nuts (30)", "Item_NutBag_Medium", "Item_NutBag_Medium" },
    { "Deku Nuts", "40 nuts", "Deku Nuts (40)", "Item_NutBag_Large", "Item_NutBag_Large" },
    { "Ocarina", "Fairy Ocarina", "Fairy Ocarina", "Item_Ocarina_Fairy", "Item_Ocarina_Fairy" },
};

// A count's value is the number of goods it shuffles in each shop. As in SoH's shopsanity, the fill puts the game's
// goods SoH keeps on the slots a count leaves, so a count of none moves the shops' goods among the shops.
enum class ShopCount { AmongTheShops, OnePerShop, TwoPerShop, ThreePerShop, FourPerShop, FivePerShop, SixPerShop,
                       SevenPerShop, RandomPerShop };
inline constexpr std::pair<ShopCount, std::string_view> ShopCountChoices[] = {
    { ShopCount::AmongTheShops, "Goods shuffled among the shops" },
    { ShopCount::OnePerShop, "One per shop shuffled" },
    { ShopCount::TwoPerShop, "Two per shop shuffled" },
    { ShopCount::ThreePerShop, "Three per shop shuffled" },
    { ShopCount::FourPerShop, "Four per shop shuffled" },
    { ShopCount::FivePerShop, "Five per shop shuffled" },
    { ShopCount::SixPerShop, "Six per shop shuffled" },
    { ShopCount::SevenPerShop, "Seven per shop shuffled" },
    { ShopCount::RandomPerShop, "A random number per shop" },
};
// "Shop goods" at this leaves every shop's goods where the game has them.
inline constexpr std::string_view VanillaShops = "Vanilla";

// SoH sets one price, and each end of a range, from 0 to 995 rupees in steps of 5.
inline constexpr int MostShopPrice = 995;
inline constexpr int ShopPriceStep = 5;

// Each slot's own price in the remake.
struct VanillaPrices {
    bool operator==(const VanillaPrices&) const = default;
};
struct CheapPrices {
    bool operator==(const CheapPrices&) const = default;
};
struct BalancedPrices {
    bool operator==(const BalancedPrices&) const = default;
};
struct OnePrice {
    int price = 100;

    bool operator==(const OnePrice&) const = default;
};
struct PricesBetween {
    int lowest = 20;
    int highest = 200;

    bool operator==(const PricesBetween&) const = default;
};
using ShopPrices = std::variant<VanillaPrices, CheapPrices, BalancedPrices, OnePrice, PricesBetween>;
// Each kind of price with its default numbers.
inline constexpr std::pair<ShopPrices, std::string_view> ShopPricesChoices[] = {
    { VanillaPrices{}, "Vanilla" },
    { CheapPrices{}, "Cheap" },
    { BalancedPrices{}, "Balanced" },
    { OnePrice{}, "One price" },
    { PricesBetween{}, "Between two prices" },
};
inline constexpr ShopPrices DefaultShopPrices = CheapPrices{};

// The choice of the prices' kind.
inline const std::pair<ShopPrices, std::string_view>& ShopPricesChoice(const ShopPrices& prices) {
    return *std::ranges::find(ShopPricesChoices, prices.index(),
                              [](const auto& choice) { return choice.first.index(); });
}

struct ShuffledShops {
    ShopCount count;
    ShopPrices prices = DefaultShopPrices; // the shuffled slots' prices, so none while the count shuffles no slot

    bool operator==(const ShuffledShops&) const = default;
};

// Whether any slot takes a price of "Shop prices": the game's goods sell at their own.
inline bool ShopPricesApply(const std::optional<ShuffledShops>& shops) {
    return shops && shops->count != ShopCount::AmongTheShops;
}

inline std::string_view ShopsWords(const std::optional<ShuffledShops>& shops) {
    return shops ? Words(ShopCountChoices, shops->count) : VanillaShops;
}

// A group of gift places a seed can shuffle: a row of the "Gifts" list, whole or by one of its choices. Each place of
// remake/gifts.tsv names its group.
struct GiftGroup {
    std::string_view row;
    std::string_view choice; // the words of a choice among several; empty for a row of one
    std::string_view name;   // in the settings and the spoiler log

    auto operator<=>(const GiftGroup&) const = default;
};

// Adult Anju's place and Biggoron's, shuffled as SoH shuffles them with its adult trade off: the Claim Check and
// Biggoron's sword join the pool, and no Pocket Egg does, so the trade chain does not start.
inline constexpr std::string_view BiggoronQuest = "Biggoron Quest";

inline constexpr GiftGroup GiftGroups[] = {
    { "Minigames", "", "Minigames" },
    { "Characters", "", "Characters" },
    { "House of Skulltula", "", "House of Skulltula" },
    { "Great Fairies", "", "Great Fairies" },
    { "Story gifts", "", "Story gifts" },
    { "Malon's Egg", "", "Malon's Egg" },
    { "Zelda's Letter", "", "Zelda's Letter" },
    { BiggoronQuest, "", BiggoronQuest },
    { "Deku Scrubs", "One Time Rewards", "Deku Scrubs (One Time Rewards)" },
};

// The groups a seed shuffles unless its settings say otherwise: SoH's default seed shuffles every group's places but
// Malon's and Zelda's, whose "Shuffle Weird Egg" starts at Vanilla and "Shuffle Zelda's Letter" off. It leaves its
// three one-time scrubs out of the game, which Kaepora cannot do, and their group holds the Deku Theater's two prizes,
// which SoH shuffles, so the group is in.
inline std::set<GiftGroup> DefaultGifts() {
    std::set<GiftGroup> groups(std::begin(GiftGroups), std::end(GiftGroups));
    std::erase_if(groups, [](const GiftGroup& group) {
        return group.name == "Malon's Egg" || group.name == "Zelda's Letter";
    });
    return groups;
}

// "Shuffled among themselves" is SoH's "End of Dungeons": the rewards go only to the places that hand one over.
enum class Rewards { Vanilla, ShuffledAmongThemselves, Shuffled };
inline constexpr std::pair<Rewards, std::string_view> RewardsChoices[] = {
    { Rewards::Vanilla, "Vanilla" },
    { Rewards::ShuffledAmongThemselves, "Shuffled among themselves" },
    { Rewards::Shuffled, "Shuffled" },
};

enum class Skulltulas { Vanilla, InDungeons, InTheOverworld, All };
inline constexpr std::pair<Skulltulas, std::string_view> SkulltulasChoices[] = {
    { Skulltulas::Vanilla, "Vanilla" },
    { Skulltulas::InDungeons, "In dungeons" },
    { Skulltulas::InTheOverworld, "In the overworld" },
    { Skulltulas::All, "All" },
};

enum class Songs { Vanilla, ShuffledAmongThemselves };
inline constexpr std::pair<Songs, std::string_view> SongsChoices[] = {
    { Songs::Vanilla, "Vanilla" },
    { Songs::ShuffledAmongThemselves, "Shuffled among themselves" },
};

enum class LooseItems { Vanilla, Shuffled };
inline constexpr std::pair<LooseItems, std::string_view> LooseItemsChoices[] = {
    { LooseItems::Vanilla, "Vanilla" },
    { LooseItems::Shuffled, "Shuffled" },
};

enum class Lens { Needed, NeverNeeded };
inline constexpr std::pair<Lens, std::string_view> LensChoices[] = {
    { Lens::Needed, "Needed" },
    { Lens::NeverNeeded, "Never needed" },
};

enum class GossipStones { GiveHints, Vanilla };
inline constexpr std::pair<GossipStones, std::string_view> GossipStonesChoices[] = {
    { GossipStones::GiveHints, "Give hints" },
    { GossipStones::Vanilla, "Vanilla" },
};

enum class HintsFromCharacters { GiveHints, Vanilla };
inline constexpr std::pair<HintsFromCharacters, std::string_view> HintsFromCharactersChoices[] = {
    { HintsFromCharacters::GiveHints, "Give hints" },
    { HintsFromCharacters::Vanilla, "Vanilla" },
};

struct SeedSettings {
    std::set<Dungeon> shuffledDungeons{ std::begin(Dungeons), std::end(Dungeons) };
    DungeonItems keys = DungeonItems::AnywhereInTheirDungeon;
    DungeonItems mapsAndCompasses = DungeonItems::AnywhereInTheirDungeon;
    Forest forest = Forest::Closed;
    DoorOfTime doorOfTime = DoorOfTime::Closed;
    Bridge bridge = Bridge::AfterTheLightArrows;
    Trials trials = Trials::AllSix;
    std::set<Skip> skips{ std::views::keys(SkipChoices).begin(), std::views::keys(SkipChoices).end() };
    std::set<StartStep> startWith;      // at most one step of each item
    std::optional<ShuffledShops> shops; // none when "Shop goods" is "Vanilla"
    std::set<GiftGroup> gifts = DefaultGifts(); // at most one choice of each row
    Rewards rewards = Rewards::ShuffledAmongThemselves;
    Skulltulas skulltulas = Skulltulas::Vanilla;
    Songs songs = Songs::ShuffledAmongThemselves;
    LooseItems looseItems = LooseItems::Shuffled;
    Lens lens = Lens::Needed;
    GossipStones gossipStones = GossipStones::GiveHints;
    HintsFromCharacters hintsFromCharacters = HintsFromCharacters::GiveHints;

    bool operator==(const SeedSettings&) const = default;
};

// The steps the settings start with, in the list's order.
std::vector<StartStep> StartWithInOrder(const SeedSettings& settings);

// The step's item's steps up to it, in order: the finds that bring a new game to the step.
std::vector<StartStep> StepsTo(const StartStep& step);

nlohmann::ordered_json ToJson(const SeedSettings& settings);

// The settings from an object of the spoiler log's form. A setting the object leaves out has its default. Throws on
// an unknown setting or value.
SeedSettings ParseSeedSettings(const nlohmann::json& json);
