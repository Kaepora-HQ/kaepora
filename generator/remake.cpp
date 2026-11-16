#include "remake.h"

#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/dungeon.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/util.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>

static constexpr char chestTable[] = {
#embed "../remake/chests.tsv"
};
static constexpr char itemTable[] = {
#embed "../remake/items.tsv"
};

[[noreturn]] static void Fail(const std::string& message) {
    throw std::runtime_error(message);
}

static std::vector<std::vector<std::string>> ReadTable(const std::string& name, const std::string& text,
                                                       const std::string& header) {
    std::vector<std::string> lines = SohUtils::StringSplit(text, "\n");
    if (lines.front() != header || !lines.back().empty()) {
        Fail(name + ": the header is not \"" + header + "\", or the last line has no newline");
    }
    lines.pop_back();
    std::vector<std::vector<std::string>> rows;
    for (size_t line = 1; line < lines.size(); ++line) {
        rows.push_back(SohUtils::StringSplit(lines[line], "\t"));
        if (rows.back().size() != SohUtils::StringSplit(header, "\t").size()) {
            Fail(name + " line " + std::to_string(line + 1) + ": wrong number of fields");
        }
    }
    return rows;
}

std::string RemakeChest::Folder() const {
    size_t start = files.front().find('/') + 1;
    return files.front().substr(start, files.front().find('/', start) - start);
}

// Remake item name to the SoH items it stands for, from remake/items.tsv. An empty list means no SoH item exists.
static std::map<std::string, std::vector<RandomizerGet>> LoadItems() {
    std::map<std::string, std::vector<RandomizerGet>> items;
    size_t line = 1;
    for (const auto& row : ReadTable("remake/items.tsv", { itemTable, sizeof itemTable }, "remake_item\tsoh_item")) {
        const std::string where = "remake/items.tsv line " + std::to_string(++line) + ": ";
        const std::string& name = row[0];
        const std::string& sohName = row[1];
        if (name.empty()) {
            Fail(where + "no remake item");
        }
        bool seen = items.contains(name);
        std::vector<RandomizerGet>& candidates = items[name];
        if (seen && (sohName.empty() || candidates.empty())) {
            Fail(where + "\"" + name + "\" stands for no SoH item on one row and for one on another");
        }
        if (sohName.empty()) {
            continue;
        }
        auto found = Rando::StaticData::itemNameToEnum.find(sohName);
        if (found == Rando::StaticData::itemNameToEnum.end() || found->second == RG_NONE) {
            Fail(where + "unknown SoH item \"" + sohName + "\"");
        }
        if (std::ranges::find(candidates, found->second) != candidates.end()) {
            Fail(where + "repeats a row");
        }
        candidates.push_back(found->second);
    }
    return items;
}

// A remake key, map or compass names no dungeon: the chest gives the one of the dungeon it stands in.
static RandomizerGet ResolveItem(const std::vector<RandomizerGet>& candidates, RandomizerCheck rc) {
    if (candidates.size() == 1) {
        return candidates.front();
    }
    const Rando::Location* location = Rando::StaticData::GetLocation(rc);
    if (candidates.empty() || !location->IsDungeon()) {
        return RG_NONE;
    }
    const Rando::DungeonInfo* dungeon =
        Rando::Context::GetInstance()->GetDungeon(location->GetArea() - RCAREA_DEKU_TREE);
    for (RandomizerGet item : candidates) {
        if (item == dungeon->GetSmallKey() || item == dungeon->GetBossKey() || item == dungeon->GetMap() ||
            item == dungeon->GetCompass()) {
            return item;
        }
    }
    return RG_NONE;
}

std::vector<RemakeChest> LoadRemakeChests() {
    const std::map<std::string, std::vector<RandomizerGet>> items = LoadItems();
    std::vector<RemakeChest> chests;
    std::set<RandomizerCheck> locations;
    std::set<std::string> hashes;
    size_t line = 1;
    for (const auto& row : ReadTable("remake/chests.tsv", { chestTable, sizeof chestTable },
                                     "location\thash\tfiles\titem\tshuffled\tnote")) {
        const std::string where = "remake/chests.tsv line " + std::to_string(++line) + ": ";
        const std::string& name = row[0];
        const std::string& hash = row[1];
        const std::string& files = row[2];
        const std::string& item = row[3];
        const std::string& shuffled = row[4];

        auto location = Rando::StaticData::locationNameToEnum.find(name);
        if (location == Rando::StaticData::locationNameToEnum.end() ||
            Rando::StaticData::GetLocation(location->second)->GetActorID() != ACTOR_EN_BOX) {
            Fail(where + "\"" + name + "\" is not a SoH chest location");
        }
        if (!locations.insert(location->second).second) {
            Fail(where + "repeats the location \"" + name + "\"");
        }
        if (hash.empty() != files.empty() || (hash.empty() && !item.empty())) {
            Fail(where + "a row with no remake chest leaves hash, files and item empty");
        }
        if (!hash.empty() &&
            (hash.find_first_not_of("0123456789") != std::string::npos || !hashes.insert(hash).second)) {
            Fail(where + "the hash \"" + hash + "\" is not a decimal number or repeats");
        }
        if (shuffled != "yes" && shuffled != "no") {
            Fail(where + "shuffled is neither yes nor no");
        }

        RemakeChest chest{ location->second, hash, {}, item, RG_NONE, shuffled == "yes" };
        if (!files.empty()) {
            chest.files = SohUtils::StringSplit(files, ",");
        }
        for (const std::string& file : chest.files) {
            if (!file.starts_with("Banc/" + chest.Folder() + "/") || !file.ends_with(".bcett.byml.zs")) {
                Fail(where + "\"" + file + "\" is not a scene file in the chest's Banc folder");
            }
        }
        if (!item.empty()) {
            auto candidates = items.find(item);
            if (candidates == items.end()) {
                Fail(where + "unknown remake item \"" + item + "\"");
            }
            chest.sohItem = ResolveItem(candidates->second, chest.location);
            if (chest.sohItem == RG_NONE && !candidates->second.empty()) {
                Fail(where + "no SoH item for \"" + item + "\" in this location's dungeon");
            }
        }
        if (chest.shuffled && chest.sohItem == RG_NONE) {
            Fail(where + "a shuffled chest needs a remake chest whose item stands for a SoH item");
        }
        chests.push_back(std::move(chest));
    }

    for (const Rando::Location& location : Rando::StaticData::GetLocationTable()) {
        if (location.GetActorID() == ACTOR_EN_BOX && location.GetQuest() != RCQUEST_MQ &&
            !locations.contains(location.GetRandomizerCheck())) {
            Fail("remake/chests.tsv: no row for the SoH chest location \"" + location.GetName() + "\"");
        }
    }
    return chests;
}

// Saved values a new game starts with set. Each opens a locked door that SoH opens when it creates a save, and that
// SoH's rules count open.
static constexpr const char* startFlags[] = {
    // The Fire Temple's lowest locked door, to the boss key loop, starts open. Logic::IsFireLoopLocked is false, so
    // fire_temple.cpp asks no key there, and Logic::Reset counts one Fire Temple small key for it.
    "IsChainLockDoorOpen.4189300454150217955",
    // The Water Temple's west middle-floor door, to the jet lift, starts open. water_temple.cpp asks no key there.
    "IsChainLockDoorOpen.2202807660383340401",
};

nlohmann::ordered_json RemakePlan(const std::string& seed, const std::vector<RemakeChest>& chests) {
    std::map<RandomizerGet, std::deque<std::string>> names;
    for (const RemakeChest& chest : chests) {
        if (chest.shuffled) {
            names[chest.sohItem].push_back(chest.item);
        }
    }
    nlohmann::ordered_json plan = { { "seed", seed }, { "chests", nlohmann::ordered_json::array() } };
    for (const RemakeChest& chest : chests) {
        if (!chest.shuffled) {
            continue;
        }
        RandomizerGet placed = Rando::Context::GetInstance()->GetItemLocation(chest.location)->GetPlacedRandomizerGet();
        std::deque<std::string>& candidates = names.at(placed);
        plan["chests"].push_back(
            { { "hash", chest.hash }, { "files", chest.files }, { "drop_actor", candidates.front() } });
        candidates.pop_front();
    }
    // No item an event flow gives is shuffled.
    plan["gifts"] = nlohmann::ordered_json::array();
    plan["start_flags"] = startFlags;
    return plan;
}
