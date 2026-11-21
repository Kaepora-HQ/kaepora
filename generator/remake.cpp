#include "remake.h"

#include "soh/Enhancements/randomizer/3drando/fill.hpp"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/dungeon.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/util.h"

#include <algorithm>
#include <charconv>
#include <deque>
#include <format>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>

static constexpr unsigned char chestTable[] = {
#embed "../remake/chests.tsv"
};
static constexpr unsigned char itemTable[] = {
#embed "../remake/items.tsv"
};
static constexpr unsigned char stoneTable[] = {
#embed "../remake/stones.tsv"
};
static constexpr unsigned char placeTable[] = {
#embed "../remake/places.tsv"
};
static constexpr unsigned char itemNameTable[] = {
#embed "../remake/item_names.tsv"
};
static constexpr unsigned char chestHintTable[] = {
#embed "../remake/chest_hints.tsv"
};
static constexpr unsigned char junkTable[] = {
#embed "../remake/junk_hints.tsv"
};
static constexpr unsigned char prizeLineTable[] = {
#embed "../remake/prize_lines.tsv"
};
static constexpr unsigned char characterHintTable[] = {
#embed "../remake/character_hints.tsv"
};
static constexpr unsigned char shopTable[] = {
#embed "../remake/shops.tsv"
};
static constexpr unsigned char shopGoodsTable[] = {
#embed "../remake/shop_goods.tsv"
};
static constexpr unsigned char giftTable[] = {
#embed "../remake/gifts.tsv"
};
static constexpr unsigned char rewardTable[] = {
#embed "../remake/rewards.tsv"
};
static constexpr unsigned char skulltulaTable[] = {
#embed "../remake/skulltulas.tsv"
};
static constexpr unsigned char looseItemTable[] = {
#embed "../remake/loose_items.tsv"
};
static constexpr unsigned char songTable[] = {
#embed "../remake/songs.tsv"
};

[[noreturn]] static void Fail(const std::string& message) {
    throw std::runtime_error(message);
}

static std::vector<std::vector<std::string>> ReadTable(const std::string& name, std::span<const unsigned char> table,
                                                       const std::string& header) {
    std::vector<std::string> lines = SohUtils::StringSplit({ table.begin(), table.end() }, "\n");
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
    for (const auto& row : ReadTable("remake/items.tsv", itemTable, "remake_item\tsoh_item")) {
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

static void CheckSceneFiles(const std::string& where, const RemakeChest::Actor& actor) {
    for (const std::string& file : actor.files) {
        if (!file.starts_with("Banc/" + actor.Folder() + "/") || !file.ends_with(".bcett.byml.zs")) {
            Fail(where + "\"" + file + "\" is not a scene file in the Banc folder of the row's first file");
        }
    }
}

std::vector<RemakeChest> LoadRemakeChests() {
    const std::map<std::string, std::vector<RandomizerGet>> items = LoadItems();
    std::vector<RemakeChest> chests;
    std::map<RandomizerCheck, size_t> locations;
    std::set<std::string> hashes;
    size_t line = 1;
    for (const auto& row : ReadTable("remake/chests.tsv", chestTable, "location\thash\tfiles\titem\tshuffled\tnote")) {
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
            CheckSceneFiles(where, actor);
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

        auto candidates = items.find(item);
        if (!item.empty() && candidates == items.end()) {
            Fail(where + "unknown remake item \"" + item + "\"");
        }
        auto [index, added] = locations.try_emplace(location->second, chests.size());
        if (added) {
            RemakeChest chest{ location->second, {}, item, RG_NONE, shuffled == "yes" };
            if (!item.empty()) {
                chest.sohItem = ResolveItem(candidates->second, chest.location);
                if (chest.sohItem == RG_NONE) {
                    Fail(where + "no SoH item for \"" + item + "\" in this location's dungeon");
                }
            }
            if (chest.shuffled && chest.sohItem == RG_NONE) {
                Fail(where + "a shuffled chest needs a remake chest whose item stands for a SoH item");
            }
            chests.push_back(std::move(chest));
        } else if (hash.empty() || chests[index->second].actors.empty() ||
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

std::vector<RemakeChest> LoadRemakeLooseItems() {
    const std::map<std::string, std::vector<RandomizerGet>> items = LoadItems();
    std::vector<RemakeChest> looseItems;
    std::set<RandomizerCheck> locations;
    std::set<std::string> hashes;
    size_t line = 1;
    for (const auto& row : ReadTable("remake/loose_items.tsv", looseItemTable, "location\thash\tfiles\titem")) {
        const std::string where = std::format("remake/loose_items.tsv line {}: ", ++line);
        const auto location = Rando::StaticData::locationNameToEnum.find(row[0]);
        if (location == Rando::StaticData::locationNameToEnum.end() ||
            Rando::StaticData::GetLocation(location->second)->GetQuest() == RCQUEST_MQ ||
            !locations.insert(location->second).second) {
            Fail(where + "\"" + row[0] + "\" is not a location of SoH's vanilla layouts, or repeats");
        }
        const std::string& hash = row[1];
        if (hash.empty() || hash.find_first_not_of("0123456789") != std::string::npos || !hashes.insert(hash).second) {
            Fail(where + "the hash \"" + hash + "\" is not a decimal number or repeats");
        }
        const RemakeChest::Actor actor{ hash, SohUtils::StringSplit(row[2], ",") };
        CheckSceneFiles(where, actor);
        const auto candidates = items.find(row[3]);
        const RandomizerGet sohItem =
            candidates == items.end() ? RG_NONE : ResolveItem(candidates->second, location->second);
        if (sohItem != Rando::StaticData::GetLocation(location->second)->GetVanillaItem()) {
            Fail(where + "\"" + row[3] + "\" is not the remake item for what the location gives in SoH");
        }
        looseItems.push_back({ location->second, { actor }, row[3], sohItem, true });
    }
    return looseItems;
}

static RandomizerCheck LocationNamed(const std::string& where, const std::string& name) {
    auto location = Rando::StaticData::locationNameToEnum.find(name);
    if (location == Rando::StaticData::locationNameToEnum.end()) {
        Fail(where + "\"" + name + "\" is not a SoH location");
    }
    return location->second;
}

constexpr std::string_view ItemSlot = "[[1]]";

static Sentence Split(const std::string& where, const std::string& sentence) {
    const size_t slot = sentence.find(ItemSlot);
    if (slot == std::string::npos || sentence.find(ItemSlot, slot + 1) != std::string::npos) {
        Fail(where + "a sentence without one [[1]] for the item");
    }
    return { sentence.substr(0, slot), sentence.substr(slot + ItemSlot.size()) };
}

static size_t Count(const std::string& where, const std::string& text) {
    size_t count = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
    if (error != std::errc() || end != text.data() + text.size() || count == 0) {
        Fail(where + "\"" + text + "\" is not a whole number above 0");
    }
    return count;
}

static Fit ReadFit(const std::string& where, const std::string& lineLength, const std::string& boxLines) {
    return { .lineLength = Count(where, lineLength), .boxLines = Count(where, boxLines) };
}

// The SoH area whose locations' names start with the prefix.
static RandomizerCheckArea AreaNamed(const std::string& where, const std::string& prefix) {
    for (int area = RCAREA_KOKIRI_FOREST; area < RCAREA_INVALID; ++area) {
        if (SohUtils::GetRandomizerCheckAreaPrefix(area) == prefix) {
            return RandomizerCheckArea(area);
        }
    }
    Fail(where + "\"" + prefix + "\" is not the prefix of a SoH area");
}

static std::string ItemPhrase(const std::string& where, const std::vector<std::string>& row) {
    const std::string& name = row[2];
    const std::string& article = row[3];
    if (name.empty() || (article != "" && article != "a" && article != "an" && article != "the")) {
        Fail(where + "no name, or an article other than a, an or the");
    }
    return article.empty() ? name : article + " " + name;
}

RemakeHintTables LoadRemakeHintTables(const std::vector<RemakeChest>& chests, const std::vector<RemakeShopSlot>& shops,
                                      const std::vector<RemakeGift>& gifts,
                                      const std::vector<RemakeSkulltula>& skulltulas,
                                      const std::vector<RemakeSongPlace>& songs) {
    RemakeHintTables tables;
    size_t line = 1;
    for (const auto& row : ReadTable("remake/stones.tsv", stoneTable, "label\tlocation")) {
        const std::string where = std::format("remake/stones.tsv line {}: ", ++line);
        const RandomizerCheck location = LocationNamed(where, row[1]);
        if (row[0].empty() || Rando::StaticData::GetLocation(location)->GetRCType() != RCTYPE_GOSSIP_STONE ||
            std::ranges::find(tables.stones, row[0], &RemakeStone::label) != tables.stones.end() ||
            std::ranges::find(tables.stones, location, &RemakeStone::location) != tables.stones.end()) {
            Fail(where + "no label, a location that is not a gossip stone, or a stone listed twice");
        }
        tables.stones.push_back({ row[0], location });
    }

    line = 1;
    for (const auto& row : ReadTable("remake/places.tsv", placeTable, "area\tlabel\tname\tsays")) {
        const std::string where = std::format("remake/places.tsv line {}: ", ++line);
        if (row[3].empty() || !tables.places.emplace(AreaNamed(where, row[0]), row[3]).second) {
            Fail(where + "\"" + row[0] + "\" repeats an area, or says nothing");
        }
    }

    const std::map<std::string, std::vector<RandomizerGet>> items = LoadItems();
    line = 1;
    for (const auto& row : ReadTable("remake/item_names.tsv", itemNameTable, "remake_item\tlabel\tname\tarticle")) {
        const std::string where = std::format("remake/item_names.tsv line {}: ", ++line);
        if (!items.contains(row[0]) || !tables.itemsByRemakeName.emplace(row[0], ItemPhrase(where, row)).second) {
            Fail(where + "\"" + row[0] + "\" is not an item of remake/items.tsv, or repeats one");
        }
    }

    line = 1;
    for (const auto& row : ReadTable("remake/chest_hints.tsv", chestHintTable, "location\tsentence")) {
        const std::string where = std::format("remake/chest_hints.tsv line {}: ", ++line);
        if (!tables.chestSentences.emplace(LocationNamed(where, row[0]), Split(where, row[1])).second) {
            Fail(where + "a location listed twice");
        }
    }

    for (const auto& row : ReadTable("remake/junk_hints.tsv", junkTable, "hint")) {
        tables.junk.push_back(row[0]);
    }

    line = 1;
    for (const auto& row : ReadTable("remake/prize_lines.tsv", prizeLineTable,
                                     "location\tfile\tlabel\tsentence\tline_length\tbox_lines")) {
        const std::string where = std::format("remake/prize_lines.tsv line {}: ", ++line);
        const RandomizerCheck location = LocationNamed(where, row[0]);
        const auto chest = std::ranges::find(chests, location, &RemakeChest::location);
        if (chest == chests.end() || !tables.itemsByRemakeName.contains(chest->item)) {
            Fail(where + "\"" + row[0] + "\" is no chest of remake/chests.tsv, or its item has no name");
        }
        if (row[1].empty() || row[2].empty()) {
            Fail(where + "no file or label");
        }
        tables.prizeLines.push_back({ .location = location,
                                      .ownItem = chest->item,
                                      .file = row[1],
                                      .label = row[2],
                                      .sentence = Split(where, row[3]),
                                      .fit = ReadFit(where, row[4], row[5]) });
    }

    line = 1;
    for (const auto& row : ReadTable("remake/character_hints.tsv", characterHintTable,
                                     "hint\titem\tfile\tlabel\tsentence\tline_length\tbox_lines")) {
        const std::string where = std::format("remake/character_hints.tsv line {}: ", ++line);
        const RandomizerCheck hint = LocationNamed(where, row[0]);
        const auto item = Rando::StaticData::itemNameToEnum.find(row[1]);
        if (Rando::StaticData::GetLocation(hint)->GetRCType() != RCTYPE_STATIC_HINT ||
            item == Rando::StaticData::itemNameToEnum.end() || item->second == RG_NONE || row[2].empty() ||
            row[3].empty()) {
            Fail(where + "no SoH hint location, an unknown SoH item, or no file or label");
        }
        tables.characterHints.push_back({ .hint = hint,
                                          .item = item->second,
                                          .file = row[2],
                                          .label = row[3],
                                          .sentence = Split(where, row[4]),
                                          .fit = ReadFit(where, row[5], row[6]) });
    }

    std::set<RandomizerCheck> shuffled;
    const auto addShuffled = [&](RandomizerCheck rc, const std::string& item) {
        shuffled.insert(rc);
        const Rando::Location* location = Rando::StaticData::GetLocation(rc);
        if (!tables.chestSentences.contains(rc) || !tables.itemsByRemakeName.contains(item) ||
            !tables.places.contains(location->GetArea())) {
            Fail("\"" + location->GetName() + "\" has no sentence in remake/chest_hints.tsv, its item no name in " +
                 "remake/item_names.tsv, or its area no name in remake/places.tsv");
        }
    };
    for (const RemakeChest& chest : chests) {
        if (chest.shuffled) {
            addShuffled(chest.location, chest.item);
        }
    }
    for (const RemakeShopSlot& slot : shops) {
        addShuffled(slot.location, std::string(FillerName));
    }
    for (const RemakeGift& gift : gifts) {
        addShuffled(gift.location, gift.item);
    }
    for (const RemakeSkulltula& skulltula : skulltulas) {
        addShuffled(skulltula.location, std::string(TokenName));
    }
    for (const RemakeSongPlace& place : songs) {
        addShuffled(place.location, place.song);
    }
    for (const auto& [location, sentence] : tables.chestSentences) {
        if (!shuffled.contains(location)) {
            Fail("remake/chest_hints.tsv: \"" + Rando::StaticData::GetLocation(location)->GetName() +
                 "\" is not a shuffled chest, or a shop slot, gift, skulltula or song place that can be shuffled");
        }
    }
    return tables;
}

std::map<RandomizerCheck, std::string> PlacedRemakeItems(const std::vector<RemakeChest>& chests,
                                                         const std::vector<RemakeShopSlot>& shops,
                                                         const std::vector<RemakeGift>& gifts,
                                                         const std::vector<RemakeSkulltula>& skulltulas,
                                                         const std::vector<RemakeSongPlace>& songs) {
    std::map<RandomizerGet, std::deque<std::string>> names;
    for (const RemakeChest& chest : chests) {
        if (chest.shuffled) {
            names[chest.sohItem].push_back(chest.item);
        }
    }
    names[Filler].insert(names[Filler].end(), shops.size(), std::string(FillerName));
    for (const RemakeGift& gift : gifts) {
        names[gift.sohItem].push_back(gift.item);
    }
    names[RG_GOLD_SKULLTULA_TOKEN].insert(names[RG_GOLD_SKULLTULA_TOKEN].end(), skulltulas.size(),
                                          std::string(TokenName));
    for (const RemakeSongPlace& place : songs) {
        names[place.sohSong].push_back(place.song);
    }
    std::map<RandomizerCheck, std::string> placed;
    const auto place = [&](RandomizerCheck location) {
        RandomizerGet item = Rando::Context::GetInstance()->GetItemLocation(location)->GetPlacedRandomizerGet();
        std::deque<std::string>& candidates = names.at(item);
        placed.emplace(location, candidates.front());
        candidates.pop_front();
    };
    for (const RemakeChest& chest : chests) {
        if (chest.shuffled) {
            place(chest.location);
        }
    }
    for (const RemakeShopSlot& slot : shops) {
        place(slot.location);
    }
    for (const RemakeGift& gift : gifts) {
        place(gift.location);
    }
    for (const RemakeSkulltula& skulltula : skulltulas) {
        place(skulltula.location);
    }
    for (const RemakeSongPlace& song : songs) {
        place(song.location);
    }
    return placed;
}

std::vector<RemakeShopSlot> LoadRemakeShops() {
    std::vector<RemakeShopSlot> slots;
    std::set<RandomizerCheck> locations;
    size_t line = 1;
    for (const auto& row :
         ReadTable("remake/shops.tsv", shopTable, "location\tnumber\tslot\tshopkeepers\tshelves\tgoods")) {
        const std::string where = "remake/shops.tsv line " + std::to_string(++line) + ": ";
        const auto location = Rando::StaticData::locationNameToEnum.find(row[0]);
        if (location == Rando::StaticData::locationNameToEnum.end() ||
            Rando::StaticData::GetLocation(location->second)->GetRCType() != RCTYPE_SHOP ||
            !locations.insert(location->second).second) {
            Fail(where + "\"" + row[0] + "\" is not a SoH shop location, or repeats");
        }
        const std::string next = std::to_string(slots.size() % 8 + 1);
        if (row[1] != next) {
            Fail(where + "the number is not " + next + ", as each shop's rows come together, items 1 to 8 in turn");
        }
        if (row[2].size() != 1 || row[2][0] < '0' || row[2][0] > '7') {
            Fail(where + "the slot is not 0 to 7");
        }
        RemakeShopSlot slot{ .location = location->second, .number = row[1][0] - '0', .slot = row[2][0] - '0' };
        for (const std::string& cell : SohUtils::StringSplit(row[3], ",")) {
            const size_t colon = cell.find(':');
            slot.shopkeepers.push_back({ .actor = cell.substr(0, colon), .goodsList = cell.substr(colon + 1) });
        }
        for (const std::string& cell : SohUtils::StringSplit(row[4], ",")) {
            const size_t colon = cell.rfind(':');
            slot.shelves.push_back({ .file = cell.substr(0, colon), .hash = cell.substr(colon + 1) });
        }
        slots.push_back(std::move(slot));
    }
    for (const Rando::Location& location : Rando::StaticData::GetLocationTable()) {
        if (location.GetRCType() == RCTYPE_SHOP && !locations.contains(location.GetRandomizerCheck())) {
            Fail("remake/shops.tsv: no row for the SoH shop location \"" + location.GetName() + "\"");
        }
    }
    return slots;
}

void CheckShopGoods(const std::vector<RemakeChest>& chests, const std::vector<RemakeGift>& gifts) {
    std::set<std::string> holdable{ std::string(FillerName), std::string(TokenName) };
    for (const RemakeChest& chest : chests) {
        if (chest.shuffled && !KeptFromShops(chest.sohItem)) {
            holdable.insert(chest.item);
        }
    }
    for (const RemakeGift& gift : gifts) {
        if (!KeptFromShops(gift.sohItem)) {
            holdable.insert(gift.item);
        }
    }
    std::set<std::string> rows;
    size_t line = 1;
    for (const auto& row :
         ReadTable("remake/shop_goods.tsv", shopGoodsTable, "item\tgoods\tpose\tbottom\ttop\twidth\treach")) {
        const std::string where = std::format("remake/shop_goods.tsv line {}: ", ++line);
        if (!holdable.contains(row[0]) || !rows.insert(row[0]).second) {
            Fail(where + "\"" + row[0] + "\" is no item a shop slot can hold, or repeats one");
        }
    }
    for (const std::string& item : holdable) {
        if (!rows.contains(item)) {
            Fail("remake/shop_goods.tsv: no row for " + item + ", which a shop slot can hold");
        }
    }
}

static std::vector<RemakeGift> LoadGives(const std::string& file, std::span<const unsigned char> table,
                                         const std::vector<RemakeChest>& chests) {
    const std::map<std::string, std::vector<RandomizerGet>> items = LoadItems();
    std::vector<RemakeGift> gifts;
    size_t line = 1;
    for (const auto& row : ReadTable(file, table, "location\tflow\tevent\tparam\titem")) {
        const std::string where = std::format("{} line {}: ", file, ++line);
        const std::string& name = row[0];
        const std::string& flow = row[1];
        const std::string& item = row[4];
        const RandomizerCheck location = LocationNamed(where, name);
        if (std::ranges::find(chests, location, &RemakeChest::location) != chests.end()) {
            Fail(where + "\"" + name + "\" is a location of remake/chests.tsv");
        }
        if (!flow.starts_with("Event/EventFlow/") || !flow.ends_with(".bfevfl.zs")) {
            Fail(where + "\"" + flow + "\" is not an event flow under Event/EventFlow/");
        }
        const auto candidates = items.find(item);
        if (candidates == items.end() || candidates->second.size() != 1) {
            Fail(where + "\"" + item + "\" is not an item of remake/items.tsv that stands for one SoH item");
        }
        auto gift = std::ranges::find(gifts, location, &RemakeGift::location);
        if (gift == gifts.end()) {
            gift = gifts.insert(
                gifts.end(), RemakeGift{ .location = location, .item = item, .sohItem = candidates->second.front() });
        } else if (gift->item != item) {
            Fail(where + "\"" + name + "\" gives another item at another site");
        }
        gift->sites.push_back({ .flow = flow, .event = row[2], .param = row[3] });
    }
    return gifts;
}

std::vector<RemakeGift> LoadRemakeGifts(const std::vector<RemakeChest>& chests) {
    return LoadGives("remake/gifts.tsv", giftTable, chests);
}

std::vector<RemakeGift> LoadRemakeRewards(const std::vector<RemakeChest>& chests) {
    return LoadGives("remake/rewards.tsv", rewardTable, chests);
}

std::vector<RemakeSkulltula> LoadRemakeSkulltulas() {
    std::vector<RemakeSkulltula> skulltulas;
    std::set<RandomizerCheck> locations;
    std::set<std::string> hashes;
    size_t line = 1;
    for (const auto& row :
         ReadTable("remake/skulltulas.tsv", skulltulaTable, "location\thash\tfiles\tplace\tpairing\tnote")) {
        const std::string where = std::format("remake/skulltulas.tsv line {}: ", ++line);
        const auto location = Rando::StaticData::locationNameToEnum.find(row[0]);
        if (location == Rando::StaticData::locationNameToEnum.end() ||
            Rando::StaticData::GetLocation(location->second)->GetRCType() != RCTYPE_SKULL_TOKEN ||
            Rando::StaticData::GetLocation(location->second)->GetQuest() == RCQUEST_MQ ||
            !locations.insert(location->second).second) {
            Fail(where + "\"" + row[0] + "\" is not a Gold Skulltula of SoH's vanilla layouts, or repeats");
        }
        const std::string& hash = row[1];
        if (hash.empty() || hash.find_first_not_of("0123456789") != std::string::npos || !hashes.insert(hash).second) {
            Fail(where + "the hash \"" + hash + "\" is not a decimal number or repeats");
        }
        const RemakeChest::Actor actor{ hash, SohUtils::StringSplit(row[2], ",") };
        CheckSceneFiles(where, actor);
        Rando::Location* soh = Rando::StaticData::GetLocation(location->second);
        if (const std::string& place = row[3]; !place.empty()) {
            const RandomizerCheckArea area = AreaNamed(where, place);
            if (area == soh->GetArea()) {
                Fail(where + "the place \"" + place + "\" is SoH's own area for this location");
            }
            soh->SetArea(area);
        }
        if (const std::string& pairing = row[4]; pairing != "certain" && (pairing != "likely" || row[5].empty())) {
            Fail(where + "the pairing is neither certain nor likely, or a likely row has no note");
        }
        skulltulas.push_back({ .location = location->second, .actor = actor });
    }
    for (const Rando::Location& location : Rando::StaticData::GetLocationTable()) {
        if (location.GetRCType() == RCTYPE_SKULL_TOKEN && location.GetQuest() != RCQUEST_MQ &&
            !locations.contains(location.GetRandomizerCheck())) {
            Fail("remake/skulltulas.tsv: no row for the SoH Gold Skulltula \"" + location.GetName() + "\"");
        }
    }
    return skulltulas;
}

std::vector<RemakeSongPlace> LoadRemakeSongs() {
    const std::map<std::string, std::vector<RandomizerGet>> items = LoadItems();
    std::vector<RemakeSongPlace> places;
    std::set<RandomizerCheck> locations;
    std::set<int> melodies;
    size_t line = 1;
    for (const auto& row :
         ReadTable("remake/songs.tsv", songTable, "location\tsong\tmelody\tlesson\texamples\tfollows\tline\tgift")) {
        const std::string where = "remake/songs.tsv line " + std::to_string(++line) + ": ";
        const RandomizerCheck location = LocationNamed(where, row[0]);
        const auto song = items.find(row[1]);
        if (Rando::StaticData::GetLocation(location)->GetRCType() != RCTYPE_SONG_LOCATION ||
            !locations.insert(location).second || song == items.end() || song->second.size() != 1 ||
            song->second.front() != Rando::StaticData::GetLocation(location)->GetVanillaItem()) {
            Fail(where + "not a SoH song location, a repeated one, or a song that is not the location's");
        }
        int melody = 0;
        const auto [end, error] = std::from_chars(row[2].data(), row[2].data() + row[2].size(), melody);
        const size_t colon = row[7].find(':');
        RemakeSongPlace place{ .location = location,
                               .song = row[1],
                               .sohSong = song->second.front(),
                               .melody = melody,
                               .lesson = row[3],
                               .examples = SohUtils::StringSplit(row[4], ","),
                               .follows = SohUtils::StringSplit(row[5], ","),
                               .line = row[6],
                               .giftFlow = row[7].substr(0, colon),
                               .giftEvent = colon == std::string::npos ? "" : row[7].substr(colon + 1) };
        const auto isFlow = [](const std::string& path) {
            return path.starts_with("Event/EventFlow/") && path.ends_with(".bfevfl.zs");
        };
        if (error != std::errc() || end != row[2].data() + row[2].size() || !melodies.insert(melody).second ||
            !isFlow(place.lesson) || !isFlow(place.giftFlow) || place.giftEvent.empty() ||
            !place.line.starts_with("EventFlowMsg/")) {
            Fail(where + "a melody that is not a whole number or repeats, or a flow, gift or line out of place");
        }
        places.push_back(std::move(place));
    }
    for (const Rando::Location& location : Rando::StaticData::GetLocationTable()) {
        if (location.GetRCType() == RCTYPE_SONG_LOCATION && !locations.contains(location.GetRandomizerCheck())) {
            Fail("remake/songs.tsv: no row for the SoH song location \"" + location.GetName() + "\"");
        }
    }
    return places;
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

// After the 20-token reward, Event371 of the House of Skulltula's flow opens the Stone of Agony's guide once the
// reward's flag is set, whatever the reward gave. Reading the flag the guide sets once shown, it ends the talk on
// either answer.
static constexpr EventText agonyGuideShut = { "Event/EventFlow/GatheringTokenGold.bfevfl.zs", "Event371",
                                              "GameDataName",
                                              "KakarikoVillage_GatheringTokenGold_ActionGuide_AgonyStone_True" };

// Event texts every seed that shuffles the songs carries.
static constexpr EventText songFixes[] = {
    // The windmill man starts his lesson only while Link does not know the Song of Storms. A Link who learns it
    // elsewhere would never get the windmill's song, and one who learns another song there would get the lesson again
    // at every ocarina. He asks whether the lesson has played instead.
    { "Event/EventFlow/Npc_Hylia_009_Adult.bfevfl.zs", "Event17", "GameDataName",
      "HavePlayedEvent.Dm_MelodyGet_Storm" },
    { "Event/EventFlow/Npc_Hylia_009_Adult.bfevfl.zs", "Event69", "GameDataName",
      "HavePlayedEvent.Dm_MelodyGet_Storm" },
    // The Bolero's scene marks the Bolero known itself, whatever song it teaches. It marks its own lesson played
    // instead, which the game also does as the scene plays.
    { "Event/EventFlow/Dm_MelodyGet_Fire.bfevfl.zs", "Event1", "GameDataName", "HavePlayedEvent.Dm_MelodyGet_Fire" },
};

struct Presence {
    std::string_view file;
    std::string_view hash;
    std::string_view flag;
};

// Presences every seed that shuffles the songs carries. With the Requiem learned elsewhere, Link can warp to the Desert
// Colossus before the Gerudo Fortress. Walking out into the Wasteland and back plays the Wasteland's arrival, and the
// Spirit Temple moves the quest on too; either takes the Spirit quest past the fortress's step, after which no
// carpenter's thief comes. So the Colossus's way into the Wasteland and the temple's front entrance stand, for both
// ages, only once the quest is past Step02, which Link reaches by entering the Wasteland from the fortress.
static constexpr std::string_view colossusAdult = "Banc/DesertColossus/DesertColossus_Adult_Static.bcett.byml.zs";
static constexpr std::string_view colossusChild = "Banc/DesertColossus/DesertColossus_Child_Static.bcett.byml.zs";
static constexpr std::string_view pastSpiritStep02 = "SageOfSpirit_IsAfter_Step02_Exp";
static constexpr Presence colossusGates[] = {
    { colossusAdult, "12141577802165947392", pastSpiritStep02 }, // the way into the Wasteland
    { colossusAdult, "13187812046230253833", pastSpiritStep02 }, // the Spirit Temple's front entrance
    { colossusChild, "18409754566529742762", pastSpiritStep02 },
    { colossusChild, "15448624735210695325", pastSpiritStep02 },
};

static constexpr std::string_view smallKeyDoors[] = {
    "1163369963105284964",  "2293725469462648563",  "3225055400115517869",  "5481916789431949409",
    "17124821462463640959", "530640766699404174",   "8886594954069367001",  "8924322743887267313",
    "17520323088057735070", "17586687124735290487", "17649976694470179290", "18033163985131850496",
    "8936252369722474095",  "14915175233807466033", "15319553007255647544", "3785621893886752067",
    "7859741725793759796",  "14964409186081456860", "15639855627873906140", "17286348970163505298",
    "1239725692126790626",  "1598277279179137240",  "5866554311248263901",  "8208400900970984454",
    "14562151304140999752", "3180753716225647997",  "15964434394101837654", "1782957447756991766",
    "4054769999674403462",  "4861722785202029276",  "9161635131903213609",  "11215020957634035781",
    "11279749999185960905", "13610050855796186379", "13961115460808423078", "16099041652987252990",
    "2559901135200913892",
};

// Opening a boss door with the key plays its own scene, and the Spirit Temple's starts the Iron Knuckle fight, so a new
// game holds the key and the door stays locked.
static constexpr std::string_view bossKeyDungeons[] = {
    "ForestTemple", "FireTemple", "WaterTemple", "ShadowTemple", "SpiritTemple", "InsideGanonCastle",
};

static nlohmann::ordered_json StartValues(const SeedSettings& settings) {
    nlohmann::ordered_json values = nlohmann::ordered_json::object();
    for (const char* flag : startFlags) {
        values[flag] = true;
    }
    if (settings.forest == Forest::Open) {
        // Mido no longer blocks the way to the Deku Tree, and the invisible wall beside him is gone.
        values["KokiriForest_Npc_Kokiri_Mido_Child_Blocker"] = false;
    }
    if (settings.forest != Forest::Closed) {
        // The Kokiri at the exit no longer blocks the way out, and the invisible wall beside him is gone.
        values["KokiriForest_Npc_Kokiri_007_Child_Blocker_00"] = false;
    }
    if (settings.songs == Songs::ShuffledAmongThemselves) {
        // A child can learn a warp song and, grown up, warp out of the Temple of Time before he walks through its door.
        // The scene at the door plays only while AllSage stands at Step01, and the adult's world map waits for it.
        values["HavePlayedEvent.Dm_TimeTemple_0065"] = true;
    }
    if (settings.bridge == Bridge::AlwaysOpen) {
        // The bridge's logic module builds it once its event has played, whatever the Ganon quest's step.
        values["HavePlayedEvent.GanonCastle_Adult_Field_Rainbow"] = true;
    }
    if (settings.trials == Trials::None) {
        // Each trial counts done and none is left. Nothing in the game lowers the tower's barrier when no trial is left
        // to finish, so it starts down. The elevator in the barrier comes only once Rauru's scene has played.
        values["InsideGanonCastle_Dungeon_Barrier_Count"] = 0;
        values["InsideGanonCastle_IsAllBarrierClear"] = true;
        values["HavePlayedEvent.Dm_InsideGanonCastle_0090"] = true;
        values["InsideGanonCastle_Dungeon_SeeBarrierGuide"] = true;
        for (std::string_view trial : { "Forest", "Water", "Shadow", "Fire", "Light", "Spirit" }) {
            values[std::format("InsideGanonCastle_Dungeon_BarrierRemove_{}", trial)] = true;
            values[std::format("HavePlayedEvent.InsideGanonCastle_Dungeon_BarrierRemove_{}", trial)] = true;
        }
    }
    if (settings.keys == DungeonItems::GivenAtTheStart) {
        for (std::string_view door : smallKeyDoors) {
            values[std::format("IsChainLockDoorOpen.{}", door)] = true;
        }
        for (std::string_view dungeon : bossKeyDungeons) {
            values[std::format("DungeonInfo.{}.IsGetBossKey", dungeon)] = true;
        }
    }
    if (settings.mapsAndCompasses == DungeonItems::GivenAtTheStart) {
        // The Training Ground and Ganon's Castle have neither.
        for (const Dungeon& dungeon : Dungeons) {
            if (dungeon.folder != "GerudoTrainingGround" && dungeon.folder != "InsideGanonCastle") {
                const std::string info = std::format("DungeonInfo.{}.", dungeon.folder);
                values[info + "IsGetMap"] = true;
                values[info + "IsGetCompass"] = true;
            }
        }
    }
    return values;
}

nlohmann::ordered_json RemakePlan(const std::string& seed, const std::vector<RemakeChest>& chests,
                                  const std::vector<RemakeChest>& looseItems,
                                  const std::vector<RemakeShopSlot>& shops, const std::vector<RemakeGift>& gifts,
                                  const std::vector<RemakeSkulltula>& skulltulas,
                                  const std::vector<RemakeSongPlace>& songs, const SeedSettings& settings,
                                  const std::map<RandomizerCheck, std::string>& items,
                                  const std::vector<StoneHint>& hints, const std::vector<MessageText>& messages) {
    nlohmann::ordered_json plan = { { "seed", seed }, { "chests", nlohmann::ordered_json::array() } };
    for (const RemakeChest& chest : chests) {
        if (!chest.shuffled) {
            continue;
        }
        for (const RemakeChest::Actor& actor : chest.actors) {
            plan["chests"].push_back(
                { { "hash", actor.hash }, { "files", actor.files }, { "drop_actor", items.at(chest.location) } });
        }
    }
    plan["presences"] = nlohmann::ordered_json::array();
    if (!songs.empty()) {
        for (const auto& [file, hash, flag] : colossusGates) {
            plan["presences"].push_back({ { "file", file }, { "hash", hash }, { "flag", flag } });
        }
    }
    const auto addText = [&](std::string_view flow, std::string_view event, std::string_view param,
                             std::string_view text) {
        plan["event_texts"].push_back({ { "flow", flow }, { "event", event }, { "param", param }, { "text", text } });
    };
    for (const auto& [flow, event, param, text] : eventTexts) {
        addText(flow, event, param, text);
    }
    for (const RemakeGift& gift : gifts) {
        for (const RemakeGift::Site& site : gift.sites) {
            addText(site.flow, site.event, site.param, items.at(gift.location));
        }
        if (gift.location == RC_KAK_20_GOLD_SKULLTULA_REWARD) {
            addText(agonyGuideShut.flow, agonyGuideShut.event, agonyGuideShut.param, agonyGuideShut.text);
        }
    }
    if (!songs.empty()) {
        for (const auto& [flow, event, param, text] : songFixes) {
            addText(flow, event, param, text);
        }
    }
    plan["event_numbers"] = nlohmann::ordered_json::array();
    for (const RemakeSongPlace& place : songs) {
        const RemakeSongPlace& taught = *std::ranges::find(songs, items.at(place.location), &RemakeSongPlace::song);
        for (const std::string& event : place.examples) {
            addText(place.lesson, event, "MessageId", taught.line);
        }
        addText(place.giftFlow, place.giftEvent, "ActorName", taught.song);
        for (const std::vector<std::string>* events : { &place.examples, &place.follows }) {
            for (const std::string& event : *events) {
                plan["event_numbers"].push_back({ { "flow", place.lesson },
                                                  { "event", event },
                                                  { "param", "MelodyType" },
                                                  { "value", taught.melody } });
            }
        }
    }
    plan["start_values"] = StartValues(settings);
    plan["stone_texts"] = nlohmann::ordered_json::array();
    for (const StoneHint& hint : hints) {
        plan["stone_texts"].push_back({ { "label", hint.stone.label }, { "text", hint.tagged } });
    }
    plan["message_texts"] = nlohmann::ordered_json::array();
    for (const MessageText& message : messages) {
        plan["message_texts"].push_back(
            { { "file", message.file }, { "label", message.label }, { "text", message.tagged } });
    }
    plan["shops"] = nlohmann::ordered_json::array();
    for (const RemakeShopSlot& slot : shops) {
        const Rando::ItemLocation* location = Rando::Context::GetInstance()->GetItemLocation(slot.location);
        nlohmann::ordered_json shopkeepers = nlohmann::ordered_json::array();
        for (const RemakeShopSlot::Shopkeeper& keeper : slot.shopkeepers) {
            shopkeepers.push_back({ { "actor", keeper.actor }, { "goods_list", keeper.goodsList } });
        }
        nlohmann::ordered_json shelves = nlohmann::ordered_json::array();
        for (const RemakeShopSlot::Shelf& shelf : slot.shelves) {
            shelves.push_back({ { "file", shelf.file }, { "hash", shelf.hash } });
        }
        plan["shops"].push_back({ { "shopkeepers", shopkeepers },
                                  { "slot", slot.slot },
                                  { "shelves", shelves },
                                  { "item", items.at(slot.location) },
                                  { "price", location->GetPrice() } });
    }
    // Only with the exit open and Mido still blocking can Link meet Zelda before Mido lets him by.
    plan["mido_castle_entry_removed"] = settings.forest == Forest::ClosedDeku;
    plan["opening_skipped"] = settings.opening == Opening::Skipped;
    plan["skulltulas"] = nlohmann::ordered_json::array();
    for (const RemakeSkulltula& skulltula : skulltulas) {
        plan["skulltulas"].push_back({ { "hash", skulltula.actor.hash },
                                       { "files", skulltula.actor.files },
                                       { "item", items.at(skulltula.location) } });
    }
    plan["loose_items"] = nlohmann::ordered_json::array();
    for (const RemakeChest& looseItem : looseItems) {
        const std::string& item = items.at(looseItem.location);
        if (item != looseItem.item) {
            const RemakeChest::Actor& actor = looseItem.actors.front();
            plan["loose_items"].push_back({ { "hash", actor.hash }, { "files", actor.files }, { "item", item } });
        }
    }
    plan["start_items"] = nlohmann::ordered_json::array();
    for (const StartStep& started : StartWithInOrder(settings)) {
        for (const StartStep& step : StepsTo(started)) {
            plan["start_items"].push_back(step.actor);
        }
    }
    return plan;
}
