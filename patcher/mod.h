// A plan as a mod. Under romfs/, at their paths in the game's RomFS, are each placement file, event flow and actor pack
// the plan changes, the pack of each actor it adds and the actor tables with their rows, the two English text packs if
// the plan gives texts to gossip stones or other messages, the stones' flow if it gives gossip stones texts, the
// opening's flow if it skips the opening, the Kokiri Sword chest's flow if the chest holds another item, the size table
// if a file outgrows its entry or is new, the game data list if the plan names start values, start items or shop slots
// or skips the opening, the actor packs that let a bomb out of the bag with no strength, the logic module that lets
// every adult put the Master Sword back, the Temple of Time's placement with Sheik waiting behind the sword, and the
// pouch table in which the Fairy Bow and the Fairy Slingshot bring their quiver and bullet bag. Under exefs/ is the
// patch to the game's program (program_patch.h).
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
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

// An integer parameter of an event in an event flow, and the value it takes. A song's lesson names its melody this way.
struct PlannedEventNumber {
    std::string flow; // RomFS path of the event flow
    std::string event;
    std::string param;
    int32_t value;
};

// A gossip stone says the message whose label is its name.
struct PlannedStoneText {
    std::string label;
    std::string text; // the message as a text pack stores it
};

// A message of a text pack, and the text it says after the control codes the game's message opens with. A copy is a
// new message under a label the file lacks, opening with the codes the copied message opens with, in every language's
// pack; outside English it says the copied message whole.
struct PlannedMessageText {
    std::string file; // the message file in the text pack, e.g. EventFlowMsg/SignboardWood.msbt
    std::string label;
    std::optional<std::string> copyOf; // the label of the copied message
    std::string text;                  // as a text pack stores it
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

// A placed actor that stands only while a saved true-or-false value or expression of the game data list holds, as the
// game's own placements gate actors on quest steps. The Desert Colossus's ways out wait for the Spirit quest this way.
// A presence that names the flag it replaces points the actor's own presence on that flag at this one instead, and
// keeps its negation.
struct PlannedPresence {
    PlacedActor actor;
    std::string flag;
    std::optional<std::string> replaces;
};

// An expression of the mod's own, true while one saved Bool is: one of the game data list or a claim of the plan.
// An expression query can name it, and none can name a Bool.
struct PlannedExpression {
    std::string name;
    std::string flag;
};

// A shop slot that sells the item at the price, once from each shopkeeper. Each shelf shows the slot in one scene.
struct PlannedShopSlot {
    std::vector<Shopkeeper> shopkeepers;
    uint32_t slot; // the index in each shopkeeper's goods list
    std::vector<PlacedActor> shelves;
    std::string item;
    int32_t price;
};

// An item a placed actor gives: the actor's hash, every placement file that places it, and the item. A Gold Skulltula
// drops it; a loose item is it.
struct PlannedActorItem {
    uint64_t hash;
    std::vector<std::string> files;
    std::string item;
};

// An Enum's value, by its name.
struct Choice {
    std::string name;

    auto operator<=>(const Choice&) const = default;
};

// A saved value as a new game starts with it: a Bool, an Int, a Float, an Enum or a String64 of the game data list.
using StartValue = std::variant<bool, int32_t, float, Choice, std::string>;

struct Plan {
    std::vector<PlannedChest> chests;
    std::vector<PlannedPresence> presences;
    std::vector<PlannedEventText> eventTexts;
    std::vector<PlannedEventNumber> eventNumbers;
    std::map<std::string, StartValue> startValues;
    std::vector<PlannedStoneText> stoneTexts;
    std::vector<PlannedMessageText> messageTexts;
    std::vector<PlannedShopSlot> shops;
    bool midoCastleEntryRemoved;
    bool openingSkipped;
    bool cutscenesSkipped;
    std::vector<PlannedActorItem> skulltulas;
    std::vector<PlannedActorItem> looseItems;
    std::vector<std::string> startItems; // actors a new game holds, as after a find of each in turn
    std::vector<std::string> claims;     // saved Bools of the mod's own, false in a new game
    std::vector<PlannedExpression> expressions;
};

// Throws, naming the part, when the plan is not the shape the generator writes.
Plan ParsePlan(const nlohmann::json& json);

// The mod's files by their path in the mod's folder. Throws when the game file holds another version of the game, or
// when its files do not hold what the plan changes.
std::map<std::string, std::string> ModFiles(const std::filesystem::path& nup, const Plan& plan);

// Writes the data to the file at the path, making its folders.
void WriteFile(const std::filesystem::path& path, std::string_view data);
