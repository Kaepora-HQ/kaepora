#include "seed_settings.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>

template <typename Value, size_t Size>
static Value Parse(const std::pair<Value, std::string_view> (&choices)[Size], const std::string& setting,
                   const nlohmann::json& words) {
    const auto found =
        std::ranges::find(choices, words.get<std::string>(), &std::pair<Value, std::string_view>::second);
    if (found == std::end(choices)) {
        throw std::runtime_error(setting + ": unknown value " + words.dump());
    }
    return found->first;
}

static int ShopPrice(std::string_view setting, const nlohmann::json& value) {
    if (!value.is_number_integer() || value.get<std::int64_t>() < 0 || value.get<std::int64_t>() > MostShopPrice ||
        value.get<std::int64_t>() % ShopPriceStep != 0) {
        throw std::runtime_error(std::string(setting) + ": " + value.dump() +
                                 " is not a price of 0 to 995 rupees in steps of 5");
    }
    return value.get<int>();
}

static PricesBetween ShopPriceRange(const nlohmann::json& value) {
    const std::string setting(ShopPriceRangeSetting);
    if (!value.is_array() || value.size() != 2) {
        throw std::runtime_error(setting + ": " + value.dump() + " is not two prices");
    }
    const PricesBetween range{ ShopPrice(setting, value[0]), ShopPrice(setting, value[1]) };
    if (range.lowest > range.highest) {
        throw std::runtime_error(setting + ": the first number is above its second in " + value.dump());
    }
    return range;
}

static void RefuseUnless(bool applies, std::string_view setting, std::string_view when) {
    if (!applies) {
        throw std::runtime_error(std::string(setting) + ": applies only " + std::string(when));
    }
}

// The shop settings as read, by name. Only the prices in effect are kept: none while no goods are shuffled, a price
// only with one price and a range only between two prices. A left-out price takes its default.
static std::optional<ShuffledShops> ParseShops(const std::map<std::string_view, nlohmann::json>& read) {
    const auto goods = read.find(ShopsSetting);
    if (goods == read.end() || goods->second == nlohmann::json(VanillaShops)) {
        for (const std::string_view setting : std::views::keys(read)) {
            RefuseUnless(setting == ShopsSetting, setting, "while \"Shop goods\" shuffles");
        }
        return std::nullopt;
    }
    ShuffledShops shops{ .count = Parse(ShopCountChoices, std::string(ShopsSetting), goods->second) };
    if (const auto prices = read.find(ShopPricesSetting); prices != read.end()) {
        shops.prices = Parse(ShopPricesChoices, std::string(ShopPricesSetting), prices->second);
    }
    if (const auto price = read.find(ShopPriceSetting); price != read.end()) {
        OnePrice* one = std::get_if<OnePrice>(&shops.prices);
        RefuseUnless(one != nullptr, ShopPriceSetting, "with \"Shop prices\" at \"One price\"");
        one->price = ShopPrice(ShopPriceSetting, price->second);
    }
    if (const auto range = read.find(ShopPriceRangeSetting); range != read.end()) {
        PricesBetween* between = std::get_if<PricesBetween>(&shops.prices);
        RefuseUnless(between != nullptr, ShopPriceRangeSetting, "with \"Shop prices\" at \"Between two prices\"");
        *between = ShopPriceRange(range->second);
    }
    return shops;
}

nlohmann::ordered_json ToJson(const SeedSettings& settings) {
    nlohmann::ordered_json dungeons = nlohmann::ordered_json::array();
    for (const Dungeon& dungeon : Dungeons) {
        if (settings.shuffledDungeons.contains(dungeon)) {
            dungeons.push_back(dungeon.name);
        }
    }
    nlohmann::ordered_json json = { { ShuffledDungeonsSetting, dungeons },
             { KeysSetting, Words(KeysChoices, settings.keys) },
             { MapsAndCompassesSetting, Words(MapsAndCompassesChoices, settings.mapsAndCompasses) },
             { ForestSetting, Words(ForestChoices, settings.forest) },
             { BridgeSetting, Words(BridgeChoices, settings.bridge) },
             { TrialsSetting, Words(TrialsChoices, settings.trials) } };
    json[std::string(ShopsSetting)] = ShopsWords(settings.shops);
    if (settings.shops) {
        json[std::string(ShopPricesSetting)] = ShopPricesChoice(settings.shops->prices).second;
        if (const auto* one = std::get_if<OnePrice>(&settings.shops->prices)) {
            json[std::string(ShopPriceSetting)] = one->price;
        }
        if (const auto* between = std::get_if<PricesBetween>(&settings.shops->prices)) {
            json[std::string(ShopPriceRangeSetting)] = { between->lowest, between->highest };
        }
    }
    json[std::string(SkulltulasSetting)] = Words(SkulltulasChoices, settings.skulltulas);
    json[std::string(GiftsSetting)] = Words(GiftsChoices, settings.gifts);
    json[std::string(LensSetting)] = Words(LensChoices, settings.lens);
    json[std::string(GossipStonesSetting)] = Words(GossipStonesChoices, settings.gossipStones);
    json[std::string(HintsFromCharactersSetting)] = Words(HintsFromCharactersChoices, settings.hintsFromCharacters);
    return json;
}

SeedSettings ParseSeedSettings(const nlohmann::json& json) {
    static constexpr std::string_view ShopSettings[] = { ShopsSetting, ShopPricesSetting, ShopPriceSetting,
                                                         ShopPriceRangeSetting };
    SeedSettings settings;
    std::map<std::string_view, nlohmann::json> shops;
    for (const auto& [setting, value] : json.items()) {
        if (setting == ShuffledDungeonsSetting) {
            settings.shuffledDungeons.clear();
            for (const nlohmann::json& name : value) {
                const auto dungeon = std::ranges::find(Dungeons, name.get<std::string>(), &Dungeon::name);
                if (dungeon == std::end(Dungeons)) {
                    throw std::runtime_error(setting + ": unknown dungeon " + name.dump());
                }
                settings.shuffledDungeons.insert(*dungeon);
            }
        } else if (setting == KeysSetting) {
            settings.keys = Parse(KeysChoices, setting, value);
        } else if (setting == MapsAndCompassesSetting) {
            settings.mapsAndCompasses = Parse(MapsAndCompassesChoices, setting, value);
        } else if (setting == ForestSetting) {
            settings.forest = Parse(ForestChoices, setting, value);
        } else if (setting == BridgeSetting) {
            settings.bridge = Parse(BridgeChoices, setting, value);
        } else if (setting == TrialsSetting) {
            settings.trials = Parse(TrialsChoices, setting, value);
        } else if (const auto shop = std::ranges::find(ShopSettings, setting); shop != std::end(ShopSettings)) {
            shops.emplace(*shop, value);
        } else if (setting == GiftsSetting) {
            settings.gifts = Parse(GiftsChoices, setting, value);
        } else if (setting == SkulltulasSetting) {
            settings.skulltulas = Parse(SkulltulasChoices, setting, value);
        } else if (setting == LensSetting) {
            settings.lens = Parse(LensChoices, setting, value);
        } else if (setting == GossipStonesSetting) {
            settings.gossipStones = Parse(GossipStonesChoices, setting, value);
        } else if (setting == HintsFromCharactersSetting) {
            settings.hintsFromCharacters = Parse(HintsFromCharactersChoices, setting, value);
        } else {
            throw std::runtime_error("unknown setting \"" + setting + "\"");
        }
    }
    settings.shops = ParseShops(shops);
    return settings;
}
