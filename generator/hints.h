#pragma once

#include "remake.h"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// A hint for every stone of the tables, in their order, drawn after the fill as SoH's balanced distribution draws them.
// A hint about a location goes on a stone that Link reaches without the location's item. The items are the remake
// items the shuffled chests, shop slots, gifts and Gold Skulltulas give. No hint is about a location in named.
std::vector<StoneHint> PickHints(const RemakeHintTables& tables, const std::map<RandomizerCheck, std::string>& items,
                                 const std::set<RandomizerCheck>& named);

struct CharacterHintText {
    RandomizerCheck hint;
    RandomizerCheck place; // the location that holds the hint's item
    MessageText message;
    std::string plain;
};

// Each message of the characters' hints that the seed's characters say, where the hint's item lies in a place the seed
// shuffles. A stone names no place a character names.
std::vector<CharacterHintText> CharacterHints(const RemakeHintTables& tables,
                                              const std::map<RandomizerCheck, std::string>& items,
                                              const SeedSettings& settings);

// The sentence in SoH's markup, laid out to fit, in the patcher's tags: # starts and ends a coloured run, & starts a
// new line, ^ a new box, and @ is the player's name.
std::string Layout(std::string_view markup, const Fit& fit);

// Each prize line naming the item the seed places at its location, or where the location keeps its item, that item.
std::vector<MessageText> PrizeLines(const RemakeHintTables& tables,
                                    const std::map<RandomizerCheck, std::string>& items);
