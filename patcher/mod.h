// A plan as a mod. Under romfs/, at their paths in the game's RomFS, are each placement file and event flow the plan
// changes, the size table if a file outgrows its entry, and the game data list if the plan names start values. Under
// exefs/ is the patch to the game's program (program_patch.h).
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct PlannedChest {
    uint64_t hash;
    std::vector<std::string> files; // RomFS paths of the placement files that hold the chest
    std::string dropActor;
};

// A string parameter of an event in an event flow, and the text it takes. A gift names its item this way.
struct PlannedEventText {
    std::string flow; // RomFS path of the event flow
    std::string event;
    std::string param;
    std::string text;
};

// A saved value as a new game starts with it: a Bool or an Int of the game data list.
using StartValue = std::variant<bool, int32_t>;

struct Plan {
    std::vector<PlannedChest> chests;
    std::vector<PlannedEventText> eventTexts;
    std::map<std::string, StartValue> startValues;
};

// Throws, naming the part, when the plan is not the shape the generator writes.
Plan ParsePlan(const nlohmann::json& json);

// The mod's files by their path in the mod's folder. Throws when the game file holds another version of the game, or
// when its files do not hold what the plan changes.
std::map<std::string, std::string> ModFiles(const std::filesystem::path& nup, const Plan& plan);

// Writes the data to the file at the path, making its folders.
void WriteFile(const std::filesystem::path& path, std::string_view data);
