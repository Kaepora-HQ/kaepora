#include "seed_settings.h"

#include <algorithm>
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

nlohmann::ordered_json ToJson(const SeedSettings& settings) {
    nlohmann::ordered_json dungeons = nlohmann::ordered_json::array();
    for (const Dungeon& dungeon : Dungeons) {
        if (settings.shuffledDungeons.contains(dungeon)) {
            dungeons.push_back(dungeon.name);
        }
    }
    return { { ShuffledDungeonsSetting, dungeons },
             { KeysSetting, Words(KeysChoices, settings.keys) },
             { MapsAndCompassesSetting, Words(MapsAndCompassesChoices, settings.mapsAndCompasses) },
             { ForestSetting, Words(ForestChoices, settings.forest) },
             { BridgeSetting, Words(BridgeChoices, settings.bridge) },
             { TrialsSetting, Words(TrialsChoices, settings.trials) },
             { ShopsSetting, Words(ShopsChoices, settings.shops) },
             { LensSetting, Words(LensChoices, settings.lens) },
             { GossipStonesSetting, Words(GossipStonesChoices, settings.gossipStones) } };
}

SeedSettings ParseSeedSettings(const nlohmann::json& json) {
    SeedSettings settings;
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
        } else if (setting == ShopsSetting) {
            settings.shops = Parse(ShopsChoices, setting, value);
        } else if (setting == LensSetting) {
            settings.lens = Parse(LensChoices, setting, value);
        } else if (setting == GossipStonesSetting) {
            settings.gossipStones = Parse(GossipStonesChoices, setting, value);
        } else {
            throw std::runtime_error("unknown setting \"" + setting + "\"");
        }
    }
    return settings;
}
