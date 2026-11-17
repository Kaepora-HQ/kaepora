#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <set>
#include <string_view>
#include <utility>

inline constexpr std::string_view ShuffledDungeonsSetting = "Shuffled dungeons";
inline constexpr std::string_view KeysSetting = "Small and boss keys";
inline constexpr std::string_view MapsAndCompassesSetting = "Maps and compasses";
inline constexpr std::string_view LensSetting = "Lens of Truth";

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

enum class DungeonItems { AnywhereInTheirDungeon, InTheirOwnChests };
inline constexpr std::pair<DungeonItems, std::string_view> DungeonItemsChoices[] = {
    { DungeonItems::AnywhereInTheirDungeon, "Anywhere in their dungeon" },
    { DungeonItems::InTheirOwnChests, "In their own chests" },
};

enum class Lens { Needed, NeverNeeded };
inline constexpr std::pair<Lens, std::string_view> LensChoices[] = {
    { Lens::Needed, "Needed" },
    { Lens::NeverNeeded, "Never needed" },
};

template <typename Value, size_t Size>
std::string_view Words(const std::pair<Value, std::string_view> (&choices)[Size], Value value) {
    return std::ranges::find(choices, value, &std::pair<Value, std::string_view>::first)->second;
}

struct SeedSettings {
    std::set<Dungeon> shuffledDungeons{ std::begin(Dungeons), std::end(Dungeons) };
    DungeonItems keys = DungeonItems::AnywhereInTheirDungeon;
    DungeonItems mapsAndCompasses = DungeonItems::AnywhereInTheirDungeon;
    Lens lens = Lens::Needed;

    bool operator==(const SeedSettings&) const = default;
};

nlohmann::ordered_json ToJson(const SeedSettings& settings);

// The settings from an object of the spoiler log's form. A setting the object leaves out has its default. Throws on
// an unknown setting or value.
SeedSettings ParseSeedSettings(const nlohmann::json& json);
