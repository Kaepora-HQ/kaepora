#pragma once

#include "seed_settings.h"
#include "soh/Enhancements/randomizer/randomizerEnums.h"
#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

// A SoH location and the remake chest it is. Where the remake has no chest, actors is empty.
struct RemakeChest {
    // One actor that places the chest: its hash, and the romfs paths of the scene files that hold it, all in one Banc
    // folder.
    struct Actor {
        std::string hash;
        std::vector<std::string> files;

        std::string Folder() const;
    };

    RandomizerCheck location;
    // Usually one. The files keep a few chests twice, under two hashes, and both give the location's item: once in
    // each of two versions of a scene, once in each of two scenes that show the same spot, or as the two chests one
    // puzzle shows one of.
    std::vector<Actor> actors;
    // What the first actor gives, by its remake name (remake/README.md), empty when the table names nothing, and the
    // SoH item it stands for. RG_NONE means the logic sees nothing there.
    std::string item;
    RandomizerGet sohItem;
    bool shuffled;

    // The first actor's Banc folder, the remake's name for the scene the chest stands in.
    std::string Folder() const;
};

// The chests of remake/chests.tsv that stand for a location, read from the copy built into the program, one per
// location. Throws when a table names an unknown location or item, repeats a hash, repeats a location for anything but
// another copy of its chest, gives an unshuffled row no note, or leaves out a chest location of SoH's vanilla layouts.
std::vector<RemakeChest> LoadRemakeChests();

struct RemakeStone {
    std::string label;
    RandomizerCheck location;
};

struct ChestSentence {
    std::string beforeItem;
    std::string afterItem;
};

// Places and items as a hint names them, with their article.
struct RemakeHintTables {
    std::vector<RemakeStone> stones;
    std::map<RandomizerCheckArea, std::string> places;
    std::map<std::string, std::string> itemsByRemakeName;
    std::map<RandomizerCheck, ChestSentence> chestSentences;
    std::vector<std::string> junk;
};

// The hint tables, read from the copies built into the program. Throws when a table names an unknown location, area or
// item, repeats one, or leaves out a shuffled chest of the chest table, its area or its item.
RemakeHintTables LoadRemakeHintTables(const std::vector<RemakeChest>& chests);

// The remake name of the item each shuffled chest gives. Where several remake names stand for one SoH item, the chests
// that hold it give the names the shuffled chests held, in table order, each as often as it occurred.
std::map<RandomizerCheck, std::string> PlacedRemakeItems(const std::vector<RemakeChest>& chests);

struct StoneHint {
    RemakeStone stone;
    std::string tagged;
    std::string plain;
};

// The plan the patcher reads: each actor of each shuffled chest, in table order, with the remake item placed there.
// Then the event flow strings every seed changes, the saved values a new game starts with that the rules or the
// settings count on, and the gossip stones' texts.
nlohmann::ordered_json RemakePlan(const std::string& seed, const std::vector<RemakeChest>& chests,
                                  const SeedSettings& settings, const std::map<RandomizerCheck, std::string>& items,
                                  const std::vector<StoneHint>& hints);
