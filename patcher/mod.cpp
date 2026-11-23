#include "mod.h"

#include "bfevfl.h"
#include "byml.h"
#include "bytes.h"
#include "compression.h"
#include "game_file.h"
#include "msbt.h"
#include "new_actor.h"
#include "program_patch.h"
#include "sarc.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>

static uint64_t ParseHash(const nlohmann::json& json) {
    const std::string text = json.get<std::string>();
    uint64_t hash = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), hash);
    if (error != std::errc() || end != text.data() + text.size()) {
        throw std::runtime_error(std::format("hash \"{}\" is not a 64-bit decimal number", text));
    }
    return hash;
}

static PlannedChest ParseChest(const nlohmann::json& chest) {
    const PlannedChest planned{ .hash = ParseHash(chest.at("hash")),
                                .files = chest.at("files").get<std::vector<std::string>>(),
                                .dropActor = chest.at("drop_actor").get<std::string>() };
    if (planned.files.empty() || planned.dropActor.empty()) {
        throw std::runtime_error("files and drop_actor must not be empty");
    }
    return planned;
}

static PlannedPresence ParsePresence(const nlohmann::json& presence) {
    PlannedPresence planned{ .actor = { .file = presence.at("file").get<std::string>(),
                                        .hash = ParseHash(presence.at("hash")) },
                             .flag = presence.at("flag").get<std::string>() };
    if (planned.actor.file.empty() || planned.flag.empty()) {
        throw std::runtime_error("file and flag must not be empty");
    }
    if (presence.contains("replaces")) {
        planned.replaces = presence.at("replaces").get<std::string>();
    }
    return planned;
}

static PlannedEventText ParseEventText(const nlohmann::json& text) {
    PlannedEventText planned{ .flow = text.at("flow").get<std::string>(),
                              .event = text.at("event").get<std::string>(),
                              .param = text.at("param").get<std::string>(),
                              .text = text.at("text").get<std::string>() };
    if (planned.text.empty()) {
        throw std::runtime_error("text must not be empty");
    }
    return planned;
}

static PlannedEventNumber ParseEventNumber(const nlohmann::json& number) {
    const nlohmann::json& value = number.at("value");
    if (!value.is_number_integer() || value < std::numeric_limits<int32_t>::min() ||
        value > std::numeric_limits<int32_t>::max()) {
        throw std::runtime_error("value must be a whole number");
    }
    return { .flow = number.at("flow").get<std::string>(),
             .event = number.at("event").get<std::string>(),
             .param = number.at("param").get<std::string>(),
             .value = value.get<int32_t>() };
}

// A slot sells an item at a price, or the game's goods actor named by "goods".
static std::variant<ItemSale, GoodsSale> ParseSale(const nlohmann::json& slot) {
    if (slot.contains("goods")) {
        if (slot.contains("item") || slot.contains("price")) {
            throw std::runtime_error("a slot sells goods or an item, not both");
        }
        GoodsSale sale{ .goods = slot.at("goods").get<std::string>() };
        if (sale.goods.empty()) {
            throw std::runtime_error("goods must not be empty");
        }
        return sale;
    }
    const nlohmann::json& price = slot.at("price");
    if (!price.is_number_unsigned() || price > std::numeric_limits<int32_t>::max()) {
        throw std::runtime_error("price must be a whole number of rupees");
    }
    ItemSale sale{ .item = slot.at("item").get<std::string>(), .price = price.get<int32_t>() };
    if (sale.item.empty()) {
        throw std::runtime_error("item must not be empty");
    }
    return sale;
}

static PlannedShopSlot ParseShopSlot(const nlohmann::json& slot) {
    PlannedShopSlot planned{ .slot = slot.at("slot").get<uint32_t>(), .sale = ParseSale(slot) };
    for (const nlohmann::json& keeper : slot.at("shopkeepers")) {
        planned.shopkeepers.push_back(
            { .actor = keeper.at("actor").get<std::string>(), .goodsList = keeper.at("goods_list").get<std::string>() });
    }
    for (const nlohmann::json& shelf : slot.at("shelves")) {
        planned.shelves.push_back({ .file = shelf.at("file").get<std::string>(), .hash = ParseHash(shelf.at("hash")) });
    }
    if (planned.shopkeepers.empty() || planned.shelves.empty()) {
        throw std::runtime_error("shopkeepers and shelves must not be empty");
    }
    return planned;
}

static PlannedActorItem ParseActorItem(const nlohmann::json& entry) {
    PlannedActorItem planned{ .hash = ParseHash(entry.at("hash")),
                              .files = entry.at("files").get<std::vector<std::string>>(),
                              .item = entry.at("item").get<std::string>() };
    if (planned.files.empty() || planned.item.empty()) {
        throw std::runtime_error("files and item must not be empty");
    }
    return planned;
}

static PlannedScheduleEntry ParseScheduleEntry(const nlohmann::json& entry) {
    return { .file = entry.at("file").get<std::string>(),
             .replaces = entry.at("replaces").get<std::string>(),
             .flag = entry.at("flag").get<std::string>(),
             .talk = entry.at("talk").get<std::string>() };
}

static std::string Name(std::string_view flow, std::string_view event, std::string_view param) {
    return std::format("{} {} in {}", event, param, flow);
}

// A rumour opens with the stones' colour, their smaller text and code 3:0, and sets the size and colour again after a
// page break, as the game's own rumours do in every language.
constexpr std::string_view RumourOpening = "{color:1}{size:80}{3:0:0f0000}";
constexpr std::string_view RumourPageReset = "{size:80}{color:1}";

// A table of remake/ built into the program: each row's fields, after the header given. Throws when the header is
// another, the last line has no newline, or a row has another number of fields.
static std::vector<std::vector<std::string_view>> ReadTable(std::string_view name, std::string_view table,
                                                            std::string_view header) {
    if (!table.starts_with(std::format("{}\n", header)) || !table.ends_with('\n')) {
        throw std::runtime_error(
            std::format("{}: the header is not \"{}\", or the last line has no newline", name, header));
    }
    const size_t fields = std::ranges::count(header, '\t') + 1;
    std::vector<std::vector<std::string_view>> rows;
    for (size_t start = header.size() + 1; start < table.size(); start = table.find('\n', start) + 1) {
        std::vector<std::string_view>& row = rows.emplace_back();
        for (const auto field : std::views::split(table.substr(start, table.find('\n', start) - start), '\t')) {
            row.emplace_back(field.begin(), field.end());
        }
        if (row.size() != fields) {
            throw std::runtime_error(std::format("{} line {}: wrong number of fields", name, rows.size() + 1));
        }
    }
    return rows;
}

static constexpr char stoneTable[] = {
#embed "../remake/stones.tsv"
};

static std::set<std::string, std::less<>> PairedStones() {
    std::set<std::string, std::less<>> labels;
    for (const auto& row : ReadTable("remake/stones.tsv", { stoneTable, sizeof stoneTable }, "label\tlocation")) {
        labels.emplace(row[0]);
    }
    return labels;
}

static PlannedStoneText ParseStoneText(const nlohmann::json& stone) {
    static const std::set<std::string, std::less<>> paired = PairedStones();
    const std::string label = stone.at("label").get<std::string>();
    const std::string words = stone.at("text").get<std::string>();
    if (label.empty() || words.empty()) {
        throw std::runtime_error("label and text must not be empty");
    }
    if (!paired.contains(label)) {
        throw std::runtime_error(std::format("{} is not a stone of remake/stones.tsv", label));
    }
    std::string text = std::string(RumourOpening) + words;
    for (size_t page = text.find(msbt::PageTag); page != std::string::npos; page = text.find(msbt::PageTag, page + 1)) {
        text.insert(page + msbt::PageTag.size(), RumourPageReset);
    }
    return { .label = label, .text = msbt::Encode(text) };
}

static PlannedMessageText ParseMessageText(const nlohmann::json& message) {
    PlannedMessageText planned{ .file = message.at("file").get<std::string>(),
                                .label = message.at("label").get<std::string>(),
                                .text = message.at("text").get<std::string>() };
    if (message.contains("copy_of")) {
        planned.copyOf = message.at("copy_of").get<std::string>();
    }
    if (planned.file.empty() || planned.label.empty() || planned.copyOf == "" || planned.text.empty()) {
        throw std::runtime_error("file, label, copy_of and text must not be empty");
    }
    planned.text = msbt::Encode(planned.text);
    return planned;
}

Plan ParsePlan(const nlohmann::json& json) {
    const auto chests = json.at("chests").get<std::vector<nlohmann::json>>();
    const auto texts = json.at("event_texts").get<std::vector<nlohmann::json>>();
    const auto numbers = json.at("event_numbers").get<std::vector<nlohmann::json>>();
    Plan plan;
    for (const auto& [name, value] : json.at("start_values").get<std::map<std::string, nlohmann::json>>()) {
        if (value.is_boolean()) {
            plan.startValues.emplace(name, value.get<bool>());
        } else if (value.is_number_integer() && value >= std::numeric_limits<int32_t>::min() &&
                   value <= std::numeric_limits<int32_t>::max()) {
            plan.startValues.emplace(name, value.get<int32_t>());
        } else {
            throw std::runtime_error(std::format("start_values: \"{}\" is not true, false or a whole number", name));
        }
    }
    const auto stones = json.at("stone_texts").get<std::vector<nlohmann::json>>();
    for (size_t i = 0; i < chests.size(); ++i) {
        try {
            plan.chests.push_back(ParseChest(chests[i]));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("chests[{}]: {}", i, e.what()));
        }
    }
    std::set<std::tuple<std::string, std::string, std::string>> named;
    for (size_t i = 0; i < texts.size(); ++i) {
        try {
            const PlannedEventText& text = plan.eventTexts.emplace_back(ParseEventText(texts[i]));
            if (!named.emplace(text.flow, text.event, text.param).second) {
                throw std::runtime_error(std::format("{} is listed twice", Name(text.flow, text.event, text.param)));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("event_texts[{}]: {}", i, e.what()));
        }
    }
    for (size_t i = 0; i < numbers.size(); ++i) {
        try {
            const PlannedEventNumber& number = plan.eventNumbers.emplace_back(ParseEventNumber(numbers[i]));
            if (!named.emplace(number.flow, number.event, number.param).second) {
                throw std::runtime_error(
                    std::format("{} is listed twice", Name(number.flow, number.event, number.param)));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("event_numbers[{}]: {}", i, e.what()));
        }
    }
    std::set<std::string> labels;
    for (size_t i = 0; i < stones.size(); ++i) {
        try {
            const PlannedStoneText& stone = plan.stoneTexts.emplace_back(ParseStoneText(stones[i]));
            if (!labels.insert(stone.label).second) {
                throw std::runtime_error(std::format("{} is listed twice", stone.label));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("stone_texts[{}]: {}", i, e.what()));
        }
    }
    const auto messages = json.at("message_texts").get<std::vector<nlohmann::json>>();
    std::set<std::pair<std::string, std::string>> messageLabels;
    for (size_t i = 0; i < messages.size(); ++i) {
        try {
            const PlannedMessageText& message = plan.messageTexts.emplace_back(ParseMessageText(messages[i]));
            if (!messageLabels.emplace(message.file, message.label).second) {
                throw std::runtime_error(std::format("{} of {} is listed twice", message.label, message.file));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("message_texts[{}]: {}", i, e.what()));
        }
    }
    const auto shops = json.at("shops").get<std::vector<nlohmann::json>>();
    for (size_t i = 0; i < shops.size(); ++i) {
        try {
            plan.shops.push_back(ParseShopSlot(shops[i]));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("shops[{}]: {}", i, e.what()));
        }
    }
    plan.startItems = json.at("start_items").get<std::vector<std::string>>();
    for (const std::string& item : plan.startItems) {
        if (std::ranges::count(plan.startItems, item) > 1) {
            throw std::runtime_error(std::format("start_items: {} is listed twice", item));
        }
    }
    plan.claims = json.at("claims").get<std::vector<std::string>>();
    for (const std::string& claim : plan.claims) {
        if (std::ranges::count(plan.claims, claim) > 1) {
            throw std::runtime_error(std::format("claims: {} is listed twice", claim));
        }
    }
    const auto expressions = json.at("expressions").get<std::vector<nlohmann::json>>();
    for (size_t i = 0; i < expressions.size(); ++i) {
        const PlannedExpression& expression = plan.expressions.emplace_back(
            expressions[i].at("name").get<std::string>(), expressions[i].at("all_of").get<std::vector<std::string>>());
        if (expression.allOf.empty()) {
            throw std::runtime_error(std::format("expressions[{}]: {} names nothing", i, expression.name));
        }
        if (std::ranges::count(plan.expressions, expression.name, &PlannedExpression::name) > 1) {
            throw std::runtime_error(std::format("expressions[{}]: {} is listed twice", i, expression.name));
        }
    }
    const auto nexts = json.at("event_nexts").get<std::vector<nlohmann::json>>();
    std::set<std::pair<std::string, std::string>> sent;
    for (size_t i = 0; i < nexts.size(); ++i) {
        try {
            const PlannedEventNext& next = plan.eventNexts.emplace_back(nexts[i].at("flow").get<std::string>(),
                                                                        nexts[i].at("event").get<std::string>(),
                                                                        nexts[i].at("next").get<std::string>());
            if (!sent.emplace(next.flow, next.event).second) {
                throw std::runtime_error(std::format("{} in {} is listed twice", next.event, next.flow));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("event_nexts[{}]: {}", i, e.what()));
        }
    }
    plan.midoCastleEntryRemoved = json.at("mido_castle_entry_removed").get<bool>();
    const auto scheduleEntries = json.at("schedule_entries").get<std::vector<nlohmann::json>>();
    for (size_t i = 0; i < scheduleEntries.size(); ++i) {
        try {
            plan.scheduleEntries.push_back(ParseScheduleEntry(scheduleEntries[i]));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("schedule_entries[{}]: {}", i, e.what()));
        }
    }
    plan.openingSkipped = json.at("opening_skipped").get<bool>();
    plan.cutscenesSkipped = json.at("cutscenes_skipped").get<bool>();
    for (const auto& [key, list] : { std::pair{ "skulltulas", &plan.skulltulas },
                                     std::pair{ "loose_items", &plan.looseItems } }) {
        const auto entries = json.at(key).get<std::vector<nlohmann::json>>();
        for (size_t i = 0; i < entries.size(); ++i) {
            try {
                list->push_back(ParseActorItem(entries[i]));
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("{}[{}]: {}", key, i, e.what()));
            }
        }
    }
    const auto presences = json.at("presences").get<std::vector<nlohmann::json>>();
    std::set<std::pair<std::string, uint64_t>> placed;
    for (size_t i = 0; i < presences.size(); ++i) {
        try {
            const PlannedPresence& presence = plan.presences.emplace_back(ParsePresence(presences[i]));
            if (!placed.emplace(presence.actor.file, presence.actor.hash).second) {
                throw std::runtime_error(
                    std::format("actor {} in {} is listed twice", presence.actor.hash, presence.actor.file));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("presences[{}]: {}", i, e.what()));
        }
    }
    return plan;
}

using byml::Find;
using byml::FindOrAdd;
using byml::FloatOf;

static byml::Node* FindHash(byml::Node& array, uint64_t hash) {
    const auto found = std::ranges::find_if(array.array, [&](byml::Node& item) {
        const byml::Node* itemHash = Find(item, "Hash");
        return itemHash != nullptr && itemHash->scalar == hash;
    });
    return found == array.array.end() ? nullptr : &*found;
}

static byml::Node& Actor(byml::Node& placement, uint64_t hash) {
    byml::Node* actors = Find(placement, "Actors");
    byml::Node* actor = actors == nullptr ? nullptr : FindHash(*actors, hash);
    if (actor == nullptr) {
        throw std::runtime_error("no actor in the file has this hash");
    }
    return *actor;
}

static bool SetGyaml(byml::Node& placed, const std::string& actor) {
    byml::Node* gyaml = Find(placed, "Gyaml");
    if (gyaml == nullptr || gyaml->type != byml::Type::String) {
        throw std::runtime_error("the actor has no Gyaml");
    }
    return std::exchange(gyaml->string, actor) != actor;
}

// Items whose own actor is a pickup that the game places loose. Fire Arrows are not one: their pickup's tag makes
// taking it move the lake's Fire Arrows quest on, so it stays only at the lake, where the game places it.
constexpr std::array<std::string_view, 2> LoosePickups = { "Obj_Heart_Container", "Obj_Heart_Piece" };

static constexpr char shopGoodsTable[] = {
#embed "../remake/shop_goods.tsv"
};

// How the model of an item sold from goods of its own stands in the pose a shop plays for it, in millimetres.
struct GoodsModel {
    int32_t bottom;
    int32_t top;
    int32_t width;
    int32_t reach;
};

// A row of remake/shop_goods.tsv: the game's goods actor that sells the item, or the model of goods of its own.
using ShopGoods = std::variant<std::string, GoodsModel>;

static const ShopGoods& Goods(std::string_view item) {
    static const std::map<std::string, ShopGoods, std::less<>> rows = [] {
        std::map<std::string, ShopGoods, std::less<>> rows;
        for (const auto& row : ReadTable("remake/shop_goods.tsv", { shopGoodsTable, sizeof shopGoodsTable },
                                         "item\tgoods\tpose\tbottom\ttop\twidth\treach")) {
            if (!row[1].empty()) {
                rows.emplace(row[0], std::string(row[1]));
                continue;
            }
            std::array<int32_t, 4> millimetres{};
            for (size_t i = 0; i < millimetres.size(); ++i) {
                const std::string_view cell = row[3 + i];
                const auto [end, error] = std::from_chars(cell.data(), cell.data() + cell.size(), millimetres[i]);
                if (error != std::errc() || end != cell.data() + cell.size()) {
                    throw std::runtime_error(std::format("remake/shop_goods.tsv: {} has no model", row[0]));
                }
            }
            const auto [bottom, top, width, reach] = millimetres;
            rows.emplace(row[0], GoodsModel{ bottom, top, width, reach });
        }
        return rows;
    }();
    const auto goods = rows.find(item);
    if (goods == rows.end()) {
        throw std::runtime_error(std::format("remake/shop_goods.tsv has no row for {}", item));
    }
    return goods->second;
}

// The tallest of the game's own goods on a shop's shelf, the potions, and the farthest any reaches from the upright
// through its origin, the bombchus, in millimetres, as their shop poses stand them.
constexpr double TallestGoods = 561;
constexpr double LongestReach = 395;
// The smallest of those goods, the heart, by the larger of its height and its width, in millimetres, and the most the
// game raises any of its goods for a shelf, the masks.
constexpr double SmallestGoods = 318;
constexpr double MostRaised = 1.4;

// Moves a placed actor up by the height, in metres, or down where the height is negative.
static void MoveUp(byml::Node& placed, double height) {
    byml::Node* translate = Find(placed, "Translate");
    if (translate == nullptr || translate->array.size() != 3) {
        throw std::runtime_error("the actor has no Translate");
    }
    byml::Node& y = translate->array[1];
    y.scalar = std::bit_cast<uint32_t>(float(FloatOf(y) + height));
}

// The game's goods rest the lowest point of their pose on their shelf's origin, so the shelf rises by what hangs below
// it. Every shelf stands upright.
static void StandOnShelf(byml::Node& shelf, const GoodsModel& model) {
    const double height = model.top - model.bottom;
    const double raised = std::max(1.0, std::min(MostRaised, SmallestGoods / std::max<double>(height, model.width)));
    const double scale = std::min({ TallestGoods / height, LongestReach / model.reach, raised });
    MoveUp(shelf, -scale * model.bottom / 1000);
    if (scale != 1) {
        const byml::Node size{ .type = byml::Type::Float, .scalar = std::bit_cast<uint32_t>(float(scale)) };
        FindOrAdd(shelf, "Scale", byml::Type::Array).array = { size, size, size };
    }
}

constexpr std::string_view QuantitySign = "Shop_Board_Bundle_";

// A row with no BundleActorNum gives one thing, as the token's and the Deku Seed's do. A rupee's row holds its worth
// there, which is no count of things on the shelf.
static uint32_t Quantity(byml::Node& pouchRow) {
    const byml::Node* bundle = Find(pouchRow, "BundleActorNum");
    const byml::Node* category = Find(pouchRow, "PouchCategory");
    return bundle == nullptr || (category != nullptr && category->string == "Rupee") ? 1 : uint32_t(bundle->scalar);
}

// Takes the actors out of the scene with every mention of them, wherever the file holds it: an entry of any list that
// is one of their hashes, or a dict that holds one directly, such as the actor or a link to it. A list emptied so goes
// from its dict, as the game writes no empty list of links.
static void Unplace(byml::Node& node, const std::vector<uint64_t>& hashes) {
    const auto isHash = [&](const byml::Node& value) {
        return value.type == byml::Type::UInt64 && std::ranges::find(hashes, value.scalar) != hashes.end();
    };
    if (node.type == byml::Type::Array) {
        std::erase_if(node.array, [&](const byml::Node& item) {
            return isHash(item) || std::ranges::any_of(item.dict, isHash, &byml::Entry::value);
        });
        for (byml::Node& item : node.array) {
            Unplace(item, hashes);
        }
    } else if (node.type == byml::Type::Dict) {
        std::vector<std::string> emptied;
        for (byml::Entry& entry : node.dict) {
            const bool held = !entry.value.array.empty();
            Unplace(entry.value, hashes);
            if (held && entry.value.array.empty()) {
                emptied.push_back(entry.key);
            }
        }
        std::erase_if(node.dict, [&](const byml::Entry& entry) {
            return std::ranges::find(emptied, entry.key) != emptied.end();
        });
    }
}

// The game's single goods have no quantity sign. Returns whether the scene changed.
static bool ShowQuantity(const GameFile& game, byml::Node& placement, uint64_t shelf, uint32_t quantity) {
    std::vector<uint64_t> signs;
    if (byml::Node* links = Find(Actor(placement, shelf), "Links")) {
        for (byml::Node& link : links->array) {
            const uint64_t linked = Find(link, "Dst")->scalar;
            if (Find(Actor(placement, linked), "Gyaml")->string.starts_with(QuantitySign)) {
                signs.push_back(linked);
            }
        }
    }
    if (signs.empty()) {
        return false;
    }
    if (quantity > 1) {
        const std::string sign = std::format("{}{:02}", QuantitySign, quantity);
        if (!game.Files().contains(std::format("Pack/Actor/{}.pack.zs", sign))) {
            throw std::runtime_error(std::format("the game has no quantity sign for {}", quantity));
        }
        bool changed = false;
        for (const uint64_t hash : signs) {
            std::string& gyaml = Find(Actor(placement, hash), "Gyaml")->string;
            changed |= gyaml != sign;
            gyaml = sign;
        }
        return changed;
    }
    Unplace(placement, signs);
    return true;
}

// A shop sells the game's goods at their base price in this table, the row's BuyingPrice, plus the slot's
// PriceOffset.
static int32_t BasePrice(byml::Node& pouchActorInfo, const std::string& goodsActor) {
    byml::Node* price = Find(Row(pouchActorInfo, goodsActor), "BuyingPrice");
    if (price == nullptr || price->type != byml::Type::Int) {
        throw std::runtime_error(std::format("{} has no BuyingPrice for {}", PouchActorInfoPath, goodsActor));
    }
    return int32_t(uint32_t(price->scalar));
}

// Each shopkeeper's actor pack holds its own copy of the goods list it sells from.
struct ShopPack {
    sarc::Files files;
    std::map<std::string, byml::Document> goodsLists;

    byml::Node& GoodsList(const std::string& goodsList) {
        const std::string name = std::format("Component/ShopParam/{}.game__component__ShopParam.bgyml", goodsList);
        auto list = goodsLists.find(name);
        if (list == goodsLists.end()) {
            const auto file = files.find(name);
            if (file == files.end()) {
                throw std::runtime_error(std::format("the shopkeeper's pack has no {}", name));
            }
            list = goodsLists.emplace(name, byml::Parse(file->second)).first;
        }
        return list->second.root;
    }
};

constexpr int32_t UnlimitedStock = -1;
// A slot sells its goods stock times a day from each shopkeeper, or as often as Link likes at UnlimitedStock. A gate
// on the slot's release and a discount were for the game's own goods there, so they take the forms of a slot with
// neither.
static void SetGoods(byml::Node& goodsList, uint32_t slot, const std::string& goodsActor, int32_t priceOffset,
                     int32_t stock) {
    byml::Node* list = Find(goodsList, "GoodsList");
    if (list == nullptr || slot >= list->array.size()) {
        throw std::runtime_error(std::format("the goods list has no slot {}", slot));
    }
    byml::Node& goods = list->array[slot];
    FindOrAdd(goods, "Actor", byml::Type::String).string = WorkPath(goodsActor);
    FindOrAdd(goods, "PriceOffset", byml::Type::Int).scalar = uint32_t(priceOffset);
    FindOrAdd(goods, "StockNum", byml::Type::Int).scalar = uint32_t(stock);
    if (byml::Node* release = Find(goods, "Release")) {
        *release = { .type = byml::Type::Dict,
                     .dict = { { .key = "IsCheckReleaseGameData", .value = { .type = byml::Type::Bool } },
                               { .key = "ReleaseGameData", .value = { .type = byml::Type::String } } } };
    }
    if (byml::Node* discounts = Find(goods, "ConditionalPriceOffsetList")) {
        discounts->array.clear();
    }
}

// A chest gives its Dynamic.Drop__DropActor. One without it gives a refill of what Link is lowest on, so the item is
// added, in key order as the game's dicts are. One with a Drop__DropActor_Substitute gives that instead to Link who
// owns its Drop__CheckActor, so the substitute takes the item too. Returns whether the actor changed.
static bool SetItem(byml::Node& chest, const std::string& item) {
    byml::Node* dynamic = Find(chest, "Dynamic");
    if (dynamic == nullptr) {
        throw std::runtime_error("the chest's actor has no Dynamic values");
    }
    const std::string dropActor = "Drop__DropActor";
    FindOrAdd(*dynamic, dropActor, byml::Type::String);
    bool changed = false;
    for (const std::string& key : { dropActor, std::string("Drop__DropActor_Substitute") }) {
        byml::Node* value = Find(*dynamic, key);
        if (value == nullptr) {
            continue;
        }
        if (value->type != byml::Type::String) {
            throw std::runtime_error(std::format("the chest's Dynamic.{} is not a string", key));
        }
        changed |= value->string != item;
        value->string = item;
    }
    return changed;
}

// A Gold Skulltula drops its Dynamic.Drop__DropActor in place of the token its drop table names.
static bool SetDrop(byml::Node& skulltula, const std::string& dropActor) {
    const byml::Node* gyaml = Find(skulltula, "Gyaml");
    if (gyaml == nullptr || gyaml->string != "Enemy_Skulltula_Gold") {
        throw std::runtime_error("the actor is not a Gold Skulltula");
    }
    byml::Node& dynamic = FindOrAdd(skulltula, "Dynamic", byml::Type::Dict);
    byml::Node& drop = FindOrAdd(dynamic, "Drop__DropActor", byml::Type::String);
    if (drop.type != byml::Type::String) {
        throw std::runtime_error("the skulltula's Dynamic.Drop__DropActor is not a string");
    }
    return std::exchange(drop.string, dropActor) != dropActor;
}

constexpr std::string_view TokenItem = "Item_Token_Gold";

// The game reads the update's size table and ignores the base game's.
constexpr std::string_view SizeTablePath = "System/Resource/ResourceSizeTable.Product.111.Nin_Ounce_NVN2.rsizetable.zs";

// "RESTBL", u32 version, u32 name length, u32 entry count, u32 name entry count, then the entries: the CRC-32 of a
// path without its ".zs", and a u32 size. Entries by name follow for paths whose CRC-32s collide, and those CRC-32s
// have no entry.
constexpr std::string_view SizeTableMagic = "RESTBL";
constexpr uint32_t SizeTableVersion = 1;
constexpr size_t SizeTableHeaderSize = 0x16;
constexpr size_t SizeTableEntrySize = 8;

// CRC-32 as zlib computes it.
static uint32_t Crc32(std::string_view data) {
    uint32_t crc = 0xFFFFFFFF;
    for (const char byte : data) {
        crc ^= uint8_t(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
        }
    }
    return ~crc;
}

static std::string ReadSizeTable(GameFile& game) {
    try {
        std::string table = Decompress(game.Read(std::string(SizeTablePath)));
        if (!table.starts_with(SizeTableMagic) || ReadAt<uint32_t>(table, 0x06) != SizeTableVersion) {
            throw std::runtime_error(std::format("not a version {} size table", SizeTableVersion));
        }
        return table;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", SizeTablePath, e.what()));
    }
}

// The game's own size table entries are a file's decompressed size rounded up to a multiple of 32, plus an overhead
// for its kind of file.
constexpr size_t SizeAlignment = 32;
constexpr size_t PlacementOverhead = 264;
constexpr size_t EventFlowOverhead = 272;
constexpr size_t TextPackOverhead = 312;
constexpr size_t ActorPackOverhead = 312;
constexpr size_t ActorTableOverhead = 264;
constexpr size_t ActorInfoOverhead = 3064;
constexpr size_t GameDataListOverhead = 264;

// Raises the size table entry of the file at this path if its decompressed size needs a larger one. Returns whether
// the entry changed. The game silently skips a file that is too large for its entry, and loads one that is smaller.
static bool RaiseSize(std::string& table, std::string_view path, size_t size, size_t overhead) {
    const uint32_t key = Crc32(path.substr(0, path.size() - std::strlen(".zs")));
    const size_t end = SizeTableHeaderSize + ReadAt<uint32_t>(table, 0x0E) * SizeTableEntrySize;
    for (size_t entry = SizeTableHeaderSize; entry < end; entry += SizeTableEntrySize) {
        if (ReadAt<uint32_t>(table, entry) == key) {
            const uint32_t value = uint32_t(AlignUp(size, SizeAlignment) + overhead);
            if (ReadAt<uint32_t>(table, entry + 4) >= value) {
                return false;
            }
            WriteAt(table, entry + 4, value);
            return true;
        }
    }
    throw std::runtime_error(std::format("{}: the size table has no entry for this file", path));
}

// Adds the entry of a file the game does not have, in the CRC-32 order the table keeps.
static void AddSize(std::string& table, std::string_view path, size_t size, size_t overhead) {
    const std::string_view name = path.substr(0, path.size() - std::strlen(".zs"));
    const uint32_t key = Crc32(name);
    const uint32_t count = ReadAt<uint32_t>(table, 0x0E);
    const size_t end = SizeTableHeaderSize + count * SizeTableEntrySize;
    const size_t nameSize = ReadAt<uint32_t>(table, 0x0A);
    for (size_t entry = end; entry < table.size(); entry += nameSize + 4) {
        const std::string_view named = std::string_view(table).substr(entry, nameSize);
        if (Crc32(named.substr(0, named.find('\0'))) == key) {
            throw std::runtime_error(std::format("{}: the CRC-32 of this file's path collides with another's", path));
        }
    }
    size_t entry = SizeTableHeaderSize;
    while (entry < end && ReadAt<uint32_t>(table, entry) < key) {
        entry += SizeTableEntrySize;
    }
    if (entry < end && ReadAt<uint32_t>(table, entry) == key) {
        throw std::runtime_error(std::format("{}: the size table already has an entry for this file", path));
    }
    std::string added(SizeTableEntrySize, '\0');
    WriteAt(added, 0, key);
    WriteAt(added, 4, uint32_t(AlignUp(size, SizeAlignment) + overhead));
    table.insert(entry, added);
    WriteAt(table, 0x0E, count + 1);
}

// Adds the row in __RowId order, the order of the game's actor tables.
static void AddRow(byml::Node& table, const byml::Node& row) {
    const auto at = std::ranges::lower_bound(table.array, RowId(row), {}, RowId);
    if (at != table.array.end() && RowId(*at) == RowId(row)) {
        throw std::runtime_error(std::format("the game already has an actor {}", RowId(row)));
    }
    table.array.insert(at, row);
}

// The Fairy Bow's and the Fairy Slingshot's rows, by the item's name and by the actor name the game counts each by,
// and the small container of each. The pouch add treats any row that lists actors to fill as a bag's: it raises the
// limit the row names and fills each listed actor to it. It asks for the list in the row of the name its caller gives
// and reads the rest from the row of the actor name. A shop asks for the container before it sells arrows or seeds.
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> WeaponContainers{ {
    { "Item_Range_Bow", "Item_Quiver_Small" },
    { "Weapon_Range_Bow", "Item_Quiver_Small" },
    { "Item_Range_SlingShot", "Item_BulletBag_Small" },
    { "Weapon_Range_SlingShot", "Item_BulletBag_Small" },
} };

// The kinds of container that come in a small, a medium and a large size. A bigger one lists the small one of its kind
// too, so it brings the small one when it comes first, which shops and the rolling Goron ask for.
constexpr std::array<std::string_view, 5> ContainerKinds{ "Quiver", "BulletBag", "BombBag", "StickBag", "NutBag" };

static void BringContainers(byml::Node& pouchActorInfo) {
    for (const std::string_view kind : ContainerKinds) {
        for (const std::string_view size : { "Medium", "Large" }) {
            const std::string bigger = std::format("Item_{}_{}", kind, size);
            byml::Node* listed = Find(Row(pouchActorInfo, bigger), "ExpansionMaxNumActor");
            if (listed == nullptr) {
                throw std::runtime_error(std::format("{}: {} has no ExpansionMaxNumActor", PouchActorInfoPath, bigger));
            }
            listed->string += "," + WorkPath(std::format("Item_{}_Small", kind));
        }
    }
    for (const auto& [weapon, container] : WeaponContainers) {
        byml::Node fills = Row(pouchActorInfo, container);
        byml::Node& row = Row(pouchActorInfo, weapon);
        for (const char* key :
             { "MaxNumGameData", "AfterExpansionMaxNum", "IsItemStockMaxIfGet", "ExpansionMaxNumActor" }) {
            const byml::Node* value = Find(fills, key);
            if (value == nullptr) {
                throw std::runtime_error(std::format("{}: {} has no {}", PouchActorInfoPath, container, key));
            }
            FindOrAdd(row, key, value->type) = *value;
        }
        FindOrAdd(row, "ExpansionMaxNumActor", byml::Type::String).string += "," + WorkPath(container);
    }
}

// A new game starts with the defaults this list gives every saved value.
constexpr std::string_view GameDataListPath = "GameData/GameDataList.Product.110.byml.zs";

// MurmurHash3 x86_32 with seed 0. The game data list keys each value by this hash of its name.
static uint32_t Murmur3(std::string_view name) {
    const auto mix = [](uint32_t k) { return std::rotl(k * 0xCC9E2D51, 15) * 0x1B873593; };
    const size_t blocks = name.size() / 4 * 4;
    uint32_t hash = 0;
    for (size_t i = 0; i < blocks; i += 4) {
        hash = std::rotl(hash ^ mix(ReadAt<uint32_t>(name, i)), 13) * 5 + 0xE6546B64;
    }
    uint32_t tail = 0;
    for (size_t i = blocks; i < name.size(); ++i) {
        tail |= uint32_t(uint8_t(name[i])) << 8 * (i - blocks);
    }
    hash ^= mix(tail) ^ uint32_t(name.size());
    hash = (hash ^ hash >> 16) * 0x85EBCA6B;
    hash = (hash ^ hash >> 13) * 0xC2B2AE35;
    return hash ^ hash >> 16;
}

// The game data list's table and node type for each kind of start value, in the order of StartValue's types.
static constexpr std::tuple<std::string_view, byml::Type, std::string_view> startValueKinds[] = {
    { "Bool", byml::Type::Bool, "a true-or-false value" },
    { "Int", byml::Type::Int, "a whole number" },
    { "Float", byml::Type::Float, "a number with a fraction" },
    { "Enum", byml::Type::UInt, "a choice of names" },
    { "String64", byml::Type::String, "a name" },
};

// A whole number may also start an unsigned value of the list, if it is not below zero.
static byml::Node& StartValueEntry(byml::Node& data, const std::string& name, const StartValue& value) {
    const auto& [kind, type, words] = startValueKinds[value.index()];
    const auto entryIn = [&](std::string_view table, byml::Type expected) -> byml::Node* {
        byml::Node* values = Find(data, table);
        byml::Node* entry = values == nullptr ? nullptr : FindHash(*values, Murmur3(name));
        const byml::Node* defaultValue = entry == nullptr ? nullptr : Find(*entry, "DefaultValue");
        return defaultValue == nullptr || defaultValue->type != expected ? nullptr : entry;
    };
    if (byml::Node* entry = entryIn(kind, type)) {
        return *entry;
    }
    if (const int32_t* whole = std::get_if<int32_t>(&value)) {
        if (byml::Node* entry = entryIn("UInt", byml::Type::UInt)) {
            if (*whole < 0) {
                throw std::runtime_error(
                    std::format("start value \"{}\" is a whole number of 0 or more in the list", name));
            }
            return *entry;
        }
    }
    throw std::runtime_error(std::format("start value \"{}\" is not {} in the list", name, words));
}

static byml::Node StartedDefault(byml::Node& entry, const std::string& name, const StartValue& value) {
    byml::Node started = *Find(entry, "DefaultValue");
    std::visit(
        [&]<typename T>(const T& v) {
            if constexpr (std::is_same_v<T, std::string>) {
                started.string = v;
            } else if constexpr (std::is_same_v<T, Choice>) {
                const byml::Node* choices = Find(entry, "Values");
                started.scalar = Murmur3(v.name);
                if (choices == nullptr ||
                    std::ranges::find(choices->array, started.scalar, &byml::Node::scalar) == choices->array.end()) {
                    throw std::runtime_error(std::format("start value \"{}\" has no choice {}", name, v.name));
                }
            } else if constexpr (std::is_same_v<T, float>) {
                started.scalar = std::bit_cast<uint32_t>(v);
            } else {
                started.scalar = uint32_t(v);
            }
        },
        value);
    return started;
}

static std::string Shown(const StartValue& value) {
    return std::visit(
        []<typename T>(const T& v) {
            if constexpr (std::is_same_v<T, Choice>) {
                return v.name;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::format("\"{}\"", v);
            } else {
                return std::format("{}", v);
            }
        },
        value);
}

static std::string Text(byml::Node& row, std::string_view key) {
    const byml::Node* value = Find(row, key);
    return value == nullptr ? "" : value->string;
}

static bool IsTrue(byml::Node& row, std::string_view key) {
    const byml::Node* value = Find(row, key);
    return value != nullptr && value->scalar != 0;
}

static std::vector<std::string> Actors(byml::Node& row, std::string_view key) {
    const std::string paths = Text(row, key);
    std::vector<std::string> actors;
    if (paths.empty()) {
        return actors;
    }
    for (const auto path : std::views::split(paths, ',')) {
        actors.push_back(WorkPathActor(std::string_view(path.begin(), path.end())));
    }
    return actors;
}

// As in the game, a container never lowers a limit.
static int32_t RaiseLimit(byml::Node& container, std::map<std::string, StartValue>& values) {
    const auto limit = values.try_emplace("PouchContentMaxNum." + Text(container, "MaxNumGameData"), 0).first;
    limit->second =
        std::max(std::get<int32_t>(limit->second), int32_t(Value(container, "AfterExpansionMaxNum").scalar));
    return std::get<int32_t>(limit->second);
}

// A container that comes with another find has no get scene of its own.
static void Bring(byml::Node& rows, const std::string& container, std::map<std::string, StartValue>& values) {
    values["IsGet." + container] = true;
    values[std::format("PouchData.{}.StockNum", container)] = 1;
    RaiseLimit(Row(rows, container), values);
}

// What a find of the actor leaves in the save, as the game's pouch add writes it from the actor's row in
// PouchActorInfo. A find also names a container in CurrentExpansionActor_<kind>. With those empty, a new game's rupee
// and bomb counters show their limits and a bomb comes out (seen in the game), so a start leaves them empty.
static void AddFind(byml::Node& rows, const std::string& actor, std::map<std::string, StartValue>& values) {
    byml::Node& row = Row(rows, actor);
    values["IsGet." + actor] = true;
    values["HavePlayedGetEvent." + actor] = true;
    values[std::format("PouchData.{}.StockNum", actor)] = 1;
    if (const std::string deal = Text(row, "PouchSpecialDeal"); deal == "Ocarina") {
        values["PlayerStatus.EquipOcarina"] = actor;
    } else if (deal == "Gauntlet") {
        values["PlayerStatus.EquipHandArmor"] = actor;
    } else if (deal != "None" && deal != "GoronGauntlet") {
        throw std::runtime_error(std::format("its PouchSpecialDeal is {}, which a start does not write", deal));
    }
    if (const std::string ability = Text(row, "ExpansionAvility"); ability == "LiftGradeUP") {
        values["PlayerStatus.LiftGrade"] = Choice{ Text(row, "LiftableGrade") };
    } else if (ability == "DiveDepthUP") {
        values["PlayerStatus.DiveDepth"] = FloatOf(Value(row, "DiveDepth"));
    } else if (!ability.empty()) {
        throw std::runtime_error(std::format("its ExpansionAvility is {}, which a start does not write", ability));
    }
    const std::string kind = Text(row, "MaxNumGameData");
    if (kind.empty()) {
        return;
    }
    if (IsTrue(row, "PouchStockable")) {
        if (IsTrue(row, "IsAbleToGetBagIfGet")) {
            Bring(rows, WorkPathActor(Text(row, "SmallBagActor")), values);
        }
        const auto limit = values.find("PouchContentMaxNum." + kind);
        if (limit == values.end()) {
            throw std::runtime_error(std::format("it goes into a {} bag, which the start does not hold", kind));
        }
        values[std::format("PouchData.{}.StockNum", actor)] = limit->second;
        return;
    }
    const int32_t limit = RaiseLimit(row, values);
    if (!IsTrue(row, "IsItemStockMaxIfGet")) {
        return;
    }
    const std::vector<std::string> onlyIfHeld = Actors(row, "ActorFullOnlyIfIsGetWhenGetBag");
    for (const std::string& filled : Actors(row, "ExpansionMaxNumActor")) {
        if (!IsTrue(Row(rows, filled), "PouchStockable")) {
            Bring(rows, filled, values);
        } else if (std::ranges::find(onlyIfHeld, filled) == onlyIfHeld.end() || values.contains("IsGet." + filled)) {
            values["IsGet." + filled] = true;
            values[std::format("PouchData.{}.StockNum", filled)] = limit;
        }
    }
}

// The kinds of value a mod adds to the game data list.
enum class Added { Key, Bool };

// What a save keeps of each value it holds.
constexpr uint64_t SavedValueSize = 8;

static uint64_t HashOf(byml::Node& value) {
    return Value(value, "Hash").scalar;
}

// Adds the value where the list keeps its kind, and grows the list's save sizes by what the save keeps of it. A
// per-actor key is no record of its save's table: it lies past the table, among the keys in hash order, so it grows the
// save's size and the size of all saves, and the table's end stays. A Bool is a record of its save's table, after the
// list's Bools, so it also moves the end of the save's table and the offset of all saves. That is the layout the game's
// own two lists keep (measured on .100 and .110).
static void Add(byml::Node& list, Added kind, byml::Node value) {
    byml::Node& meta = Value(list, "MetaData");
    const uint64_t save = Value(value, "SaveFileIndex").scalar;
    std::vector<byml::Node*> grown = { &Value(meta, "SaveDataSize").array.at(save), &Value(meta, "AllDataSaveSize") };
    switch (kind) {
    case Added::Key: {
        std::vector<byml::Node>& keys = Value(Value(list, "Data"), "Bool64bitKey").array;
        keys.insert(std::ranges::lower_bound(keys, HashOf(value), {}, HashOf), std::move(value));
        break;
    }
    case Added::Bool:
        Value(Value(list, "Data"), "Bool").array.push_back(std::move(value));
        grown.push_back(&Value(meta, "SaveDataOffsetPos").array.at(save));
        grown.push_back(&Value(meta, "AllDataSaveOffset"));
        break;
    }
    for (byml::Node* size : grown) {
        size->scalar += SavedValueSize;
    }
}

// The reset type the game gives every loose spot's key.
constexpr uint64_t LooseSpotResetType = 24;

// The first of MetaData's save files, slot_NN/progress.sav, where the game keeps every per-actor key.
constexpr uint64_t ProgressSave = 0;

// The reset type the game gives its own once-only flags, which a Bool of the mod's own takes too.
constexpr uint64_t OnceOnlyResetType = 8;

// A Bool of the mod's own, as the game keeps its own once-only flags: false in a new game, kept in the progress save.
static byml::Node ClaimBool(uint32_t hash) {
    byml::Node entry{ .type = byml::Type::Dict };
    FindOrAdd(entry, "DefaultValue", byml::Type::Bool).scalar = 0;
    FindOrAdd(entry, "Hash", byml::Type::UInt).scalar = hash;
    FindOrAdd(entry, "ResetTypeValue", byml::Type::Int).scalar = OnceOnlyResetType;
    FindOrAdd(entry, "SaveFileIndex", byml::Type::Int).scalar = ProgressSave;
    return entry;
}

// The operations of a saved expression that a plan's expression uses. The game lists them in postfix order.
constexpr uint64_t ReadBool = 0;
constexpr uint64_t And = 3;
constexpr uint64_t ReadExpression = 10;

// An expression of the mod's own, unsaved and in the shape of the game's own that join Bools and expressions.
// SageOfShadow_Kakariko_Burning joins three expressions so, and HappyMaskShop_AppearMask_Keaton reads one Bool.
static byml::Node AndExpression(uint32_t hash, const std::vector<std::pair<uint64_t, uint32_t>>& reads) {
    byml::Node entry{ .type = byml::Type::Dict };
    FindOrAdd(entry, "Hash", byml::Type::UInt).scalar = hash;
    FindOrAdd(entry, "ResetTypeValue", byml::Type::Int).scalar = 0;
    FindOrAdd(entry, "SaveFileIndex", byml::Type::Int).scalar = uint32_t(-1);
    std::vector<byml::Node>& operations = FindOrAdd(entry, "Values", byml::Type::Array).array;
    for (const auto& [operation, named] : reads) {
        operations.push_back({ .type = byml::Type::Array,
                               .array = { { .type = byml::Type::UInt64, .scalar = operation },
                                          { .type = byml::Type::UInt64, .scalar = named } } });
    }
    const byml::Node join{ .type = byml::Type::Array, .array = { { .type = byml::Type::UInt64, .scalar = And } } };
    operations.insert(operations.end(), reads.size() - 1, join);
    return entry;
}

// A loose spot's key, as the game gives every loose spot's.
static byml::Node LooseSpotKey(uint64_t hash) {
    byml::Node key{ .type = byml::Type::Dict };
    FindOrAdd(key, "Hash", byml::Type::UInt64).scalar = hash;
    FindOrAdd(key, "ResetTypeValue", byml::Type::Int).scalar = LooseSpotResetType;
    FindOrAdd(key, "SaveFileIndex", byml::Type::Int).scalar = ProgressSave;
    return key;
}

// A new game starts with each start value as its default. Each count of a shop slot sold takes reset type 0 in place
// of the game's 2, the type that the game's data has a day change reset. An entry's reset type and every default but a
// String64's sit inside the entry. A String64's default is a name in the list's string table, which holds none of the
// item names a start writes, so each such name grows the list. A taken pickup stays taken by the key its spot has in
// the list, so each loose spot the list has no key for gets one. Each claim is a Bool of the mod's own, under a name no
// value of the list has. Gives nothing when the list stays the game's.
static std::optional<std::string> GameDataList(GameFile& game, const std::string& dictionary,
                                               const std::map<std::string, StartValue>& values,
                                               const std::set<std::string>& soldOnce,
                                               const std::vector<PlannedActorItem>& looseItems,
                                               const std::vector<std::string>& claims,
                                               const std::vector<PlannedExpression>& expressions) {
    try {
        byml::Document list = byml::Parse(Decompress(game.Read(std::string(GameDataListPath)), dictionary));
        byml::Node* data = Find(list.root, "Data");
        if (data == nullptr) {
            throw std::runtime_error("the list has no Data");
        }
        for (const auto& [name, value] : values) {
            byml::Node& entry = StartValueEntry(*data, name, value);
            byml::Node& defaultValue = *Find(entry, "DefaultValue");
            const byml::Node started = StartedDefault(entry, name, value);
            if (started == defaultValue) {
                throw std::runtime_error(
                    std::format("start value \"{}\" is already {} by default", name, Shown(value)));
            }
            defaultValue = started;
        }
        for (const std::string& name : soldOnce) {
            byml::Node* ints = Find(*data, "Int");
            byml::Node* entry = ints == nullptr ? nullptr : FindHash(*ints, Murmur3(name));
            byml::Node* reset = entry == nullptr ? nullptr : Find(*entry, "ResetTypeValue");
            if (reset == nullptr || reset->type != byml::Type::Int) {
                throw std::runtime_error(std::format("\"{}\" is not a count with a reset type in the list", name));
            }
            reset->scalar = 0;
        }
        bool keyed = false;
        std::vector<byml::Node>& keys = Value(*data, "Bool64bitKey").array;
        for (const PlannedActorItem& loose : looseItems) {
            if (!std::ranges::binary_search(keys, loose.hash, {}, HashOf)) {
                Add(list.root, Added::Key, LooseSpotKey(loose.hash));
                keyed = true;
            }
        }
        for (const std::string& claim : claims) {
            const uint32_t hash = Murmur3(claim);
            for (byml::Entry& table : data->dict) {
                if (FindHash(table.value, hash) != nullptr) {
                    throw std::runtime_error(
                        std::format("claim \"{}\" has the hash of a value in {}", claim, table.key));
                }
            }
            Add(list.root, Added::Bool, ClaimBool(hash));
        }
        // An expression is no value of a save, so the saves keep their layout.
        for (const PlannedExpression& expression : expressions) {
            const uint32_t hash = Murmur3(expression.name);
            for (byml::Entry& table : data->dict) {
                if (FindHash(table.value, hash) != nullptr) {
                    throw std::runtime_error(
                        std::format("expression \"{}\" has the hash of a value in {}", expression.name, table.key));
                }
            }
            std::vector<std::pair<uint64_t, uint32_t>> reads;
            for (const std::string& name : expression.allOf) {
                const uint32_t named = Murmur3(name);
                if (FindHash(Value(*data, "Bool"), named) != nullptr) {
                    reads.emplace_back(ReadBool, named);
                } else if (FindHash(Value(*data, "BoolExp"), named) != nullptr) {
                    reads.emplace_back(ReadExpression, named);
                } else {
                    throw std::runtime_error(
                        std::format("expression \"{}\" names \"{}\", which is no true-or-false value or expression",
                                    expression.name, name));
                }
            }
            Value(*data, "BoolExp").array.push_back(AndExpression(hash, reads));
        }
        if (values.empty() && soldOnce.empty() && !keyed && claims.empty() && expressions.empty()) {
            return std::nullopt;
        }
        return byml::Write(list);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", GameDataListPath, e.what()));
    }
}

// The game data list's table, Bool or BoolExp, of each flag a presence or a query can name, by the hash of its name.
static std::map<uint32_t, std::string_view> Flags(GameFile& game, const std::string& dictionary,
                                                  const std::vector<std::string>& claims,
                                                  const std::vector<PlannedExpression>& expressions) {
    byml::Document list = byml::Parse(Decompress(game.Read(std::string(GameDataListPath)), dictionary));
    byml::Node* data = Find(list.root, "Data");
    std::map<uint32_t, std::string_view> flags;
    for (const std::string_view table : { "Bool", "BoolExp" }) {
        byml::Node* values = data == nullptr ? nullptr : Find(*data, table);
        if (values == nullptr) {
            throw std::runtime_error(std::format("{} has no {} values", GameDataListPath, table));
        }
        for (byml::Node& value : values->array) {
            if (const byml::Node* hash = Find(value, "Hash"); hash != nullptr) {
                flags.emplace(uint32_t(hash->scalar), table);
            }
        }
    }
    for (const std::string& claim : claims) {
        flags.emplace(Murmur3(claim), "Bool");
    }
    for (const PlannedExpression& expression : expressions) {
        flags.emplace(Murmur3(expression.name), "BoolExp");
    }
    return flags;
}

static void CheckFlag(const std::map<uint32_t, std::string_view>& flags, std::string_view flag) {
    if (!flags.contains(Murmur3(flag))) {
        throw std::runtime_error(
            std::format("\"{}\" is not a true-or-false value or expression in the game data list", flag));
    }
}

// The game data actor's queries whose one string parameter names a flag, each with the table it looks the name up in.
// Given a name the table lacks, the expression query gives no answer and its switch repeats the last one (read in the
// program).
static constexpr std::tuple<std::string_view, std::string_view, std::string_view> flagQueries[] = {
    { "EventQueryIsGameDataBool", "Bool", "a true-or-false value" },
    { "EventQueryIsGameDataBoolExp", "BoolExp", "a true-or-false expression" },
};

static void CheckFlagName(const bfevfl::Flow& flow, const PlannedEventText& text,
                          const std::map<uint32_t, std::string_view>& flags) {
    const std::optional<bfevfl::Query> query = bfevfl::QueryOf(flow, text.event);
    if (!query || query->actor != "GameDataActor") {
        return;
    }
    const auto flag = flags.find(Murmur3(text.text));
    for (const auto& [name, table, words] : flagQueries) {
        if (query->name == name && (flag == flags.end() || flag->second != table)) {
            throw std::runtime_error(
                std::format("{} asks for {}, and the game data list has none named \"{}\"", name, words, text.text));
        }
    }
}

constexpr std::string_view EventSettingPath = "RSDB/EventSetting.Product.111.rstbl.byml.zs";

static byml::Node& ScheduleEntry(byml::Node& schedule, std::string_view flag) {
    if (byml::Node* entries = Find(schedule, "ConditionalSchedule"); entries != nullptr) {
        const auto entry =
            std::ranges::find(entries->array, flag, [](byml::Node& e) { return Text(e, "ConditionFlag"); });
        if (entry != entries->array.end()) {
            return *entry;
        }
    }
    throw std::runtime_error("the schedule has no such entry");
}

static byml::Node& TalkEvent(byml::Node& entry) {
    if (byml::Node* events = Find(entry, "EventEntry"); events != nullptr) {
        const auto talk = std::ranges::find(events->array, "game__aischedule__OnPlayerTalk",
                                            [](byml::Node& e) { return Text(e, "$type"); });
        if (talk != events->array.end()) {
            return *talk;
        }
    }
    throw std::runtime_error("the entry has no talk");
}

// The game's size table entry for a schedule is several times the schedule's size, so an edit of a few strings needs
// no larger entry.
static std::map<std::string, std::string> EditedSchedules(GameFile& game,
                                                          const std::vector<PlannedScheduleEntry>& edits,
                                                          const std::map<uint32_t, std::string_view>& flags) {
    byml::Document talks = byml::Parse(Decompress(game.Read(std::string(EventSettingPath))));
    std::map<std::string, byml::Document> schedules;
    for (const PlannedScheduleEntry& planned : edits) {
        try {
            auto schedule = schedules.find(planned.file);
            if (schedule == schedules.end()) {
                schedule = schedules.emplace(planned.file, byml::Parse(game.Read(planned.file))).first;
            }
            byml::Node& entry = ScheduleEntry(schedule->second.root, planned.replaces);
            CheckFlag(flags, planned.flag);
            if (std::ranges::find(talks.root.array, planned.talk,
                                  [](byml::Node& row) { return Text(row, "__RowId"); }) == talks.root.array.end()) {
                throw std::runtime_error(std::format("\"{}\" is not a row of {}", planned.talk, EventSettingPath));
            }
            Find(entry, "ConditionFlag")->string = planned.flag;
            Find(TalkEvent(entry), "EventName")->string =
                std::format("Work/Event/EventSetting/{}.engine__event__EventSettingParam.gyml", planned.talk);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::format("schedule entry on \"{}\" in {}: {}", planned.replaces, planned.file, e.what()));
        }
    }
    std::map<std::string, std::string> written;
    for (const auto& [path, schedule] : schedules) {
        written.emplace(path, byml::Write(schedule));
    }
    return written;
}

// Link takes a bomb out of the bag only when the bomb's lift grade is at most his, as he lifts a rock. A bomb's grade
// is Low, which a child reaches only with Goron's Bracelet. At None, the bombchu's grade, a child with no strength
// takes a bomb out and breaks a wall with it. Each of these packs holds a copy of the bomb's lift file, and which copy
// the game loads is not known. In the three bomb flower packs the fruit's own lift file builds on this one and states
// Low itself, so a bomb flower still takes the bracelet's strength.
constexpr std::string_view BombLiftFile = "Component/LiftableParam/Item_Bomb.game__component__LiftableParam.bgyml";
constexpr std::array<std::string_view, 5> BombLiftPacks{
    "Pack/Actor/Item_Bomb.pack.zs",
    "Pack/Actor/Npc_Hylia_ImperialSoldier_Bomb.pack.zs",
    "Pack/Actor/Obj_BombFlower_A_01.pack.zs",
    "Pack/Actor/Obj_BombFlower_A_04.pack.zs",
    "Pack/Actor/Obj_BombFlower_Fruit_For_Break_Obj_DeathMtTrail_BrokenRock_A_01.pack.zs",
};

// The grade changes in place, so the pack keeps its size and still fits its size table entry.
static std::string BombLiftPack(GameFile& game, const std::string& dictionary, std::string_view path) {
    try {
        sarc::Files pack = sarc::Parse(Decompress(game.Read(std::string(path)), dictionary));
        const auto file = pack.find(std::string(BombLiftFile));
        if (file == pack.end()) {
            throw std::runtime_error(std::format("the pack has no {}", BombLiftFile));
        }
        byml::SetRootUInt(file->second, "LiftGrade", Murmur3("Low"), Murmur3("None"));
        return Compress(sarc::Write(pack, sarc::ActorPackAlignment), dictionary);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", path, e.what()));
    }
}

// Once the castle quest is complete, the last entry of Mido's schedule sends him away from his post with a talk that
// never lets Link by, while the wall at the post stands as long as his blocker value holds. Without that entry, a Link
// who meets Zelda before Mido lets him by finds Mido at his post with his blocking talk, as before Zelda. Once Mido has
// let him by, the other entries hold as before, and Mido gives his lines for the forest's story instead of those for
// after the castle.
constexpr std::string_view MidoSchedule =
    "Component/AIScheduleParam/Npc_Kokiri_Mido_Child.game__component__AIScheduleParam.bgyml";
constexpr std::string_view MidoCastleCondition = "Goto_HyruleCastle_IsCompleted_Exp";

// The file shrinks, so it still fits its size table entry.
static std::string MidoScheduleWithoutCastleEntry(GameFile& game) {
    try {
        byml::Document schedule = byml::Parse(game.Read(std::string(MidoSchedule)));
        byml::Node* entries = Find(schedule.root, "ConditionalSchedule");
        byml::Node* condition =
            entries == nullptr || entries->array.empty() ? nullptr : Find(entries->array.back(), "ConditionFlag");
        if (condition == nullptr || condition->string != MidoCastleCondition) {
            throw std::runtime_error(std::format("the last entry is not the one for {}", MidoCastleCondition));
        }
        entries->array.pop_back();
        return byml::Write(schedule);
    } catch (const std::exception& e) { throw std::runtime_error(std::format("{}: {}", MidoSchedule, e.what())); }
}

// Adult Link becomes a child by putting the Master Sword back. This logic module takes the sword's interaction away
// while the expression it names is false, and the game names the Forest Temple's quest complete. Every adult has the
// Door of Time's quest complete, since the first pull of the sword completes it. The module names its expression once,
// in its string pool, and the file is stored uncompressed. The shorter name ends in NUL bytes where the longer one
// stood, so the module keeps its size and its size table entry.
constexpr std::string_view PedestalModule = "Logic/Market_Adult_Prefab_89da.logic.module.ainb";
constexpr std::string_view ForestTempleDone = "SageOfForest_IsCompleted_Exp";
constexpr std::string_view DoorOfTimeOpened = "DoorOfTime_IsCompleted_Exp";

static std::string OpenPedestal(GameFile& game) {
    std::string module = game.Read(std::string(PedestalModule));
    const std::string gameName = std::string(ForestTempleDone) + '\0';
    const size_t at = module.find(gameName);
    if (at == std::string::npos || module.find(gameName, at + 1) != std::string::npos) {
        throw std::runtime_error(std::format("{}: the module does not name {} once", PedestalModule, ForestTempleDone));
    }
    std::string name(DoorOfTimeOpened);
    name.resize(gameName.size(), '\0');
    module.replace(at, name.size(), name);
    return module;
}

// Before the Forest Temple, Sheik's schedule sends him to his AnchorAction1, 0.77 units in front of the Master Sword on
// its axis. The sword's "Return" prompt needs Link within 1.5 units in front of it, and after a pull adult Link appears
// 0.34 units from that spot. Sheik's own placement stands 1.39 units behind the sword. The anchor takes that x and
// keeps its height and z, so its position stays a container of its own and the file keeps its size. His lesson after
// the Forest Temple warps him to a spot of its own.
constexpr std::string_view TempleStatic = "Banc/Market/Market_Adult_Static.bcett.byml.zs";
constexpr uint64_t SheikHash = 2669233513522124871;
constexpr uint64_t SheikWaitHash = 3082534610608980032;
// The anchor's position as the game ships it, (136.871, 106.3536, -34.0), as the bits of its floats.
constexpr std::array<uint32_t, 3> SheikWaitShipped = { 0x4308DEFA, 0x42D4B50B, 0xC2080000 };

static void MoveSheikWaitBehindSword(byml::Node& placement) {
    byml::Node* wait = Find(Actor(placement, SheikWaitHash), "Translate");
    const bool shipped = wait != nullptr && wait->array.size() == SheikWaitShipped.size() &&
                         std::ranges::equal(wait->array, SheikWaitShipped, [](const byml::Node& value, uint32_t bits) {
                             return value.type == byml::Type::Float && value.scalar == bits;
                         });
    if (!shipped) {
        throw std::runtime_error("Sheik's AnchorAction1 is not where the game ships it");
    }
    const byml::Node* sheik = Find(Actor(placement, SheikHash), "Translate");
    if (sheik == nullptr || sheik->array.empty() || sheik->array[0].type != byml::Type::Float) {
        throw std::runtime_error("Sheik has no position");
    }
    wait->array[0] = sheik->array[0];
}

// The game reads the update's text pack of the console's language. A planned text is in English, and on a console in
// another language the game's own text stays. A copied message goes into every language's pack, as the flow that shows
// it is one file for every language.
constexpr std::string_view EnglishTextPacks[] = { "Mals/USen.Product.110.sarc.zs", "Mals/EUen.Product.110.sarc.zs" };
constexpr std::string_view TextPackFolder = "Mals/";
constexpr std::string_view TextPackSuffix = ".Product.110.sarc.zs";
constexpr std::string_view StoneMessages = "EventFlowMsg/GossipStone.msbt";

// Every stone's talk runs this flow. Without the Mask of Truth held and worn, Navi talks in the stone's place. With it,
// the stone's talk opens, the stone says its lines about the mask, and then its message.
constexpr std::string_view StoneFlow = "Event/EventFlow/GossipStone.bfevfl.zs";
constexpr std::string_view MaskHeldQuery = "Event112";
constexpr std::string_view StoneTalkOpening = "Event6";
constexpr std::string_view StoneMessage = "Event18";
// The mask question goes on to the talk's opening on both answers, and the opening straight on to the message.
constexpr std::pair<std::string_view, std::string_view> StoneFlowNext[] = { { MaskHeldQuery, StoneTalkOpening },
                                                                            { StoneTalkOpening, StoneMessage } };

// Zora's River's waterfall entrance runs this flow's Goto_ZoraDomain while the domain's intro has not played. After the
// intro, Event48, the entry sets the Water quest to Step03 or Step03_Impa with no check of the step. Lake Hylia's
// tunnel leads into the domain without the intro, so by the first walk in from the river the quest can be past Step03.
// Event551 follows both writes. King Zora's first talk takes the quest on from Ready, Step01 or Step02.
constexpr std::string_view ArrivalFlow = "Event/EventFlow/Dm_SY_BancChangeArrive.bfevfl.zs";
constexpr std::string_view ZoraDomainIntro = "Event48";
constexpr std::string_view AfterWaterStep = "Event551";

// A new game runs this flow. The loading entry point plays the narrated opening, then asks whether Kokiri Forest has
// loaded. The introduction waits, plays Navi waking Link, and fades out before it shows him in his house. The Deku
// Tree's quest and the forest's first tutorial wait for Navi's scene to count as played, which a scene left out never
// does.
constexpr std::string_view OpeningFlow = "Event/EventFlow/OpeningEvent.bfevfl.zs";
constexpr std::string_view LoadingEntry = "Opening_LoadingKokiriForest";
constexpr std::string_view ForestLoadedQuery = "Event18";
constexpr std::string_view WaitBeforeNavi = "Event27";
constexpr std::string_view FadeOutAfterNavi = "Event25";
constexpr std::string_view NaviWokeLink = "HavePlayedEvent.Dm_Kokiri_0030";

// The Kokiri Sword chest's event, entry point GetSword of this flow, gives what the chest holds in Event16, plays the
// scene of Link raising the sword, and from Event152 picks Navi's line. The scene puts a Kokiri Sword in his weapon
// slot that is not in his pouch, and Mido lets him by with it.
constexpr uint64_t KokiriSwordChest = 16757992849254467386u;
constexpr std::string_view KokiriSword = "Item_Sword_Kokiri";
constexpr std::string_view SwordFlow = "Event/EventFlow/Get_StoneOfForest.bfevfl.zs";
constexpr std::string_view SwordChestGive = "Event16";
constexpr std::string_view NaviLineAfterSword = "Event152";

// The Door of Time's quest takes the stones' steps only from DoorOfTime_CheckStoneNum, which runs as each stone's
// dungeon ends. Until Zelda is met the quest refuses every step, so a child who finishes both stone dungeons first
// never brings the quest to Step03, which Zelda's escape past the moat waits for. Impa's send-off, DoorOfTime_SendOff,
// writes Step02 and ends in Event62. Going on from there to the check's first event, Event38, it gives the quest the
// stones done by then. With both, the check writes Step03 in Event37 and then raises the drawbridge to the Market in
// Event77, a saved flag that no quest state refuses and only Zelda's escape lowers. Going from Event37 straight on to
// the stone count in Event4, the check leaves the bridge down for a child who has not met her yet.
constexpr std::string_view DoorOfTimeFlow = "Event/EventFlow/DoorOfTime.bfevfl.zs";
constexpr std::string_view SendOffEnd = "Event62";
constexpr std::string_view StoneCheck = "Event38";
constexpr std::string_view BothStonesStep = "Event37";
constexpr std::string_view StoneCount = "Event4";
constexpr std::pair<std::string_view, std::string_view> DoorOfTimeNext[] = { { SendOffEnd, StoneCheck },
                                                                             { BothStonesStep, StoneCount } };

// The text pack with each planned stone saying its text, and each planned message its text after the codes the game's
// message opens with. Another language's pack keeps the game's words, and a copy there says its copied message whole.
static std::string TextPack(GameFile& game, const std::string& path, const std::string& dictionary, const Plan& plan,
                            bool english) {
    try {
        sarc::Files pack = sarc::Parse(Decompress(game.Read(path), dictionary));
        std::map<std::string, std::vector<msbt::Message>> files;
        const auto message = [&](const std::string& file, const std::string& label) -> msbt::Message& {
            auto messages = files.find(file);
            if (messages == files.end()) {
                const auto data = pack.find(file);
                if (data == pack.end()) {
                    throw std::runtime_error(std::format("the pack has no {}", file));
                }
                messages = files.emplace(file, msbt::Parse(data->second)).first;
            }
            const auto found = std::ranges::find(messages->second, label, &msbt::Message::label);
            if (found == messages->second.end()) {
                throw std::runtime_error(std::format("{} has no message with the label {}", file, label));
            }
            return *found;
        };
        if (english) {
            for (const PlannedStoneText& stone : plan.stoneTexts) {
                message(std::string(StoneMessages), stone.label).text = stone.text;
            }
        }
        for (const PlannedMessageText& planned : plan.messageTexts) {
            if (!planned.copyOf) {
                if (english) {
                    msbt::Message& text = message(planned.file, planned.label);
                    text.text = std::string(msbt::Opening(text.text)) + planned.text;
                }
                continue;
            }
            const std::string& source = message(planned.file, *planned.copyOf).text;
            const std::string copied = english ? std::string(msbt::Opening(source)) + planned.text : source;
            std::vector<msbt::Message>& messages = files.at(planned.file);
            if (std::ranges::find(messages, planned.label, &msbt::Message::label) != messages.end()) {
                throw std::runtime_error(
                    std::format("{} already has a message with the label {}", planned.file, planned.label));
            }
            messages.push_back({ .label = planned.label, .text = copied });
        }
        for (const auto& [file, messages] : files) {
            pack[file] = msbt::Write(messages);
        }
        return sarc::Write(pack, sarc::TextPackAlignment);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", path, e.what()));
    }
}

std::map<std::string, std::string> ModFiles(const std::filesystem::path& nup, const Plan& plan) {
    GameFile game(nup);
    // With a program of another build the game would run without the program patch.
    if (game.ProgramBuildId() != PatchedBuildId) {
        throw std::runtime_error(
            "This game file is another version of the game. Kaepora works only with version 2.0.0.");
    }
    const std::string dictionary = Dictionary(game, "bcett.byml.zsdic");

    std::map<std::string, byml::Document> placements;
    const auto placement = [&](const std::string& path) -> byml::Node& {
        auto found = placements.find(path);
        if (found == placements.end()) {
            found = placements.emplace(path, byml::Parse(Decompress(game.Read(path), dictionary))).first;
        }
        return found->second.root;
    };
    std::set<std::string> changed;
    for (const PlannedChest& chest : plan.chests) {
        for (const std::string& path : chest.files) {
            try {
                if (SetItem(Actor(placement(path), chest.hash), chest.dropActor)) {
                    changed.insert(path);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("chest {} in {}: {}", chest.hash, path, e.what()));
            }
        }
    }

    try {
        MoveSheikWaitBehindSword(placement(std::string(TempleStatic)));
        changed.insert(std::string(TempleStatic));
    } catch (const std::exception& e) { throw std::runtime_error(std::format("{}: {}", TempleStatic, e.what())); }

    const std::string packDictionary = Dictionary(game, "pack.zsdic");
    std::map<std::string, ShopPack> packs;
    std::set<std::string> soldOnce;
    ActorTables tables;
    for (const std::string_view path : ActorTablePaths) {
        tables.emplace(path, byml::Parse(Decompress(game.Read(std::string(path)))));
    }
    BringContainers(tables.find(PouchActorInfoPath)->second.root);
    std::map<std::string, NewActor> goodsActors;
    std::map<std::string, NewActor> tokenCopies;
    std::map<std::string, NewActor> pickupCopies;
    for (const PlannedShopSlot& slot : plan.shops) {
        try {
            byml::Node& pouch = tables.find(PouchActorInfoPath)->second.root;
            std::string goodsActor;
            int32_t priceOffset = 0;
            int32_t stock = UnlimitedStock;
            const GoodsModel* model = nullptr;
            uint32_t quantity = 0;
            if (const ItemSale* sale = std::get_if<ItemSale>(&slot.sale)) {
                const ShopGoods& goods = Goods(sale->item);
                model = std::get_if<GoodsModel>(&goods);
                priceOffset = sale->price;
                stock = 1;
                quantity = Quantity(Row(pouch, sale->item));
                if (model == nullptr) {
                    goodsActor = std::get<std::string>(goods);
                    priceOffset -= BasePrice(pouch, goodsActor);
                } else {
                    auto actor = goodsActors.find(sale->item);
                    if (actor == goodsActors.end()) {
                        actor =
                            goodsActors.emplace(sale->item, GoodsActor(game, packDictionary, tables, sale->item)).first;
                    }
                    goodsActor = actor->second.name;
                }
            } else {
                goodsActor = std::get<GoodsSale>(slot.sale).goods;
                quantity = Quantity(Row(pouch, goodsActor));
            }
            for (const Shopkeeper& keeper : slot.shopkeepers) {
                const std::string path = std::format("Pack/Actor/{}.pack.zs", keeper.actor);
                auto pack = packs.find(path);
                if (pack == packs.end()) {
                    pack = packs.emplace(path, ShopPack{ sarc::Parse(Decompress(game.Read(path), packDictionary)) }).first;
                }
                SetGoods(pack->second.GoodsList(keeper.goodsList), slot.slot, goodsActor, priceOffset, stock);
                if (stock != UnlimitedStock) {
                    soldOnce.insert(std::format("Shop_SoldNum.{}.{}", keeper.actor, slot.slot));
                }
            }
            for (const PlacedActor& shelf : slot.shelves) {
                try {
                    byml::Node& shelfActor = Actor(placement(shelf.file), shelf.hash);
                    if (SetGyaml(shelfActor, goodsActor)) {
                        changed.insert(shelf.file);
                    }
                    if (model != nullptr) {
                        StandOnShelf(shelfActor, *model);
                        changed.insert(shelf.file);
                    }
                    if (ShowQuantity(game, placement(shelf.file), shelf.hash, quantity)) {
                        changed.insert(shelf.file);
                    }
                } catch (const std::exception& e) {
                    throw std::runtime_error(std::format("shelf {} in {}: {}", shelf.hash, shelf.file, e.what()));
                }
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::format("shop slot {} of {}: {}", slot.slot, slot.shopkeepers.front().actor, e.what()));
        }
    }

    for (const PlannedActorItem& skulltula : plan.skulltulas) {
        if (skulltula.item == TokenItem) {
            continue;
        }
        auto copy = tokenCopies.find(skulltula.item);
        if (copy == tokenCopies.end()) {
            try {
                copy = tokenCopies.emplace(skulltula.item, TokenCopy(game, packDictionary, tables, skulltula.item))
                           .first;
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("skulltula {}: {}", skulltula.hash, e.what()));
            }
        }
        for (const std::string& path : skulltula.files) {
            try {
                if (SetDrop(Actor(placement(path), skulltula.hash), copy->second.name)) {
                    changed.insert(path);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("skulltula {} in {}: {}", skulltula.hash, path, e.what()));
            }
        }
    }
    for (const PlannedActorItem& loose : plan.looseItems) {
        std::string pickup = loose.item;
        // A copy's body sinks by its model's depth under its origin, so the spot rises by as much: the body starts
        // where the template's would, and the model's lowest point stands at the spot.
        float depth = 0;
        if (std::ranges::find(LoosePickups, loose.item) == LoosePickups.end()) {
            auto copy = pickupCopies.find(loose.item);
            if (copy == pickupCopies.end()) {
                try {
                    copy = pickupCopies.emplace(loose.item, PickupCopy(game, packDictionary, tables, loose.item)).first;
                } catch (const std::exception& e) {
                    throw std::runtime_error(std::format("loose item {}: {}", loose.hash, e.what()));
                }
            }
            pickup = copy->second.name;
            depth = DepthUnderOrigin(copy->second.rows.find(ActorInfoPath)->second);
        }
        for (const std::string& path : loose.files) {
            try {
                byml::Node& spot = Actor(placement(path), loose.hash);
                if (SetGyaml(spot, pickup)) {
                    MoveUp(spot, depth);
                    changed.insert(path);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("loose item {} in {}: {}", loose.hash, path, e.what()));
            }
        }
    }

    const std::string zsDictionary = Dictionary(game, "zs.zsdic");
    const std::map<uint32_t, std::string_view> flags = Flags(game, zsDictionary, plan.claims, plan.expressions);
    for (const PlannedPresence& presence : plan.presences) {
        try {
            CheckFlag(flags, presence.flag);
            byml::Node& actor = Actor(placement(presence.actor.file), presence.actor.hash);
            byml::Node* own = Find(actor, "Presence");
            if (presence.replaces) {
                byml::Node* flag = own == nullptr ? nullptr : Find(*own, "FlagName");
                if (flag == nullptr || flag->string != *presence.replaces) {
                    throw std::runtime_error(std::format("the actor has no presence on \"{}\"", *presence.replaces));
                }
                flag->string = presence.flag;
            } else if (own != nullptr) {
                throw std::runtime_error("the actor already has a presence");
            } else {
                // The game's presences that wait for a quest step take this shape.
                FindOrAdd(actor, "Presence", byml::Type::Dict).dict = {
                    { .key = "FlagName", .value = { .type = byml::Type::String, .string = presence.flag } },
                    { .key = "IsCheckLazy", .value = { .type = byml::Type::Bool } },
                    { .key = "IsNegation", .value = { .type = byml::Type::Bool } },
                };
            }
            changed.insert(presence.actor.file);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::format("presence {} in {}: {}", presence.actor.hash, presence.actor.file, e.what()));
        }
    }
    std::map<std::string, bfevfl::Flow> flows;
    std::set<std::string> changedFlows;
    const auto flowAt = [&](const std::string& path) -> bfevfl::Flow& {
        auto flow = flows.find(path);
        if (flow == flows.end()) {
            flow = flows.emplace(path, bfevfl::Parse(Decompress(game.Read(path), zsDictionary))).first;
        }
        return flow->second;
    };
    for (const PlannedEventText& text : plan.eventTexts) {
        try {
            if (bfevfl::SetString(flowAt(text.flow), text.event, text.param, text.text)) {
                changedFlows.insert(text.flow);
            }
            CheckFlagName(flowAt(text.flow), text, flags);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::format("event text {}: {}", Name(text.flow, text.event, text.param), e.what()));
        }
    }
    for (const PlannedEventNumber& number : plan.eventNumbers) {
        try {
            if (bfevfl::SetInt(flowAt(number.flow), number.event, number.param, number.value)) {
                changedFlows.insert(number.flow);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::format("event number {}: {}", Name(number.flow, number.event, number.param), e.what()));
        }
    }
    for (const PlannedEventNext& next : plan.eventNexts) {
        try {
            if (bfevfl::SetNext(flowAt(next.flow), next.event, next.next)) {
                changedFlows.insert(next.flow);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("event next {} in {}: {}", next.event, next.flow, e.what()));
        }
    }
    const std::string arrival(ArrivalFlow);
    if (bfevfl::SetNext(flowAt(arrival), ZoraDomainIntro, AfterWaterStep)) {
        changedFlows.insert(arrival);
    }
    if (!plan.stoneTexts.empty()) {
        const std::string path(StoneFlow);
        for (const auto& [event, next] : StoneFlowNext) {
            if (bfevfl::SetNext(flowAt(path), event, next)) {
                changedFlows.insert(path);
            }
        }
    }
    if (plan.openingSkipped) {
        const std::string path(OpeningFlow);
        if (bfevfl::SetEntry(flowAt(path), LoadingEntry, ForestLoadedQuery)) {
            changedFlows.insert(path);
        }
        if (bfevfl::SetNext(flowAt(path), WaitBeforeNavi, FadeOutAfterNavi)) {
            changedFlows.insert(path);
        }
    }
    if (std::ranges::any_of(plan.chests, [](const PlannedChest& chest) {
            return chest.hash == KokiriSwordChest && chest.dropActor != KokiriSword;
        })) {
        const std::string path(SwordFlow);
        if (bfevfl::SetNext(flowAt(path), SwordChestGive, NaviLineAfterSword)) {
            changedFlows.insert(path);
        }
    }
    const std::string doorOfTime(DoorOfTimeFlow);
    for (const auto& [event, next] : DoorOfTimeNext) {
        if (bfevfl::SetNext(flowAt(doorOfTime), event, next)) {
            changedFlows.insert(doorOfTime);
        }
    }

    std::map<std::string, std::string> mod{ { std::format("exefs/{}.ips", PatchedBuildId),
                                              ProgramPatch(plan.cutscenesSkipped) } };
    const auto addToRomfs = [&](std::string_view path, std::string data) {
        mod.emplace(std::format("romfs/{}", path), std::move(data));
    };
    for (const std::string_view path : BombLiftPacks) {
        addToRomfs(path, BombLiftPack(game, packDictionary, path));
    }
    if (plan.midoCastleEntryRemoved) {
        addToRomfs(MidoSchedule, MidoScheduleWithoutCastleEntry(game));
    }
    for (auto& [path, data] : EditedSchedules(game, plan.scheduleEntries, flags)) {
        addToRomfs(path, std::move(data));
    }
    addToRomfs(PedestalModule, OpenPedestal(game));
    std::string table = ReadSizeTable(game);
    bool tableChanged = false;
    for (const std::string& path : changed) {
        const std::string data = byml::Write(placements.at(path));
        tableChanged |= RaiseSize(table, path, data.size(), PlacementOverhead);
        addToRomfs(path, Compress(data, dictionary));
    }
    for (const std::string& path : changedFlows) {
        const std::string data = bfevfl::Write(flows.at(path));
        tableChanged |= RaiseSize(table, path, data.size(), EventFlowOverhead);
        addToRomfs(path, Compress(data, zsDictionary));
    }
    const bool copies = std::ranges::any_of(
        plan.messageTexts, [](const PlannedMessageText& planned) { return planned.copyOf.has_value(); });
    for (const std::string& path : std::views::keys(game.Files())) {
        const bool english = std::ranges::find(EnglishTextPacks, path) != std::end(EnglishTextPacks);
        const bool written = english ? !plan.stoneTexts.empty() || !plan.messageTexts.empty() : copies;
        if (!path.starts_with(TextPackFolder) || !path.ends_with(TextPackSuffix) || !written) {
            continue;
        }
        const std::string data = TextPack(game, path, zsDictionary, plan, english);
        tableChanged |= RaiseSize(table, path, data.size(), TextPackOverhead);
        addToRomfs(path, Compress(data, zsDictionary));
    }
    for (auto& [path, pack] : packs) {
        for (const auto& [name, list] : pack.goodsLists) {
            pack.files[name] = byml::Write(list);
        }
        const std::string data = sarc::Write(pack.files, sarc::ActorPackAlignment);
        tableChanged |= RaiseSize(table, path, data.size(), ActorPackOverhead);
        addToRomfs(path, Compress(data, packDictionary));
    }
    // Each new actor's pack.
    std::vector<const NewActor*> newActors;
    for (const auto* actors : { &goodsActors, &tokenCopies, &pickupCopies }) {
        for (const NewActor& actor : std::views::values(*actors)) {
            newActors.push_back(&actor);
        }
    }
    for (const NewActor* actor : newActors) {
        const std::string path = std::format("Pack/Actor/{}.pack.zs", actor->name);
        const std::string data = sarc::Write(actor->pack, sarc::ActorPackAlignment);
        AddSize(table, path, data.size(), ActorPackOverhead);
        tableChanged = true;
        addToRomfs(path, Compress(data, packDictionary));
    }
    for (auto& [path, rows] : tables) {
        if (newActors.empty() && path != PouchActorInfoPath) {
            continue;
        }
        for (const NewActor* actor : newActors) {
            AddRow(rows.root, actor->rows.find(path)->second);
        }
        const std::string data = byml::Write(rows);
        const size_t overhead = path == ActorInfoPath ? ActorInfoOverhead : ActorTableOverhead;
        tableChanged |= RaiseSize(table, path, data.size(), overhead);
        addToRomfs(path, Compress(data));
    }
    std::map<std::string, StartValue> startValues = plan.startValues;
    if (plan.openingSkipped && !startValues.emplace(NaviWokeLink, true).second) {
        throw std::runtime_error(std::format("the plan's start values already name {}", NaviWokeLink));
    }
    std::map<std::string, StartValue> found;
    for (const std::string& item : plan.startItems) {
        try {
            AddFind(tables.find(PouchActorInfoPath)->second.root, item, found);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("start item {}: {}", item, e.what()));
        }
    }
    for (auto& [name, value] : found) {
        if (!startValues.emplace(name, value).second) {
            throw std::runtime_error(std::format("the plan's start values already name {}, a start item's", name));
        }
    }
    if (!startValues.empty() || !soldOnce.empty() || !plan.looseItems.empty() || !plan.claims.empty() ||
        !plan.expressions.empty()) {
        if (const auto data = GameDataList(game, zsDictionary, startValues, soldOnce, plan.looseItems, plan.claims,
                                           plan.expressions)) {
            tableChanged |= RaiseSize(table, GameDataListPath, data->size(), GameDataListOverhead);
            addToRomfs(GameDataListPath, Compress(*data, zsDictionary));
        }
    }
    if (tableChanged) {
        addToRomfs(SizeTablePath, Compress(table));
    }
    return mod;
}

void WriteFile(const std::filesystem::path& path, std::string_view data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), std::streamsize(data.size()));
    file.close();
    if (!file) {
        throw std::runtime_error(std::format("{}: cannot write", path.string()));
    }
}
