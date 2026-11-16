// The remake's chests, from the tables in remake/.
#pragma once

#include "soh/Enhancements/randomizer/randomizerEnums.h"

#include <string>
#include <vector>

// A SoH chest location and the remake chest it is. Where the remake has no chest, hash and files are empty.
struct RemakeChest {
    RandomizerCheck location;
    std::string hash;
    // Romfs paths of the scene files that place the chest, all in one Banc folder.
    std::vector<std::string> files;
    // The chest's Drop__DropActor, empty when it has none, and the SoH item it stands for. RG_NONE means the logic
    // sees nothing there.
    std::string item;
    RandomizerGet sohItem;
    bool shuffled;

    // The Banc folder, the remake's name for the scene the chest stands in.
    std::string Folder() const;
};

// The rows of remake/chests.tsv, read from the copy built into the program. Exits with a message when a table names
// an unknown location or item, repeats a location or hash, or leaves out a chest location of SoH's vanilla layouts.
std::vector<RemakeChest> LoadRemakeChests();
