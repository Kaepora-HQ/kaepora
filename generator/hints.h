#pragma once

#include "remake.h"

#include <map>
#include <string>
#include <vector>

// A hint for every stone of the tables, in their order, drawn after the fill as SoH's balanced distribution draws them.
// A hint about a location goes on a stone that Link reaches without the location's item. The items are the remake
// items the shuffled chests and shop slots give.
std::vector<StoneHint> PickHints(const RemakeHintTables& tables, const std::map<RandomizerCheck, std::string>& items);
