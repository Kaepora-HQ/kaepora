#include "seed_settings.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
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

static std::set<StartStep> StartWith(const std::string& setting, const nlohmann::json& names) {
    std::set<StartStep> steps;
    for (const nlohmann::json& name : names) {
        const auto step = std::ranges::find(StartSteps, name.get<std::string>(), &StartStep::name);
        if (step == std::end(StartSteps)) {
            throw std::runtime_error(setting + ": unknown item " + name.dump());
        }
        if (const auto other = std::ranges::find(steps, step->item, &StartStep::item); other != steps.end()) {
            throw std::runtime_error(
                std::format("{}: {} and {} are steps of one item", setting, other->name, step->name));
        }
        steps.insert(*step);
    }
    return steps;
}

// The settings of released seeds name no group "Vanilla" and every group "Shuffled".
static std::set<GiftGroup> Gifts(const std::string& setting, const nlohmann::json& value) {
    if (value == nlohmann::json("Vanilla")) {
        return {};
    }
    if (value == nlohmann::json("Shuffled")) {
        return EveryGiftGroup();
    }
    if (!value.is_array()) {
        throw std::runtime_error(setting + ": unknown value " + value.dump());
    }
    std::set<GiftGroup> groups;
    for (const nlohmann::json& name : value) {
        groups.insert(Parse(GiftGroups, setting, name));
    }
    return groups;
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

std::set<GiftGroup> EveryGiftGroup() {
    const auto groups = std::views::keys(GiftGroups);
    return std::set<GiftGroup>(groups.begin(), groups.end());
}

std::vector<StartStep> StartWithInOrder(const SeedSettings& settings) {
    std::vector<StartStep> steps;
    std::ranges::copy_if(StartSteps, std::back_inserter(steps),
                         [&](const StartStep& step) { return settings.startWith.contains(step); });
    return steps;
}

std::vector<StartStep> StepsTo(const StartStep& step) {
    std::vector<StartStep> steps;
    for (const StartStep& other : StartSteps) {
        if (other.item == step.item) {
            steps.push_back(other);
        }
        if (other == step) {
            break;
        }
    }
    return steps;
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
             { TrialsSetting, Words(TrialsChoices, settings.trials) },
             { OpeningSetting, Words(OpeningChoices, settings.opening) },
             { CutscenesSetting, Words(CutscenesChoices, settings.cutscenes) },
             { StartWithSetting, nlohmann::ordered_json::array() } };
    for (const StartStep& step : StartWithInOrder(settings)) {
        json[std::string(StartWithSetting)].push_back(step.name);
    }
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
    json[std::string(GiftsSetting)] = nlohmann::ordered_json::array();
    for (const auto& [group, name] : GiftGroups) {
        if (settings.gifts.contains(group)) {
            json[std::string(GiftsSetting)].push_back(name);
        }
    }
    json[std::string(SongsSetting)] = Words(SongsChoices, settings.songs);
    json[std::string(LooseItemsSetting)] = Words(LooseItemsChoices, settings.looseItems);
    json[std::string(RewardsSetting)] = Words(RewardsChoices, settings.rewards);
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
        } else if (setting == OpeningSetting) {
            settings.opening = Parse(OpeningChoices, setting, value);
        } else if (setting == CutscenesSetting) {
            settings.cutscenes = Parse(CutscenesChoices, setting, value);
        } else if (setting == StartWithSetting) {
            settings.startWith = StartWith(setting, value);
        } else if (const auto shop = std::ranges::find(ShopSettings, setting); shop != std::end(ShopSettings)) {
            shops.emplace(*shop, value);
        } else if (setting == GiftsSetting) {
            settings.gifts = Gifts(setting, value);
        } else if (setting == RewardsSetting) {
            settings.rewards = Parse(RewardsChoices, setting, value);
        } else if (setting == SkulltulasSetting) {
            settings.skulltulas = Parse(SkulltulasChoices, setting, value);
        } else if (setting == SongsSetting) {
            settings.songs = Parse(SongsChoices, setting, value);
        } else if (setting == LooseItemsSetting) {
            settings.looseItems = Parse(LooseItemsChoices, setting, value);
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
