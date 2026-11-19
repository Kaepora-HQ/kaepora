// A plan as a mod. Under romfs/, at their paths in the game's RomFS, are each placement file, event flow and actor pack
// the plan changes, the pack of each actor it adds and the actor tables with their rows, the two English text packs and
// the stones' flow if the plan gives gossip stones texts, the size table if a file outgrows its entry or is new, the
// game data list if the plan names start values or shop slots, and the actor packs that let a bomb out of the bag with
// no strength. Under exefs/ is the patch to the game's program (program_patch.h).
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

// A gossip stone says the message whose label is its name.
struct PlannedStoneText {
    std::string label;
    std::string text; // the message as a text pack stores it
};

// A shopkeeper actor, and the goods list in its pack that it sells from.
struct Shopkeeper {
    std::string actor;
    std::string goodsList;
};

// A placed actor, by the RomFS path of its placement file and its hash there.
struct PlacedActor {
    std::string file;
    uint64_t hash;
};

// A shop slot that sells the item at the price, once from each shopkeeper. Each shelf shows the slot in one scene.
struct PlannedShopSlot {
    std::vector<Shopkeeper> shopkeepers;
    uint32_t slot; // the index in each shopkeeper's goods list
    std::vector<PlacedActor> shelves;
    std::string item;
    int32_t price;
};

// A saved value as a new game starts with it: a Bool or an Int of the game data list.
using StartValue = std::variant<bool, int32_t>;

struct Plan {
    std::vector<PlannedChest> chests;
    std::vector<PlannedEventText> eventTexts;
    std::map<std::string, StartValue> startValues;
    std::vector<PlannedStoneText> stoneTexts;
    std::vector<PlannedShopSlot> shops;
};

// Throws, naming the part, when the plan is not the shape the generator writes.
Plan ParsePlan(const nlohmann::json& json);

// The mod's files by their path in the mod's folder. Throws when the game file holds another version of the game, or
// when its files do not hold what the plan changes.
std::map<std::string, std::string> ModFiles(const std::filesystem::path& nup, const Plan& plan);

// Writes the data to the file at the path, making its folders.
void WriteFile(const std::filesystem::path& path, std::string_view data);
