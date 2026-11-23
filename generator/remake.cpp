#include "remake.h"

#include "soh/Enhancements/randomizer/3drando/fill.hpp"
#include "soh/Enhancements/randomizer/3drando/shops.hpp"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/dungeon.h"
#include "soh/Enhancements/randomizer/logic.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/util.h"

#include <algorithm>
#include <charconv>
#include <deque>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
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
static constexpr unsigned char keptGoodsTable[] = {
#embed "../remake/kept_goods.tsv"
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
static constexpr unsigned char fountainClaimTable[] = {
#embed "../remake/fountain_claims.tsv"
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
static constexpr unsigned char areaSceneTable[] = {
#embed "../remake/area_scenes.tsv"
};
static constexpr unsigned char tutorialTable[] = {
#embed "../remake/tutorials.tsv"
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
                                     "location\tfile\tlabel\tcopy_of\tshown_at\tsentence\tline_length\tbox_lines")) {
        const std::string where = std::format("remake/prize_lines.tsv line {}: ", ++line);
        const RandomizerCheck location = LocationNamed(where, row[0]);
        if (std::ranges::find(chests, location, &RemakeChest::location) == chests.end() &&
            std::ranges::find(gifts, location, &RemakeGift::location) == gifts.end()) {
            Fail(where + "\"" + row[0] + "\" is no chest of remake/chests.tsv and no gift");
        }
        if (row[1].empty() || row[2].empty()) {
            Fail(where + "no file or label");
        }
        std::optional<MessageCopy> copy;
        if (!row[3].empty() || !row[4].empty()) {
            const std::vector<std::string> shown = SohUtils::StringSplit(row[4], ":");
            if (row[3].empty() || shown.size() != 3 || !row[1].ends_with(".msbt")) {
                Fail(where + "a copy names the label it copies and flow:event:parameter that shows it");
            }
            copy = MessageCopy{ .of = row[3], .flow = shown[0], .event = shown[1], .param = shown[2] };
        }
        tables.prizeLines.push_back({ .location = location,
                                      .file = row[1],
                                      .label = row[2],
                                      .copy = copy,
                                      .sentence = Split(where, row[5]),
                                      .fit = ReadFit(where, row[6], row[7]) });
    }

    line = 1;
    for (const auto& row : ReadTable("remake/character_hints.tsv", characterHintTable,
                                     "hint\titem\tfile\tlabel\tsent_on\tsentence\tline_length\tbox_lines")) {
        const std::string where = std::format("remake/character_hints.tsv line {}: ", ++line);
        const RandomizerCheck hint = LocationNamed(where, row[0]);
        const auto item = Rando::StaticData::itemNameToEnum.find(row[1]);
        if (Rando::StaticData::GetLocation(hint)->GetRCType() != RCTYPE_STATIC_HINT ||
            item == Rando::StaticData::itemNameToEnum.end() || item->second == RG_NONE || row[2].empty() ||
            row[3].empty()) {
            Fail(where + "no SoH hint location, an unknown SoH item, or no file or label");
        }
        std::optional<EventNext> sentOn;
        if (!row[4].empty()) {
            const std::vector<std::string> sent = SohUtils::StringSplit(row[4], ":");
            if (sent.size() != 3 || std::ranges::any_of(sent, [](const std::string& part) { return part.empty(); })) {
                Fail(where + "sent_on is not flow:event:next");
            }
            sentOn = EventNext{ .flow = sent[0], .event = sent[1], .next = sent[2] };
        }
        tables.characterHints.push_back({ .hint = hint,
                                          .item = item->second,
                                          .file = row[2],
                                          .label = row[3],
                                          .sentOn = sentOn,
                                          .sentence = Split(where, row[5]),
                                          .fit = ReadFit(where, row[6], row[7]) });
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

std::map<RandomizerGet, KeptGoods> LoadKeptGoods() {
    std::map<RandomizerGet, KeptGoods> kept;
    size_t line = 1;
    for (const auto& row : ReadTable("remake/kept_goods.tsv", keptGoodsTable, "item\tgoods\tprice")) {
        const std::string where = "remake/kept_goods.tsv line " + std::to_string(++line) + ": ";
        const auto item = Rando::StaticData::itemNameToEnum.find(row[0]);
        if (item == Rando::StaticData::itemNameToEnum.end() ||
            Rando::StaticData::RetrieveItem(item->second).GetItemType() != ITEMTYPE_SHOP) {
            Fail(where + "\"" + row[0] + "\" is no SoH item a shop sells");
        }
        int price = 0;
        const auto [end, error] = std::from_chars(row[2].data(), row[2].data() + row[2].size(), price);
        if (row[1].empty() || error != std::errc() || end != row[2].data() + row[2].size() ||
            !kept.emplace(item->second, KeptGoods{ .goods = row[1], .price = price }).second) {
            Fail(where + "no goods actor, a price that is no number, or a repeated item");
        }
    }
    for (const RandomizerGet item : GetMinVanillaShopItems(0)) {
        if (!kept.contains(item)) {
            Fail("remake/kept_goods.tsv: no row for \"" + Rando::StaticData::RetrieveItem(item).GetName().GetEnglish() +
                 "\", which SoH keeps on a shop's shelf");
        }
    }
    return kept;
}

std::string KeptGoodsName(RandomizerGet item) {
    const std::string name = Rando::StaticData::RetrieveItem(item).GetName().GetEnglish();
    return name.substr(0, name.find(" ["));
}

void CheckShopGoods(const std::vector<RemakeChest>& chests, const std::vector<RemakeGift>& gifts) {
    std::set<std::string> holdable{ std::string(FillerName), std::string(TokenName) };
    for (const RemakeChest& chest : chests) {
        if (chest.shuffled && !KeptFromShops(chest.sohItem) && !KeptInChests(chest.sohItem)) {
            holdable.insert(chest.item);
        }
    }
    for (const RemakeGift& gift : gifts) {
        if (!KeptFromShops(gift.sohItem) && !KeptInChestsAndCalls(gift.sohItem) && !KeptInChests(gift.sohItem)) {
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

static constexpr std::string_view GivesHeader = "location\tflow\tevent\tparam\tgives\titem";

static std::vector<RemakeGift> LoadGives(const std::string& file, const std::vector<std::vector<std::string>>& rows,
                                         const std::vector<RemakeChest>& chests) {
    const std::map<std::string, std::vector<RandomizerGet>> items = LoadItems();
    std::vector<RemakeGift> gifts;
    size_t line = 1;
    for (const auto& row : rows) {
        const std::string where = std::format("{} line {}: ", file, ++line);
        const std::string& name = row[0];
        const std::string& flow = row[1];
        const std::string& item = row[5];
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
        // With the place left out of the shuffle, the logic counts what SoH's location list says it gives.
        if (candidates->second.front() != Rando::StaticData::GetLocation(location)->GetVanillaItem()) {
            Fail(where + "\"" + item + "\" is not the remake item for what the location gives in SoH");
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

std::vector<GroupedGift> LoadRemakeGifts(const std::vector<RemakeChest>& chests) {
    const std::string file = "remake/gifts.tsv";
    const auto rows = ReadTable(file, giftTable, std::string(GivesHeader) + "\tgroup\tplace");
    std::map<RandomizerCheck, GiftGroup> groups;
    std::map<RandomizerCheck, std::string> places;
    size_t line = 1;
    for (const auto& row : rows) {
        const std::string where = std::format("{} line {}: ", file, ++line);
        const RandomizerCheck location = LocationNamed(where, row[0]);
        const auto group = std::ranges::find(GiftGroups, row[6], &GiftGroup::name);
        if (group == std::end(GiftGroups) || groups.try_emplace(location, *group).first->second != *group) {
            Fail(where + "\"" + row[6] + "\" is no group of \"Gifts\", or not the group of the location's other rows");
        }
        if (places.try_emplace(location, row[7]).first->second != row[7]) {
            Fail(where + "\"" + row[7] + "\" is not the place of the location's other rows");
        }
    }
    // A gift that stands outside its location's SoH area moves the location into the area of its place, as a skulltula
    // does, so hints and the barren areas name where it is.
    for (const auto& [location, place] : places) {
        if (!place.empty()) {
            Rando::Location* soh = Rando::StaticData::GetLocation(location);
            const RandomizerCheckArea area = AreaNamed(file + ": ", place);
            if (area == soh->GetArea()) {
                Fail(file + ": the place \"" + place + "\" is SoH's own area for " + soh->GetName());
            }
            soh->SetArea(area);
        }
    }
    for (const GiftGroup& group : GiftGroups) {
        if (std::ranges::count(std::views::values(groups), group) == 0) {
            Fail(file + ": no row of the group \"" + std::string(group.name) + "\"");
        }
    }
    std::vector<GroupedGift> gifts;
    for (RemakeGift& gift : LoadGives(file, rows, chests)) {
        gifts.push_back({ groups.at(gift.location), std::move(gift) });
    }
    line = 1;
    for (const auto& row :
         ReadTable("remake/fountain_claims.tsv", fountainClaimTable, "location\tflow\tpaid\tquestion\tclaim")) {
        const std::string where = std::format("remake/fountain_claims.tsv line {}: ", ++line);
        const RandomizerCheck location = LocationNamed(where, row[0]);
        const auto grouped = std::ranges::find(gifts, location, [](const GroupedGift& g) { return g.gift.location; });
        constexpr std::string_view prefix = "Kaepora_Once_";
        if (grouped == gifts.end() || grouped->gift.claim || grouped->gift.sites.front().flow != row[1] ||
            !row[4].starts_with(prefix) || row[4].size() == prefix.size() ||
            !std::ranges::all_of(row[4].substr(prefix.size()), [](char c) { return c >= '0' && c <= '9'; })) {
            Fail(where + "\"" + row[0] +
                 "\" is no gift of remake/gifts.tsv, has a claim already or another flow, or \"" + row[4] +
                 "\" is not Kaepora_Once_ and a number");
        }
        grouped->gift.claim =
            RemakeGift::Claim{ .name = row[4],
                               .paid = { .flow = row[1], .event = row[2], .param = "GameDataName" },
                               .question = { .flow = row[1], .event = row[3], .param = "GameDataName" } };
    }
    return gifts;
}

std::vector<RemakeGift> LoadRemakeRewards(const std::vector<RemakeChest>& chests) {
    const std::string file = "remake/rewards.tsv";
    return LoadGives(file, ReadTable(file, rewardTable, std::string(GivesHeader)), chests);
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
    // The world's time stands still until the owl's scene on the child's first walk into Hyrule Field sets
    // HyruleField_ArriveEntrance, and the afternoon stops at 15:00 until Impa's send-off film has played. SoH's logic
    // gives a child day and night from the start, so with both set the time runs from the first frame and through the
    // night.
    "HyruleField_ArriveEntrance",
    "HavePlayedEvent.Dm_HrlField_0020",
};

struct EventText {
    std::string_view flow;
    std::string_view event;
    std::string_view param;
    std::string_view text;
};

// Darunia dances at each Saria's Song from his first meeting on, and his switch after the dance (Event39 of the Fire
// quest's flow) starts his gift's scene while the quest is not past SariaMelodyPlay. Opening Dodongo's Cavern first
// takes the quest past it, and five writes after the dance take it back and give again. So the switch asks this
// expression of the mod's own, true once the gift's scene has played. An expression query reads no Bool.
static constexpr std::string_view danceGiven = "Kaepora_HavePlayedEvent_Dm_Goron_0030_Exp";
static constexpr std::string_view danceGiftPlayed = "HavePlayedEvent.Dm_Goron_0030";

static constexpr std::string_view stonesAfterStep01 = "Kaepora_BothStones_AND_DoorOfTime_IsAfter_Step01_Exp";

// Darunia's son's own schedule, and the flag his gift talk sets as it opens the door. Its entries [1] to [5] apply once
// the Fire quest is complete: [1] holds on the quest complete, and [2] to [5] on four unsaved flags that arrivals and
// areas of Goron City set only then. All five start the same talk, and it gives nothing. Which of two entries that hold
// the game takes is not known, so each of the five holds only once the door's flag is set as well, and until then the
// entry that starts his gift talk is the only one that holds.
static constexpr std::string_view daruniaJrSchedule =
    "Component/AIScheduleParam/Npc_Goron_DaruniaJr_Adult.game__component__AIScheduleParam.bgyml";
static constexpr std::string_view daruniaJrDoorOpen = "SageOfFire_Step02_DoorOpen";
static constexpr std::string_view daruniaJrAfterTempleTalk = "Npc_Goron_DaruniaJr_Adult_C1C2C3C4C5_TTTTT";
// Each of the five by the flag it holds on in the game, with the expression of the mod's own it holds on instead.
static constexpr std::pair<std::string_view, std::string_view> daruniaJrAfterTemple[] = {
    { "SageOfFire_IsCompleted_Exp", "Kaepora_SageOfFire_IsCompleted_AND_DoorOpen_Exp" },
    { "SageOfFire_FromCrater", "Kaepora_SageOfFire_FromCrater_AND_DoorOpen_Exp" },
    { "SageOfFire_FromCrater_FarJr", "Kaepora_SageOfFire_FromCrater_FarJr_AND_DoorOpen_Exp" },
    { "SageOfFire_FromDeathMt", "Kaepora_SageOfFire_FromDeathMt_AND_DoorOpen_Exp" },
    { "SageOfFire_FromDeathMt2", "Kaepora_SageOfFire_FromDeathMt2_AND_DoorOpen_Exp" },
};

static constexpr EventText eventTexts[] = {
    // The warp after Gohma completes the Forest quest in its Event1. Its Event4 sets the castle quest to Step01 even when
    // the quest is further on, and a Link who woke Talon before Gohma can then not meet Zelda. These texts make Event4 a
    // second copy of Event1.
    { "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "Event4", "ChallengeName", "Get_StoneOfForest" },
    { "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "Event4", "StepName", "Complete" },
    // The Forest Temple's exit completes the Forest Sage's quest in its Event199. Its Event208 then sets AllSage to
    // Step01_00 even when the quest is further on. Once the Shadow and the Spirit sages have woken, AllSage stands at
    // Step02, the one step at which Zelda's scene starts as Link walks into the Temple of Time, and nothing sets Step02
    // again. These texts give Event208 the quest and step of Event199, and Event208 keeps its own flags. The game drops
    // a write to a completed quest whatever its flags say.
    { "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "Event208", "ChallengeName", "SageOfForest" },
    { "Event/EventFlow/Dm_SY_BancChange_EscapeDungeon.bfevfl.zs", "Event208", "StepName", "Complete" },
    // Kaepora Gaebora waits at the foot of Zora's River once the Dodongo's Cavern Bomb Bag Chest is open. His Event80
    // sets the Water quest to Step02 even when the quest is further on, and a Link who has shown King Zora Ruto's
    // letter is then barred from Zora's Fountain. Event80 runs only after Impa's talk, which comes only once the Fire
    // quest is complete, so these texts make it complete the Fire quest, a write the game ignores on a completed quest.
    { "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "Event80", "ChallengeName", "Get_StoneOfFire" },
    { "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "Event80", "StepName", "Complete" },
    // The diving game's first win, from Event293 on, sets the Water quest to Step05 whenever the quest is past
    // Step03_Impa. Once King Zora has taken Ruto's letter the quest is at Step07, so a first win then sets it back, and
    // King Zora sits in the way to Zora's Fountain again with the letter gone. Asking whether the domain is the adult's
    // after Morpha, which it never is for the child who plays the game, the win writes no step. In the usual order
    // (arrive, talk, win, letter) the quest then stays at Step04 until the lake's letter writes Step05_01, and nothing
    // tells Step04 from Step05.
    { "Event/EventFlow/Get_StoneOfWater.bfevfl.zs", "Event293", "GameDataName",
      "Sequence_IsAlternative_ZoraDomainAdult" },
    // Impa teaches her song only from her talk while the Door of Time's quest is at Step01, and stands in the courtyard
    // only until the quest is past Step01. Zelda's talk after her scene (Event42) and Saria's Song on the ocarina
    // (Event172) set the quest to Srep2_1_1, so a Link who took the Ruby before he met Zelda can lose Impa's lesson to
    // either. Both write only once the Fire quest is complete, so these texts make them complete the Fire quest, a
    // write the game ignores on a completed quest.
    { "Event/EventFlow/Npc_Hylia_Zelda_Child.bfevfl.zs", "Event42", "ChallengeName", "Get_StoneOfFire" },
    { "Event/EventFlow/Npc_Hylia_Zelda_Child.bfevfl.zs", "Event42", "StepName", "Complete" },
    { "Event/EventFlow/Dm_SY_Ocarina_SariaSong.bfevfl.zs", "Event172", "ChallengeName", "Get_StoneOfFire" },
    { "Event/EventFlow/Dm_SY_Ocarina_SariaSong.bfevfl.zs", "Event172", "StepName", "Complete" },
    // A warp song takes a child out of the courtyard after Zelda's scene and before Impa's lesson. The stone check,
    // DoorOfTime_CheckStoneNum, runs as each stone dungeon ends and writes Step2_1 (Event147), Step2_2 (Event148) or
    // Step03 (Event37) whatever step the quest stands at, so a stone dungeon finished after that warp sends Impa
    // away. These texts make Event38 ask for both stones and the quest past Step01 at once, so Step03 waits for both
    // and for Impa's send-off, which writes Step02 and then runs the check. Event147 then runs no more. Event148 runs
    // only with the Water quest complete, so they make it complete the Water quest, a write the game ignores on a
    // completed quest. The quest stays at Step01 until the send-off, and at Step02 until both stones are done.
    { "Event/EventFlow/DoorOfTime.bfevfl.zs", "Event38", "GameDataName", stonesAfterStep01 },
    { "Event/EventFlow/DoorOfTime.bfevfl.zs", "Event148", "ChallengeName", "Get_StoneOfWater" },
    { "Event/EventFlow/DoorOfTime.bfevfl.zs", "Event148", "StepName", "Complete" },
    // After Link loses Ingo's bet race on a horse that is not Epona, adult Malon's talk sets Epona's quest to
    // GuideToLonLonRanchAdult in Event177 whenever Event179 finds the quest not past ReadyForGetEponaSong. A Link who
    // met child Malon at the ranch and has not learned her song is set back, and she then never teaches it. This text
    // makes Event179 ask whether the quest is past Ready, so the write moves only a quest still at Ready.
    { "Event/EventFlow/Npc_Hylia_Malon_Adult.bfevfl.zs", "Event179", "GameDataName", "Get_Epona_IsAfter_Ready_Exp" },
    { "Event/EventFlow/Get_StoneOfFire.bfevfl.zs", "Event39", "GameDataName", danceGiven },
};

// The summit's Great Fairy gives through her own entry of the shared get flow. For a Link who had no meter, which her
// flow marks before the gift, that entry reserves the meter's birth and plays it, asking the mark before each. The
// birth ends only when its animation reaches its last frame (read from the program). With an item that brings no meter
// there is none to be born, so the plan points both questions at her claim, false until her paid path writes it after
// the gift.
static constexpr std::string_view summitMeterBirthFlow = "Event/EventFlow/Dm_SY_GetInEvent.bfevfl.zs";
static constexpr std::string_view summitMeterBirthQuestions[] = { "Event555", "Event554" };

static bool BringsTheMeter(RandomizerGet item) {
    return item == RG_PROGRESSIVE_MAGIC_METER ||
           std::ranges::find(Rando::SpellsLensAndMagicArrows, item) != Rando::SpellsLensAndMagicArrows.end();
}

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
    std::optional<std::string_view> replaces;
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

// In the domain an adult sees once the Water Sage's quest is complete, King Zora's ice stands only while that quest is
// not complete, so it never stands. Only that ice starts his thaw, which gives what his place holds.
static constexpr Presence kingZoraIce = { .file = "Banc/ZoraDomain/ZoraDomain_Adult_Alternative_Static.bcett.byml.zs",
                                          .hash = "3341274640047764997",
                                          .flag = "HavePlayedEvent.SageOfWater_KingZora_IsMelted_Ice",
                                          .replaces = "SageOfWater_IsCompleted_Exp" };

// Zora's River keeps seven invisible walls at its two boulders while both are whole and the Water quest is below
// Step03 (ZoraRiver_Common_08f0), and the adult's river places them too. SoH lets an adult by with nothing, and with
// the Door of Time open he can come before the child's Water quest. So in the adult's river they stand only for a
// child, which is never.
static constexpr std::string_view adultRiver = "Banc/ZoraRiver/ZoraRiver_Adult_Static.bcett.byml.zs";
static constexpr Presence riverWalls[] = {
    { adultRiver, "7850395794748438732", "Sequence_IsChildhood" },
    { adultRiver, "13781426553798151043", "Sequence_IsChildhood" },
    { adultRiver, "7982339630050931435", "Sequence_IsChildhood" },
    { adultRiver, "14077907143773739344", "Sequence_IsChildhood" },
    { adultRiver, "9371429508151251838", "Sequence_IsChildhood" },
    { adultRiver, "11266001672277786960", "Sequence_IsChildhood" },
    { adultRiver, "4565321854739336115", "Sequence_IsChildhood" },
};

// The presence every seed that shuffles Malon's place carries. The area at the top of the vine climb in the castle
// grounds hatches the Chicken for any Link while the castle quest is at Step03, the step her talk writes
// (HyruleCastle_Prefab_ee1b). So it stands only for a Link whose save says he got the Weird Egg, as the egg's own timer
// asks, and the game gives an area of Hyrule Field a presence of that kind on the Hookshot's record.
static constexpr Presence weirdEggHatch = { .file = "Banc/HyruleCastle/HyruleCastle_Static.bcett.byml.zs",
                                            .hash = "4190489777303842820",
                                            .flag = "IsGet.Item_WeirdEgg" };

// Biggoron's place. His hand-over (Event123) writes the trade quest complete at its end (Event89), and with the quest
// complete adult Anju offers nothing and the game drops every later step of the chain. So with his place shuffled he
// gives once on a claim of the mod's own, as SoH's Biggoron gives once on a flag of its own, and the quest never
// completes.
static constexpr std::string_view biggoronFlow = "Event/EventFlow/Npc_Goron_Biggoron_Adult.bfevfl.zs";
// Kaepora_Once_ and the hash of Biggoron's placement.
static constexpr std::string_view biggoronClaim = "Kaepora_Once_14299450766367161284";
// His question at the hand-over whether the quest is complete asks an expression, and an expression query reads no
// Bool, so it asks this expression of the mod's own, which reads the claim. His plain talk asks the same question
// (Event238) only past Step10, which no seed that shuffles his place reaches, so it keeps the game's question.
static constexpr std::string_view biggoronGiven = "Kaepora_Once_14299450766367161284_Exp";
static constexpr EventText biggoronTexts[] = {
    // His talk after the quest (AfterCompleted) writes his talk flag here, and only the question at the hand-over
    // calls that talk. The write records the claim instead.
    { biggoronFlow, "Event21", "GameDataName", biggoronClaim },
    { biggoronFlow, "Event49", "GameDataName", biggoronGiven },
};

struct ModuleText {
    std::string_view file;
    std::string_view from;
    std::string_view to;
};

// The Door of Time, in both ages' Temple of Time, opens once its quest is at Step06, which the Song of Time at the
// altar writes after Zelda, the stones and the Ocarina of Time. The child's pedestal starts the first pull at that step
// exactly, and later pulls once AllSage is past Ready. AllSage waits on nothing, stands at Ready in a new game, and
// only the first pull's last scene writes its first step. So the door opens on AllSage at Ready or after, and the first
// pull waits for AllSage at Ready exactly.
static constexpr std::string_view doorOfTimeQuest = "Work/Challenge/DoorOfTime.game__challenge__Challenge.gyml";
static constexpr std::string_view allSageQuest = "Work/Challenge/AllSage.game__challenge__Challenge.gyml";
static constexpr ModuleText doorOpen[] = {
    { "Logic/Market_Common_f252.logic.module.ainb", doorOfTimeQuest, allSageQuest },
    { "Logic/Market_Common_f252.logic.module.ainb", "Step06", "Ready" },
    { "Logic/Market_Child_Prefab_e700.logic.module.ainb", doorOfTimeQuest, allSageQuest },
    { "Logic/Market_Child_Prefab_e700.logic.module.ainb", "Step06", "Ready" },
};
// Rauru's scene after the first pull writes the Door of Time's quest complete, with no test. The engine drops the write
// before Zelda's courtyard, where the quest is at NotReady. After Zelda and before the altar's Song of Time, it would
// take what is left of Impa's lesson, the escape, the moat's Ocarina of Time and the Song of Time from the child, so it
// names the Light sage's quest, which the next scene completes.
static constexpr EventText rauruCompletesTheDoor = { "Event/EventFlow/DoorOfTime.bfevfl.zs", "Event93", "ChallengeName",
                                                     "SageOfLight" };

static const EventNext biggoronNexts[] = {
    // The hand-over's last line goes on to that write and his talk about the receipt, past the write of Complete.
    { std::string(biggoronFlow), "Event112", "Event21" },
    // Link hands over the Giant's Knife or the broken knife with the receipt if he holds one (Event53, Event54). The
    // hand-over takes neither and goes on to the wait's question.
    { std::string(biggoronFlow), "Event53", "Event71" },
    // The get flow's entry for his sword waits after the give (Event636) until the Biggoron Sword is equipped, unless
    // the Master Sword is, so with another item it can wait for ever. Only his hand-over calls that entry.
    { "Event/EventFlow/Dm_SY_GetInEvent.bfevfl.zs", "Event334", "Event392" },
};

// Biggoron's wait asks for 72 game hours since the eye-drops scene wrote this record, and is false while it is 0 (read
// in the program). Started at 1, a receipt found before that scene gives after three game days from a new game, SoH's
// forge time.
static constexpr std::string_view swordOrderedAt = "GetSwordBigGoron_ElapsedTime";

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
    if (std::ranges::any_of(settings.startWith,
                            [](const StartStep& step) { return step.actor == "Weapon_Sword_Kokiri"; })) {
        // Link's house swaps in the plain door for a Link who holds the sword, so the forest's first scene never plays,
        // and its name banner is the only thing that marks the forest visited. The forest's banner area and the close
        // of its first guide wait for Saria's run-up, which follows that scene. Her talk still waits for Link.
        values["IsVisitLocation.KokiriForest"] = true;
        values["HavePlayedEvent.Dm_Kokiri_0050"] = true;
    }
    if (settings.skips.contains(Skip::SariasGreeting)) {
        // With Saria's run-up played, the house has the plain door from the first frame, so the forest's first scene
        // and the run-up after it never play. With her talk played, her stop at the ladder and the talk's entry in her
        // schedule are gone. The first scene's banner is the only thing that marks the forest visited.
        for (const char* played : { "Dm_Kokiri_0045", "Dm_Kokiri_0050", "Dm_Kokiri_0060" }) {
            values[std::format("HavePlayedEvent.{}", played)] = true;
        }
        values["IsVisitLocation.KokiriForest"] = true;
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

static const nlohmann::ordered_json& AreaSceneCuts() {
    static const nlohmann::ordered_json cuts = [] {
        nlohmann::ordered_json read = { { "event_entries", nlohmann::ordered_json::array() },
                                        { "event_nexts", nlohmann::ordered_json::array() } };
        size_t line = 1;
        for (const auto& row : ReadTable("remake/area_scenes.tsv", areaSceneTable, "area\tflow\tkind\tfrom\tto")) {
            const std::string where = std::format("remake/area_scenes.tsv line {}: ", ++line);
            const std::string& flow = row[1];
            const std::string& kind = row[2];
            const std::string& from = row[3];
            const std::string& to = row[4];
            if (!flow.starts_with("Event/EventFlow/") || !flow.ends_with(".bfevfl.zs") || from.empty() ||
                (kind == "entry" ? to.empty() : kind != "next")) {
                Fail(where + "not a flow's entry point starting at an event, or an event going on to another or none");
            }
            if (kind == "entry") {
                read["event_entries"].push_back({ { "flow", flow }, { "entry", from }, { "event", to } });
            } else {
                read["event_nexts"].push_back(
                    { { "flow", flow },
                      { "event", from },
                      { "next", to.empty() ? nlohmann::ordered_json() : nlohmann::ordered_json(to) } });
            }
        }
        return read;
    }();
    return cuts;
}

static const std::vector<std::pair<std::string, bool>>& TutorialValues() {
    static const std::vector<std::pair<std::string, bool>> values = [] {
        std::vector<std::pair<std::string, bool>> read;
        size_t line = 1;
        for (const auto& row : ReadTable("remake/tutorials.tsv", tutorialTable, "tutorial\tvalue\tstart")) {
            const std::string where = std::format("remake/tutorials.tsv line {}: ", ++line);
            if (row[1].empty() || (row[2] != "true" && row[2] != "false")) {
                Fail(where + "not a value and true or false");
            }
            read.emplace_back(row[1], row[2] == "true");
        }
        return read;
    }();
    return values;
}

// The two lessons no start value stops, each as the event that goes on past it. The first walk into the Deku Tree goes
// on from the scene to the camera's reset, past Navi's talk and her guide screen, which opens with no check that it
// has opened before. The castle Great Fairy's gift ends its cut at the get, past the spin attack's guide, which opens
// the same way and is the cut's last event in the game.
static const EventNext tutorialNexts[] = {
    { "Event/EventFlow/Dm_SY_BancChangeArrive.bfevfl.zs", "Event9", "Event10" },
    { "Event/EventFlow/Dm_FairyFountain_Power.bfevfl.zs", "Event141", std::nullopt },
};

nlohmann::ordered_json RemakePlan(const std::string& seed, const std::vector<RemakeChest>& chests,
                                  const std::vector<RemakeChest>& looseItems, const RemakeShops& shops,
                                  const std::vector<RemakeGift>& gifts,
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
    plan["presences"].push_back({ { "file", kingZoraIce.file },
                                  { "hash", kingZoraIce.hash },
                                  { "flag", kingZoraIce.flag },
                                  { "replaces", *kingZoraIce.replaces } });
    for (const Presence& wall : riverWalls) {
        plan["presences"].push_back({ { "file", wall.file }, { "hash", wall.hash }, { "flag", wall.flag } });
    }
    if (!songs.empty()) {
        for (const Presence& gate : colossusGates) {
            plan["presences"].push_back({ { "file", gate.file }, { "hash", gate.hash }, { "flag", gate.flag } });
        }
    }
    const bool biggoron = std::ranges::find(gifts, RC_DMT_TRADE_CLAIM_CHECK, &RemakeGift::location) != gifts.end();
    if (std::ranges::find(gifts, RC_HC_MALON_EGG, &RemakeGift::location) != gifts.end()) {
        plan["presences"].push_back(
            { { "file", weirdEggHatch.file }, { "hash", weirdEggHatch.hash }, { "flag", weirdEggHatch.flag } });
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
        if (gift.claim) {
            for (const RemakeGift::Site& site : { gift.claim->paid, gift.claim->question }) {
                addText(site.flow, site.event, site.param, gift.claim->name);
            }
            const RandomizerGet placed =
                Rando::Context::GetInstance()->GetItemLocation(gift.location)->GetPlacedRandomizerGet();
            if (gift.location == RC_DMT_GREAT_FAIRY_DINS_FIRE && !BringsTheMeter(placed)) {
                for (const std::string_view event : summitMeterBirthQuestions) {
                    addText(summitMeterBirthFlow, event, "GameDataName", gift.claim->name);
                }
            }
        }
        if (gift.location == RC_KAK_20_GOLD_SKULLTULA_REWARD) {
            addText(agonyGuideShut.flow, agonyGuideShut.event, agonyGuideShut.param, agonyGuideShut.text);
        }
    }
    if (biggoron) {
        for (const auto& [flow, event, param, text] : biggoronTexts) {
            addText(flow, event, param, text);
        }
    }
    if (!songs.empty()) {
        for (const auto& [flow, event, param, text] : songFixes) {
            addText(flow, event, param, text);
        }
    }
    const bool doorOpened = settings.doorOfTime == DoorOfTime::Open;
    if (doorOpened) {
        addText(rauruCompletesTheDoor.flow, rauruCompletesTheDoor.event, rauruCompletesTheDoor.param,
                rauruCompletesTheDoor.text);
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
    plan["event_nexts"] = nlohmann::ordered_json::array();
    plan["module_texts"] = nlohmann::ordered_json::array();
    if (doorOpened) {
        for (const auto& [file, from, to] : doorOpen) {
            plan["module_texts"].push_back({ { "file", file }, { "from", from }, { "to", to } });
        }
    }
    const auto addNext = [&](const EventNext& next) {
        plan["event_nexts"].push_back(
            { { "flow", next.flow },
              { "event", next.event },
              { "next", next.next ? nlohmann::ordered_json(*next.next) : nlohmann::ordered_json() } });
    };
    plan["event_entries"] = nlohmann::ordered_json::array();
    plan["start_values"] = StartValues(settings);
    if (biggoron) {
        for (const EventNext& next : biggoronNexts) {
            addNext(next);
        }
        plan["start_values"][swordOrderedAt] = 1;
    }
    plan["stone_texts"] = nlohmann::ordered_json::array();
    for (const StoneHint& hint : hints) {
        plan["stone_texts"].push_back({ { "label", hint.stone.label }, { "text", hint.tagged } });
    }
    plan["message_texts"] = nlohmann::ordered_json::array();
    for (const MessageText& message : messages) {
        if (message.sentOn) {
            addNext(*message.sentOn);
        }
        nlohmann::ordered_json& planned = plan["message_texts"].emplace_back(
            nlohmann::ordered_json{ { "file", message.file }, { "label", message.label } });
        if (message.copy) {
            planned["copy_of"] = message.copy->of;
            // A flow names a message by its file without the extension and its label.
            addText(message.copy->flow, message.copy->event, message.copy->param,
                    message.file.substr(0, message.file.size() - std::string_view(".msbt").size()) + ":" +
                        message.label);
        }
        planned["text"] = message.tagged;
    }
    if (settings.skips.contains(Skip::NewAreaScenes)) {
        // Each scene still runs as an event, so the game writes its played flag as it ends, and its banner marks the
        // area visited.
        for (const auto& [list, cuts] : AreaSceneCuts().items()) {
            for (const nlohmann::ordered_json& cut : cuts) {
                plan[list].push_back(cut);
            }
        }
    }
    if (settings.skips.contains(Skip::TutorialMessages)) {
        for (const auto& [name, value] : TutorialValues()) {
            plan["start_values"][name] = value;
        }
        for (const EventNext& next : tutorialNexts) {
            addNext(next);
        }
    }
    plan["shops"] = nlohmann::ordered_json::array();
    const auto shopSlot = [](const RemakeShopSlot& slot) {
        nlohmann::ordered_json shopkeepers = nlohmann::ordered_json::array();
        for (const RemakeShopSlot::Shopkeeper& keeper : slot.shopkeepers) {
            shopkeepers.push_back({ { "actor", keeper.actor }, { "goods_list", keeper.goodsList } });
        }
        nlohmann::ordered_json shelves = nlohmann::ordered_json::array();
        for (const RemakeShopSlot::Shelf& shelf : slot.shelves) {
            shelves.push_back({ { "file", shelf.file }, { "hash", shelf.hash } });
        }
        return nlohmann::ordered_json{ { "shopkeepers", shopkeepers }, { "slot", slot.slot }, { "shelves", shelves } };
    };
    for (const RemakeShopSlot& slot : shops.shuffled) {
        nlohmann::ordered_json& planned = plan["shops"].emplace_back(shopSlot(slot));
        planned["item"] = items.at(slot.location);
        planned["price"] = Rando::Context::GetInstance()->GetItemLocation(slot.location)->GetPrice();
    }
    for (const RemakeShopSlot& slot : shops.kept) {
        const Rando::ItemLocation* location = Rando::Context::GetInstance()->GetItemLocation(slot.location);
        plan["shops"].emplace_back(shopSlot(slot))["goods"] = shops.goods.at(location->GetPlacedRandomizerGet()).goods;
    }
    // Only with the exit open and Mido still blocking can Link meet Zelda before Mido lets him by.
    plan["mido_castle_entry_removed"] = settings.forest == Forest::ClosedDeku;
    // Darunia's son gives what the Goron Tunic's place holds only as a participant of the Fire quest, up to its step
    // TalkDaruniaJr. Past it his own schedule applies, and its entry for a son no bomb has hit talks with no gift. That
    // entry holds instead until his gift has opened the door, and starts his gift talk.
    plan["schedule_entries"] = nlohmann::ordered_json::array();
    plan["schedule_entries"].push_back({ { "file", daruniaJrSchedule },
                                         { "replaces", "SageOfFire_Step01_DaruniaJr_BombHit" },
                                         { "flag", daruniaJrDoorOpen },
                                         { "talk", "1396283892_1460050088_2425371882_C1_T" } });
    for (const auto& [flag, joined] : daruniaJrAfterTemple) {
        plan["schedule_entries"].push_back({ { "file", daruniaJrSchedule },
                                             { "replaces", flag },
                                             { "flag", joined },
                                             { "talk", daruniaJrAfterTempleTalk } });
    }
    plan["opening_skipped"] = settings.skips.contains(Skip::OpeningMovie);
    plan["cutscenes_skipped"] = settings.skips.contains(Skip::SkippableScenes);
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
    plan["claims"] = nlohmann::ordered_json::array();
    for (const RemakeGift& gift : gifts) {
        if (gift.claim) {
            plan["claims"].push_back(gift.claim->name);
        }
    }
    plan["expressions"] = nlohmann::ordered_json::array();
    plan["expressions"].push_back(
        { { "name", danceGiven }, { "all_of", nlohmann::ordered_json::array({ danceGiftPlayed }) } });
    plan["expressions"].push_back({ { "name", stonesAfterStep01 },
                                    { "all_of", nlohmann::ordered_json::array({ "Get_StoneOfFire_IsCompleted_Exp",
                                                                                "Get_StoneOfWater_IsCompleted_Exp",
                                                                                "DoorOfTime_IsAfter_Step01_Exp" }) } });
    for (const auto& [flag, joined] : daruniaJrAfterTemple) {
        plan["expressions"].push_back(
            { { "name", joined }, { "all_of", nlohmann::ordered_json::array({ flag, daruniaJrDoorOpen }) } });
    }
    if (biggoron) {
        plan["claims"].push_back(biggoronClaim);
        plan["expressions"].push_back(
            { { "name", biggoronGiven }, { "all_of", nlohmann::ordered_json::array({ biggoronClaim }) } });
    }
    plan["start_items"] = nlohmann::ordered_json::array();
    for (const StartStep& started : StartWithInOrder(settings)) {
        for (const StartStep& step : StepsTo(started)) {
            plan["start_items"].push_back(step.actor);
        }
    }
    return plan;
}
