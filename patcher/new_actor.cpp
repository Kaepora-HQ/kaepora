#include "new_actor.h"

#include "bytes.h"
#include "compression.h"

#include <algorithm>
#include <bit>
#include <format>
#include <stdexcept>

// The goods actor of the Zora Tunic, for adult Link's shelf. A new goods actor is its pack without these members,
// which are the tunic's own.
constexpr std::string_view GoodsTemplate = "Shop_Armor_Zora";
constexpr std::string_view TemplateTable =
    "GameParameter/GameParameterTable/Obj_Armor_Zora.engine__actor__GameParameterTable.bgyml";
constexpr std::array<std::string_view, 10> TemplateOwn = {
    "Actor/Shop_Armor_Zora.engine__actor__ActorParam.bgyml",
    "Component/AnimationParam/Obj_Armor_Zora.engine__component__AnimationParam.bgyml",
    "Component/ModelInfo/Obj_Armor_Zora.engine__component__ModelInfo.bgyml",
    "Component/PouchContentParam/Obj_Armor_Zora.game__component__PouchContentParam.bgyml",
    "Component/PouchContentParam/Obj_Armor_Goron.game__component__PouchContentParam.bgyml",
    "Component/PouchContentParam/ArmorDefault.game__component__PouchContentParam.bgyml",
    "Component/PouchContentParam/ArmorGetTypeMedium.game__component__PouchContentParam.bgyml",
    TemplateTable,
    "GameParameter/PlayerEquipmentActor/Obj_Armor_Zora.game__actor__PlayerEquipmentActor.bgyml",
    "GameParameter/PriceParam/Obj_Armor_Zora.game__pouchcontent__PriceParam.bgyml",
};
constexpr std::string_view ZeroPricePack = "Shop_Mask_Gerudo";
constexpr std::string_view ZeroPrice = "GameParameter/PriceParam/Item_Mask_Gerudo.game__pouchcontent__PriceParam.bgyml";

static std::string ActorMember(std::string_view actor) {
    return std::format("Actor/{}.engine__actor__ActorParam.bgyml", actor);
}

std::string WorkPath(std::string_view actor) {
    return std::format("Work/Actor/{}.engine__actor__ActorParam.gyml", actor);
}

static std::string PouchMember(std::string_view actor) {
    return std::format("Component/PouchContentParam/{}.game__component__PouchContentParam.bgyml", actor);
}

// The pack member a reference names: "?Component/X.bgyml" names Component/X.bgyml, and a $parent
// "Work/Actor/X.gyml" names Actor/X.bgyml.
static std::string Member(std::string_view ref) {
    if (ref.starts_with('?')) {
        return std::string(ref.substr(1));
    }
    if (!ref.starts_with("Work/") || !ref.ends_with(".gyml")) {
        throw std::runtime_error(std::format("\"{}\" names no pack member", ref));
    }
    return std::format("{}.bgyml", ref.substr(5, ref.size() - 10));
}

static sarc::Files Pack(GameFile& game, const std::string& dictionary, std::string_view actor) {
    const std::string path = std::format("Pack/Actor/{}.pack.zs", actor);
    try {
        return sarc::Parse(Decompress(game.Read(path), dictionary));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", path, e.what()));
    }
}

static byml::Node& Value(byml::Node& dict, std::string_view key) {
    byml::Node* value = byml::Find(dict, key);
    if (value == nullptr) {
        throw std::runtime_error(std::format("no {}", key));
    }
    return *value;
}

static byml::Node Merged(byml::Node parent, const byml::Node& child) {
    for (const byml::Entry& entry : child.dict) {
        byml::Node* value = byml::Find(parent, entry.key);
        if (value != nullptr && value->type == byml::Type::Dict && entry.value.type == byml::Type::Dict) {
            *value = Merged(*value, entry.value);
        } else {
            byml::FindOrAdd(parent, entry.key, entry.value.type) = entry.value;
        }
    }
    return parent;
}

// Every $parent chain ends at a parent that no pack holds.
static byml::Node Resolved(const sarc::Files& pack, const std::string& member) {
    byml::Node node = byml::Parse(pack.at(member)).root;
    const byml::Node* parent = byml::Find(node, "$parent");
    if (parent == nullptr) {
        return node;
    }
    const std::string path = Member(parent->string);
    std::erase_if(node.dict, [](const byml::Entry& entry) { return entry.key == "$parent"; });
    return pack.contains(path) ? Merged(Resolved(pack, path), node) : node;
}

static void Add(sarc::Files& pack, const std::string& member, const std::string& data) {
    const auto [at, added] = pack.emplace(member, data);
    if (!added && at->second != data) {
        throw std::runtime_error(std::format("the new pack already holds another {}", member));
    }
}

static void AddWithParents(sarc::Files& pack, const sarc::Files& from, std::string member) {
    while (from.contains(member)) {
        Add(pack, member, from.at(member));
        byml::Node node = byml::Parse(from.at(member)).root;
        const byml::Node* parent = byml::Find(node, "$parent");
        if (parent == nullptr) {
            return;
        }
        member = Member(parent->string);
    }
}

const std::string& RowId(const byml::Node& row) {
    const auto id = std::ranges::find(row.dict, "__RowId", &byml::Entry::key);
    if (id == row.dict.end()) {
        throw std::runtime_error("an actor table row has no __RowId");
    }
    return id->value.string;
}

byml::Node& Row(byml::Node& table, std::string_view actor) {
    const auto row = std::ranges::find(table.array, actor, RowId);
    if (row == table.array.end()) {
        throw std::runtime_error(std::format("the actor table has no row for {}", actor));
    }
    return *row;
}

static byml::Node& Row(ActorTables& tables, std::string_view table, std::string_view actor) {
    return Row(tables.find(table)->second.root, actor);
}

static float FloatOf(const byml::Node& node) {
    return std::bit_cast<float>(uint32_t(node.scalar));
}

// The template's row, showing the item: its name, model, animation and bounds. Its heaps take the item's sizes plus
// half again, since a goods actor needs less than the item's own actor with the same model (85,672 bytes for the
// Hylian Shield's goods against 93,432 for its chest item), and its radii the larger of the two.
static byml::Node ActorInfoRow(ActorTables& tables, std::string_view shownOn, const std::string& item) {
    byml::Node row = Row(tables, ActorInfoPath, shownOn);
    byml::Node& own = Row(tables, ActorInfoPath, item);
    for (const std::string_view key :
         { "ActorName", "AnimationResources", "FmdbName", "ModelProjectName", "ModelAabbMax", "ModelAabbMin" }) {
        Value(row, key) = Value(own, key);
    }
    for (const std::string_view key : { "InstanceHeapSize", "PreActorRenderInfoHeapSize" }) {
        byml::Node& heap = Value(row, key);
        heap.scalar = AlignUp(std::max(heap.scalar, Value(own, key).scalar * 3 / 2), 8);
    }
    for (const std::string_view key : { "CalcRadius", "DisplayRadius", "LoadRadius" }) {
        byml::Node& radius = Value(row, key);
        radius = FloatOf(Value(own, key)) > FloatOf(radius) ? Value(own, key) : radius;
    }
    return row;
}

NewActor GoodsActor(GameFile& game, const std::string& packDictionary, ActorTables& tables, const std::string& item) {
    NewActor actor{ .name = std::format("Shop_{}", item) };
    if (game.Files().contains(std::format("Pack/Actor/{}.pack.zs", actor.name))) {
        throw std::runtime_error(std::format("the game already has an actor {}", actor.name));
    }
    const sarc::Files own = Pack(game, packDictionary, item);
    sarc::Files& pack = actor.pack = Pack(game, packDictionary, GoodsTemplate);
    byml::Document param = byml::Parse(pack.at(ActorMember(GoodsTemplate)));
    byml::Document table = byml::Parse(pack.at(std::string(TemplateTable)));
    for (const std::string_view member : TemplateOwn) {
        pack.erase(std::string(member));
    }

    byml::Node ownActor = Resolved(own, ActorMember(item));
    byml::Node& ownComponents = Value(ownActor, "Components");
    byml::Node& components = Value(param.root, "Components");
    for (const std::string_view key : { "ActorNameRef", "ModelInfoRef", "AnimationRef" }) {
        Value(components, key) = Value(ownComponents, key);
    }
    AddWithParents(pack, own, Member(Value(ownComponents, "ModelInfoRef").string));
    AddWithParents(pack, own, Member(Value(ownComponents, "AnimationRef").string));

    // The item's pouch content, under the goods actor's name, gives the item where the item's actor gives itself.
    const std::string ownPouch = Member(Value(ownComponents, "PouchContentRef").string);
    byml::Document pouch = byml::Parse(own.at(ownPouch));
    if (const byml::Node* parent = byml::Find(pouch.root, "$parent")) {
        AddWithParents(pack, own, Member(parent->string));
    }
    byml::Node gives = Resolved(own, ownPouch);
    if (byml::Find(gives, "PouchActor") == nullptr && byml::Find(gives, "BundleActor") == nullptr) {
        byml::FindOrAdd(pouch.root, "PouchActor", byml::Type::String).string = WorkPath(item);
    }
    byml::FindOrAdd(pouch.root, "IsNotOpenStockNumDisplay", byml::Type::Bool).scalar = 1;
    const std::string pouchMember = PouchMember(actor.name);
    Add(pack, pouchMember, byml::Write(pouch));
    Value(components, "PouchContentRef").string = "?" + pouchMember;

    // The tunic's table, without what Link wears, and a price of 0.
    const std::string price =
        std::format("GameParameter/PriceParam/{}.game__pouchcontent__PriceParam.bgyml", actor.name);
    byml::Node& tableComponents = Value(table.root, "Components");
    std::erase_if(tableComponents.dict, [](const byml::Entry& entry) { return entry.key == "PlayerEquipmentActor"; });
    Value(tableComponents, "PriceParam").string = "?" + price;
    Add(pack, price, Pack(game, packDictionary, ZeroPricePack).at(std::string(ZeroPrice)));
    const std::string tableMember =
        std::format("GameParameter/GameParameterTable/{}.engine__actor__GameParameterTable.bgyml", actor.name);
    Add(pack, tableMember, byml::Write(table));
    Value(components, "GameParameterTableRef").string = "?" + tableMember;
    Add(pack, ActorMember(actor.name), byml::Write(param));

    // The item's pouch row with no BuyingPrice, as the Megaton Hammer's has none, so the base price is 0 there too.
    byml::Node pouchRow = Row(tables, PouchActorInfoPath, item);
    std::erase_if(pouchRow.dict, [](const byml::Entry& entry) { return entry.key == "BuyingPrice"; });
    actor.rows = { { std::string(ActorInfoPath), ActorInfoRow(tables, GoodsTemplate, item) },
                   { std::string(GameActorInfoPath), Row(tables, GameActorInfoPath, GoodsTemplate) },
                   { std::string(PouchActorInfoPath), pouchRow } };
    for (auto& [_, row] : actor.rows) {
        Value(row, "__RowId").string = actor.name;
    }
    return actor;
}

constexpr std::string_view Token = "Obj_Token_Gold";
constexpr std::array<std::string_view, 5> TokenPickUp = {
    "DefaultAutoGet", "GetDemoOffsetPos", "IsPickedUpEmitXlinkKey", "PickedUpEmitXlinkKey", "PlayGetEventManyTimes",
};

NewActor TokenCopy(GameFile& game, const std::string& packDictionary, ActorTables& tables, const std::string& item) {
    NewActor actor{ .name = std::format("{}_{}", Token, item) };
    if (game.Files().contains(std::format("Pack/Actor/{}.pack.zs", actor.name))) {
        throw std::runtime_error(std::format("the game already has an actor {}", actor.name));
    }
    const sarc::Files own = Pack(game, packDictionary, item);
    sarc::Files& pack = actor.pack = Pack(game, packDictionary, Token);
    byml::Document param = byml::Parse(pack.at(ActorMember(Token)));
    byml::Document pouch = byml::Parse(pack.at(PouchMember(Token)));
    pack.erase(ActorMember(Token));
    pack.erase(PouchMember(Token));

    byml::Node ownActor = Resolved(own, ActorMember(item));
    byml::Node& ownComponents = Value(ownActor, "Components");
    byml::Node& components = Value(param.root, "Components");
    Value(components, "ActorNameRef") = Value(ownComponents, "ActorNameRef");
    const std::string ownPouch = Member(Value(ownComponents, "PouchContentRef").string);
    AddWithParents(pack, own, ownPouch);

    std::erase_if(pouch.root.dict, [](const byml::Entry& entry) {
        return std::ranges::find(TokenPickUp, entry.key) == TokenPickUp.end();
    });
    byml::FindOrAdd(pouch.root, "$parent", byml::Type::String).string =
        std::format("Work/{}.gyml", ownPouch.substr(0, ownPouch.size() - std::string_view(".bgyml").size()));
    byml::Node gives = Resolved(own, ownPouch);
    if (const byml::Node* named = byml::Find(gives, "PouchActor")) {
        byml::FindOrAdd(pouch.root, "PouchActor", byml::Type::String) = *named;
    } else if (byml::Find(gives, "BundleActor") == nullptr) {
        byml::FindOrAdd(pouch.root, "PouchActor", byml::Type::String).string = WorkPath(item);
    }
    Add(pack, PouchMember(actor.name), byml::Write(pouch));
    Value(components, "PouchContentRef").string = "?" + PouchMember(actor.name);
    Add(pack, ActorMember(actor.name), byml::Write(param));

    byml::Node actorInfo = Row(tables, ActorInfoPath, Token);
    Value(actorInfo, "ActorName") = Value(Row(tables, ActorInfoPath, item), "ActorName");
    actor.rows = { { std::string(ActorInfoPath), actorInfo },
                   { std::string(GameActorInfoPath), Row(tables, GameActorInfoPath, Token) },
                   { std::string(PouchActorInfoPath), Row(tables, PouchActorInfoPath, item) } };
    for (auto& [_, row] : actor.rows) {
        Value(row, "__RowId").string = actor.name;
    }
    return actor;
}

struct PickupTemplate {
    std::string_view actor;
    std::string_view copyPrefix;
    std::array<std::string_view, 7> ownMembers;
    std::array<std::string_view, 3> itemRefs;
    std::array<std::string_view, 3> takeKeys;
};

// The game's Hylian Shield pickup, which Link takes by touch as he takes a rupee or a small key. Seen in the game with
// another item: no glow and no sound, taken by touch, and recorded as taken.
constexpr PickupTemplate Pickup{
    .actor = "Obj_Shield_Hylian",
    .copyPrefix = "Obj_PickupShield_",
    .ownMembers = {
        "Actor/Obj_Shield_Hylian.engine__actor__ActorParam.bgyml",
        "Component/AnimationParam/Obj_Shield_Hylian.engine__component__AnimationParam.bgyml",
        "Component/ModelInfo/Obj_Shield_Hylian.engine__component__ModelInfo.bgyml",
        "Component/PouchContentParam/AutoPickShield.game__component__PouchContentParam.bgyml",
        "Component/PouchContentParam/Obj_Shield_Hylian.game__component__PouchContentParam.bgyml",
        "GameParameter/GameParameterTable/Obj_Shield_Hylian.engine__actor__GameParameterTable.bgyml",
        "GameParameter/PlayerEquipmentActor/Obj_Shield_Hylian.game__actor__PlayerEquipmentActor.bgyml",
    },
    .itemRefs = { "ActorNameRef", "ModelInfoRef", "AnimationRef" },
    .takeKeys = { "DefaultAutoGet", "IsPickedUpEmitXlinkKey", "PickedUpEmitXlinkKey" },
};
// Bundles and dungeon items have no parameter table of their own. Their pickup takes the pickup base's, which the small
// key's pack holds.
constexpr std::string_view BaseTablePack = "Obj_Dungeon_SmallKey";
constexpr std::string_view BaseTable =
    "GameParameter/GameParameterTable/Obj_Item_Base.engine__actor__GameParameterTable.bgyml";

// A reference to an actor names that actor, not a member to carry.
static void NamedMembers(const byml::Node& node, std::vector<std::string>& members) {
    if (node.type == byml::Type::String &&
        (node.string.starts_with('?') || (node.string.starts_with("Work/") && node.string.ends_with(".gyml"))) &&
        !Member(node.string).starts_with("Actor/")) {
        members.push_back(Member(node.string));
    }
    for (const byml::Node& item : node.array) {
        NamedMembers(item, members);
    }
    for (const byml::Entry& entry : node.dict) {
        NamedMembers(entry.value, members);
    }
}

static void AddTree(sarc::Files& pack, const sarc::Files& from, const std::string& member) {
    if (!from.contains(member)) {
        return;
    }
    const bool carried = pack.contains(member);
    Add(pack, member, from.at(member));
    if (carried || !member.ends_with(".bgyml")) {
        return;
    }
    std::vector<std::string> named;
    NamedMembers(byml::Parse(from.at(member)).root, named);
    for (const std::string& name : named) {
        AddTree(pack, from, name);
    }
}

NewActor PickupCopy(GameFile& game, const std::string& packDictionary, ActorTables& tables, const std::string& item) {
    NewActor actor{ .name = std::format("{}{}", Pickup.copyPrefix, item) };
    if (game.Files().contains(std::format("Pack/Actor/{}.pack.zs", actor.name))) {
        throw std::runtime_error(std::format("the game already has an actor {}", actor.name));
    }
    const sarc::Files own = Pack(game, packDictionary, item);
    sarc::Files& pack = actor.pack = Pack(game, packDictionary, Pickup.actor);
    byml::Document param = byml::Parse(pack.at(ActorMember(Pickup.actor)));
    byml::Node taken = Resolved(pack, Member(Value(Value(param.root, "Components"), "PouchContentRef").string));
    for (const std::string_view member : Pickup.ownMembers) {
        pack.erase(std::string(member));
    }

    byml::Node ownActor = Resolved(own, ActorMember(item));
    byml::Node& ownComponents = Value(ownActor, "Components");
    byml::Node& components = Value(param.root, "Components");
    for (const std::string_view key : Pickup.itemRefs) {
        const byml::Node& ref = Value(components, key) = Value(ownComponents, key);
        AddTree(pack, own, Member(ref.string));
    }
    if (const byml::Node* table = byml::Find(ownComponents, "GameParameterTableRef")) {
        Value(components, "GameParameterTableRef") = *table;
        AddTree(pack, own, Member(table->string));
    } else {
        std::erase_if(components.dict, [](const byml::Entry& entry) { return entry.key == "GameParameterTableRef"; });
        AddTree(pack, Pack(game, packDictionary, BaseTablePack), std::string(BaseTable));
    }

    // A child of the item's pouch content, which gives the item where the item's actor gives itself.
    const std::string ownPouch = Member(Value(ownComponents, "PouchContentRef").string);
    AddTree(pack, own, ownPouch);
    byml::Document pouch{ .order = param.order, .root = { .type = byml::Type::Dict } };
    byml::FindOrAdd(pouch.root, "$parent", byml::Type::String).string =
        std::format("Work/{}.gyml", ownPouch.substr(0, ownPouch.rfind(".bgyml")));
    for (const std::string_view key : Pickup.takeKeys) {
        const byml::Node& value = Value(taken, key);
        byml::FindOrAdd(pouch.root, std::string(key), value.type) = value;
    }
    byml::Node gives = Resolved(own, ownPouch);
    if (byml::Find(gives, "PouchActor") == nullptr && byml::Find(gives, "BundleActor") == nullptr) {
        byml::FindOrAdd(pouch.root, "PouchActor", byml::Type::String).string = WorkPath(item);
    }
    Add(pack, PouchMember(actor.name), byml::Write(pouch));
    Value(components, "PouchContentRef").string = "?" + PouchMember(actor.name);
    Add(pack, ActorMember(actor.name), byml::Write(param));

    // A pickup's PouchActorInfo row is its item's, as Obj_Arrow_Fire's is Item_Arrow_Fire's.
    actor.rows = { { std::string(ActorInfoPath), ActorInfoRow(tables, Pickup.actor, item) },
                   { std::string(GameActorInfoPath), Row(tables, GameActorInfoPath, Pickup.actor) },
                   { std::string(PouchActorInfoPath), Row(tables, PouchActorInfoPath, item) } };
    for (auto& [_, row] : actor.rows) {
        Value(row, "__RowId").string = actor.name;
    }
    return actor;
}
