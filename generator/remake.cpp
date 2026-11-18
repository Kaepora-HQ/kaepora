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

std::string RemakeChest::Actor::Folder() const {
    const std::string& file = files.front();
    size_t start = file.find('/') + 1;
    return file.substr(start, file.find('/', start) - start);
}

std::string RemakeChest::Folder() const {
    return actors.front().Folder();
}

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
        auto found = Rando::StaticData::itemNameToEnum.find(sohName);
        if (found == Rando::StaticData::itemNameToEnum.end() || found->second == RG_NONE) {
            Fail(where + "unknown SoH item \"" + sohName + "\"");
        }
        std::vector<RandomizerGet>& candidates = items[name];
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
    if (!location->IsDungeon()) {
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
    std::map<RandomizerCheck, size_t> locations;
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
        const std::string& note = row[5];

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
        if (shuffled == "no" && note.empty()) {
            Fail(where + "a row that is not shuffled needs a note");
        }
        const RemakeChest::Actor actor{ hash, SohUtils::StringSplit(files, ",") };
        if (!hash.empty()) {
            for (const std::string& file : actor.files) {
                if (!file.starts_with("Banc/" + actor.Folder() + "/") || !file.ends_with(".bcett.byml.zs")) {
                    Fail(where + "\"" + file + "\" is not a scene file in the Banc folder of the row's first file");
                }
            }
        }
        if (name.empty()) {
            if (hash.empty() || shuffled == "yes") {
                Fail(where + "a row with no location is a remake chest that is not shuffled");
            }
            continue;
        }
        auto location = Rando::StaticData::locationNameToEnum.find(name);
        if (location == Rando::StaticData::locationNameToEnum.end()) {
            Fail(where + "\"" + name + "\" is not a SoH location");
        }

        auto [index, added] = locations.try_emplace(location->second, chests.size());
        if (added) {
            RemakeChest chest{ location->second, {}, item, RG_NONE, shuffled == "yes" };
            if (!item.empty()) {
                auto candidates = items.find(item);
                if (candidates == items.end()) {
                    Fail(where + "unknown remake item \"" + item + "\"");
                }
                chest.sohItem = ResolveItem(candidates->second, chest.location);
                if (chest.sohItem == RG_NONE) {
                    Fail(where + "no SoH item for \"" + item + "\" in this location's dungeon");
                }
            }
            if (chest.shuffled && chest.sohItem == RG_NONE) {
                Fail(where + "a shuffled chest needs a remake chest whose item stands for a SoH item");
            }
            chests.push_back(std::move(chest));
        } else if (hash.empty() || chests[index->second].actors.empty() || item != chests[index->second].item ||
                   (shuffled == "yes") != chests[index->second].shuffled) {
            Fail(where + "repeats the location \"" + name + "\" for anything but another copy of its chest");
        }
        if (!hash.empty()) {
            chests[index->second].actors.push_back(actor);
        }
    }

    for (const Rando::Location& location : Rando::StaticData::GetLocationTable()) {
        if (location.GetActorID() == ACTOR_EN_BOX && location.GetQuest() != RCQUEST_MQ &&
            !locations.contains(location.GetRandomizerCheck())) {
            Fail("remake/chests.tsv: no row for the SoH chest location \"" + location.GetName() + "\"");
        }
    }
    return chests;
}

// Saved values a new game starts with set. Each opens a locked door that SoH's rules count open. SoH itself opens the
// first two when it creates a save.
static constexpr const char* startFlags[] = {
    // The Fire Temple's lowest locked door, to the boss key loop, starts open. Logic::IsFireLoopLocked is false, so
    // fire_temple.cpp asks no key there, and Logic::Reset counts one Fire Temple small key for it.
    "IsChainLockDoorOpen.4189300454150217955",
    // The Water Temple's west middle-floor door, to the jet lift, starts open. water_temple.cpp asks no key there.
    "IsChainLockDoorOpen.2202807660383340401",
    // The west door of the Forest Temple's bow chest room stays locked from the room's side until an arrow hits its
    // eye, and refuses a warp to a Link who came in through it while it is locked, so one with no bow is shut in when
    // the chest holds another item. Set from the start, this value lifts the door's only lock, and the door opens from
    // both sides. forest_temple.cpp lets Link back west once the Stalfos are dead.
    "Gimmick_DefeatEnemy_Open_ForestTemple_03",
    // The Spirit Temple's door between the statue room and the child climb room stays shut from both sides until the
    // cracked wall in the climb room breaks. spirit_temple.cpp lets Link through both ways with nothing, and SoH's key
    // counts for the temple expect an adult to walk from the statue room into the climb room. Set from the start, this
    // value lifts both of the door's locks, and the wall stays standing.
    "SpiritTemple_Room07_Unlocked",
};

struct EventText {
    std::string_view flow;
    std::string_view event;
    std::string_view param;
    std::string_view text;
};

static constexpr EventText eventTexts[] = {
    // The warp after Gohma completes the Forest quest in its Event1. Its Event4 sets the castle quest to Step01 even when
    // the quest is further on, and a Link who woke Talon before Gohma can then not meet Zelda. These texts make Event4 a
    // second copy of Event1.
    { "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "Event4", "ChallengeName", "Get_StoneOfForest" },
    { "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "Event4", "StepName", "Complete" },
    // Kaepora Gaebora waits at the foot of Zora's River once the Dodongo's Cavern Bomb Bag Chest is open. His Event80
    // sets the Water quest to Step02 even when the quest is further on, and a Link who has shown King Zora Ruto's
    // letter is then barred from Zora's Fountain. Event80 runs only after Impa's talk, which comes only once the Fire
    // quest is complete, so these texts make it complete the Fire quest, a write the game ignores on a completed quest.
    { "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "Event80", "ChallengeName", "Get_StoneOfFire" },
    { "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "Event80", "StepName", "Complete" },
    // After Link loses Ingo's bet race on a horse that is not Epona, adult Malon's talk sets Epona's quest to
    // GuideToLonLonRanchAdult in Event177 whenever Event179 finds the quest not past ReadyForGetEponaSong. A Link who
    // met child Malon at the ranch and has not learned her song is set back, and she then never teaches it. This text
    // makes Event179 ask whether the quest is past Ready, so the write moves only a quest still at Ready.
    { "Event/EventFlow/Npc_Hylia_Malon_Adult.bfevfl.zs", "Event179", "GameDataName", "Get_Epona_IsAfter_Ready_Exp" },
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
        for (const RemakeChest::Actor& actor : chest.actors) {
            plan["chests"].push_back(
                { { "hash", actor.hash }, { "files", actor.files }, { "drop_actor", candidates.front() } });
        }
        candidates.pop_front();
    }
    for (const auto& [flow, event, param, text] : eventTexts) {
        plan["event_texts"].push_back({ { "flow", flow }, { "event", event }, { "param", param }, { "text", text } });
    }
    plan["start_flags"] = startFlags;
    return plan;
}
