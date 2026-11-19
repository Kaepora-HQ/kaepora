#pragma once

#include "remake.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

// A hint for every stone of the tables, in their order, drawn after the fill as SoH's balanced distribution draws them.
// A hint about a location goes on a stone that Link reaches without the location's item. The items are the remake
// items the shuffled chests, shop slots and gifts give.
std::vector<StoneHint> PickHints(const RemakeHintTables& tables, const std::map<RandomizerCheck, std::string>& items);

// The sentence in SoH's markup, laid out to fit, in the patcher's tags: # starts and ends a coloured run, & starts a
// new line, ^ a new box, and @ is the player's name.
std::string Layout(std::string_view markup, const Fit& fit);

// Each prize line naming the item the seed places at its location, or where the location keeps its item, that item.
std::vector<MessageText> PrizeLines(const RemakeHintTables& tables,
                                    const std::map<RandomizerCheck, std::string>& items);
