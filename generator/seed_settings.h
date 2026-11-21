#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

inline constexpr std::string_view ShuffledDungeonsSetting = "Shuffled dungeons";
inline constexpr std::string_view KeysSetting = "Small and boss keys";
inline constexpr std::string_view MapsAndCompassesSetting = "Maps and compasses";
inline constexpr std::string_view ForestSetting = "Kokiri Forest";
inline constexpr std::string_view BridgeSetting = "Rainbow Bridge";
inline constexpr std::string_view TrialsSetting = "Ganon's trials";
inline constexpr std::string_view OpeningSetting = "Opening";
inline constexpr std::string_view CutscenesSetting = "Cutscenes";
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

enum class Opening { Skipped, Played };
inline constexpr std::pair<Opening, std::string_view> OpeningChoices[] = {
    { Opening::Skipped, "Skipped" },
    { Opening::Played, "Played" },
};

enum class Cutscenes { Skipped, Played };
inline constexpr std::pair<Cutscenes, std::string_view> CutscenesChoices[] = {
    { Cutscenes::Skipped, "Skipped" },
    { Cutscenes::Played, "Played" },
};

// An item a seed can start with: its name in the game's English text, and its actor.
struct StartItem {
    std::string_view name;
    std::string_view actor;

    auto operator<=>(const StartItem&) const = default;
};

inline constexpr StartItem StartItems[] = {
    { "Kokiri Sword", "Weapon_Sword_Kokiri" },   { "Deku Shield", "Weapon_Shield_Deku" },
    { "Hylian Shield", "Weapon_Shield_Hylian" }, { "Mirror Shield", "Weapon_Shield_Mirror" },
    { "Iron Boots", "Armor_Adult_HeavyBoot" },   { "Hover Boots", "Armor_Adult_HoverBoot" },
    { "Goron Tunic", "Armor_Adult_Goron" },      { "Zora Tunic", "Armor_Adult_Zora" },
    { "Megaton Hammer", "Item_Hammer" },         { "Boomerang", "Item_Boomerang" },
};

// A count's value is the number of goods it shuffles in each shop.
enum class ShopCount { OnePerShop = 1, TwoPerShop, ThreePerShop, FourPerShop, FivePerShop, RandomPerShop };
inline constexpr std::pair<ShopCount, std::string_view> ShopCountChoices[] = {
    { ShopCount::OnePerShop, "One per shop shuffled" },
    { ShopCount::TwoPerShop, "Two per shop shuffled" },
    { ShopCount::ThreePerShop, "Three per shop shuffled" },
    { ShopCount::FourPerShop, "Four per shop shuffled" },
    { ShopCount::FivePerShop, "Five per shop shuffled" },
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
    ShopPrices prices = DefaultShopPrices;

    bool operator==(const ShuffledShops&) const = default;
};

inline std::string_view ShopsWords(const std::optional<ShuffledShops>& shops) {
    return shops ? Words(ShopCountChoices, shops->count) : VanillaShops;
}

enum class Gifts { Vanilla, Shuffled };
inline constexpr std::pair<Gifts, std::string_view> GiftsChoices[] = {
    { Gifts::Vanilla, "Vanilla" },
    { Gifts::Shuffled, "Shuffled" },
};

enum class Rewards { Vanilla, Shuffled };
inline constexpr std::pair<Rewards, std::string_view> RewardsChoices[] = {
    { Rewards::Vanilla, "Vanilla" },
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
    Bridge bridge = Bridge::AfterTheLightArrows;
    Trials trials = Trials::AllSix;
    Opening opening = Opening::Skipped;
    Cutscenes cutscenes = Cutscenes::Skipped;
    std::set<StartItem> startWith;
    std::optional<ShuffledShops> shops; // none when "Shop goods" is "Vanilla"
    Gifts gifts = Gifts::Vanilla;
    Rewards rewards = Rewards::Vanilla;
    Skulltulas skulltulas = Skulltulas::Vanilla;
    Songs songs = Songs::Vanilla;
    LooseItems looseItems = LooseItems::Vanilla;
    Lens lens = Lens::Needed;
    GossipStones gossipStones = GossipStones::GiveHints;
    HintsFromCharacters hintsFromCharacters = HintsFromCharacters::GiveHints;

    bool operator==(const SeedSettings&) const = default;
};

// What the settings start with, in the list's order.
std::vector<StartItem> StartWithInOrder(const SeedSettings& settings);

nlohmann::ordered_json ToJson(const SeedSettings& settings);

// The settings from an object of the spoiler log's form. A setting the object leaves out has its default. Throws on
// an unknown setting or value.
SeedSettings ParseSeedSettings(const nlohmann::json& json);
