#include "mod.h"

#include "bfevfl.h"
#include "byml.h"
#include "bytes.h"
#include "compression.h"
#include "game_file.h"
#include "program_patch.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

static PlannedChest ParseChest(const nlohmann::json& chest) {
    const std::string hash = chest.at("hash").get<std::string>();
    PlannedChest planned{ .files = chest.at("files").get<std::vector<std::string>>(),
                          .dropActor = chest.at("drop_actor").get<std::string>() };
    const auto [end, error] = std::from_chars(hash.data(), hash.data() + hash.size(), planned.hash);
    if (error != std::errc() || end != hash.data() + hash.size()) {
        throw std::runtime_error(std::format("hash \"{}\" is not a 64-bit decimal number", hash));
    }
    if (planned.files.empty() || planned.dropActor.empty()) {
        throw std::runtime_error("files and drop_actor must not be empty");
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

static std::string Name(const PlannedEventText& text) {
    return std::format("{} {} in {}", text.event, text.param, text.flow);
}

Plan ParsePlan(const nlohmann::json& json) {
    const auto chests = json.at("chests").get<std::vector<nlohmann::json>>();
    const auto texts = json.at("event_texts").get<std::vector<nlohmann::json>>();
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
                throw std::runtime_error(std::format("{} is listed twice", Name(text)));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("event_texts[{}]: {}", i, e.what()));
        }
    }
    return plan;
}

static byml::Node* Find(byml::Node& dict, std::string_view key) {
    const auto entry = std::ranges::find(dict.dict, key, &byml::Entry::key);
    return entry == dict.dict.end() ? nullptr : &entry->value;
}

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

// A chest gives its Dynamic.Drop__DropActor. One without it gives a refill of what Link is lowest on, so the item is
// added, in key order as the game's dicts are. One with a Drop__DropActor_Substitute gives that instead to Link who
// owns its Drop__CheckActor, so the substitute takes the item too. Returns whether the actor changed.
static bool SetItem(byml::Node& chest, const std::string& item) {
    byml::Node* dynamic = Find(chest, "Dynamic");
    if (dynamic == nullptr) {
        throw std::runtime_error("the chest's actor has no Dynamic values");
    }
    const std::string dropActor = "Drop__DropActor";
    const auto at = std::ranges::lower_bound(dynamic->dict, dropActor, {}, &byml::Entry::key);
    if (at == dynamic->dict.end() || at->key != dropActor) {
        dynamic->dict.insert(at, { .key = dropActor, .value = { .type = byml::Type::String } });
    }
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
};

static byml::Node& DefaultValue(byml::Node& data, const std::string& name, const StartValue& value) {
    const auto& [kind, type, words] = startValueKinds[value.index()];
    byml::Node* values = Find(data, kind);
    byml::Node* entry = values == nullptr ? nullptr : FindHash(*values, Murmur3(name));
    byml::Node* defaultValue = entry == nullptr ? nullptr : Find(*entry, "DefaultValue");
    if (defaultValue == nullptr || defaultValue->type != type) {
        throw std::runtime_error(std::format("start value \"{}\" is not {} in the list", name, words));
    }
    return *defaultValue;
}

// A Bool's or an Int's default sits inside its entry, so the list keeps its size and still fits its size table entry.
static std::string StartValueList(GameFile& game, const std::string& dictionary,
                                  const std::map<std::string, StartValue>& values) {
    try {
        byml::Document list = byml::Parse(Decompress(game.Read(std::string(GameDataListPath)), dictionary));
        byml::Node* data = Find(list.root, "Data");
        if (data == nullptr) {
            throw std::runtime_error("the list has no Data");
        }
        for (const auto& [name, value] : values) {
            byml::Node& defaultValue = DefaultValue(*data, name, value);
            const uint64_t scalar = std::visit([](auto v) { return uint64_t(uint32_t(v)); }, value);
            if (defaultValue.scalar == scalar) {
                throw std::runtime_error(std::format("start value \"{}\" is already {} by default", name,
                                                     std::visit([](auto v) { return std::format("{}", v); }, value)));
            }
            defaultValue.scalar = scalar;
        }
        return Compress(byml::Write(list), dictionary);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", GameDataListPath, e.what()));
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
    std::set<std::string> changed;
    for (const PlannedChest& chest : plan.chests) {
        for (const std::string& path : chest.files) {
            try {
                auto placement = placements.find(path);
                if (placement == placements.end()) {
                    placement = placements.emplace(path, byml::Parse(Decompress(game.Read(path), dictionary))).first;
                }
                if (SetItem(Actor(placement->second.root, chest.hash), chest.dropActor)) {
                    changed.insert(path);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("chest {} in {}: {}", chest.hash, path, e.what()));
            }
        }
    }

    const std::string zsDictionary = Dictionary(game, "zs.zsdic");
    std::map<std::string, bfevfl::Flow> flows;
    std::set<std::string> changedFlows;
    for (const PlannedEventText& text : plan.eventTexts) {
        try {
            auto flow = flows.find(text.flow);
            if (flow == flows.end()) {
                flow = flows.emplace(text.flow, bfevfl::Parse(Decompress(game.Read(text.flow), zsDictionary))).first;
            }
            if (bfevfl::SetString(flow->second, text.event, text.param, text.text)) {
                changedFlows.insert(text.flow);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("event text {}: {}", Name(text), e.what()));
        }
    }

    std::map<std::string, std::string> mod{ { std::format("exefs/{}.ips", PatchedBuildId), ProgramPatch() } };
    const auto addToRomfs = [&](std::string_view path, std::string data) {
        mod.emplace(std::format("romfs/{}", path), std::move(data));
    };
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
    if (tableChanged) {
        addToRomfs(SizeTablePath, Compress(table));
    }
    if (!plan.startValues.empty()) {
        addToRomfs(GameDataListPath, StartValueList(game, zsDictionary, plan.startValues));
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
