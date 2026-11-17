#include "mod.h"

#include "bfevfl.h"
#include "byml.h"
#include "bytes.h"
#include "compression.h"
#include "romfs.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
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

static PlannedGift ParseGift(const nlohmann::json& gift) {
    PlannedGift planned{ .flow = gift.at("flow").get<std::string>(),
                         .event = gift.at("event").get<std::string>(),
                         .param = gift.at("param").get<std::string>(),
                         .item = gift.at("item").get<std::string>() };
    if (planned.item.empty()) {
        throw std::runtime_error("item must not be empty");
    }
    return planned;
}

static std::string Name(const PlannedGift& gift) {
    return std::format("{} {} in {}", gift.event, gift.param, gift.flow);
}

Plan ParsePlan(const nlohmann::json& json) {
    const auto chests = json.at("chests").get<std::vector<nlohmann::json>>();
    const auto gifts = json.at("gifts").get<std::vector<nlohmann::json>>();
    Plan plan{ .startFlags = json.at("start_flags").get<std::vector<std::string>>() };
    for (size_t i = 0; i < chests.size(); ++i) {
        try {
            plan.chests.push_back(ParseChest(chests[i]));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("chests[{}]: {}", i, e.what()));
        }
    }
    std::set<std::tuple<std::string, std::string, std::string>> given;
    for (size_t i = 0; i < gifts.size(); ++i) {
        try {
            const PlannedGift& gift = plan.gifts.emplace_back(ParseGift(gifts[i]));
            if (!given.emplace(gift.flow, gift.event, gift.param).second) {
                throw std::runtime_error(std::format("{} is listed twice", Name(gift)));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("gifts[{}]: {}", i, e.what()));
        }
    }
    return plan;
}

static byml::Node* Find(byml::Node& dict, std::string_view key) {
    const auto entry = std::ranges::find(dict.dict, key, &byml::Entry::key);
    return entry == dict.dict.end() ? nullptr : &entry->value;
}

// The actor with this hash.
static byml::Node& Actor(byml::Node& placement, uint64_t hash) {
    byml::Node* actors = Find(placement, "Actors");
    if (actors != nullptr) {
        for (byml::Node& actor : actors->array) {
            const byml::Node* actorHash = Find(actor, "Hash");
            if (actorHash != nullptr && actorHash->scalar == hash) {
                return actor;
            }
        }
    }
    throw std::runtime_error("no actor in the file has this hash");
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

// The size table, decompressed.
static std::string ReadSizeTable(Romfs& romfs) {
    try {
        std::string table = Decompress(romfs.Read(std::string(SizeTablePath)));
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
constexpr size_t PlacementOverhead = 264;
constexpr size_t EventFlowOverhead = 272;

// Raises the size table entry of the file at this path if its decompressed size needs a larger one. Returns whether
// the entry changed.
static bool RaiseSize(std::string& table, std::string_view path, size_t size, size_t overhead) {
    // The game silently skips a file that is too large for its entry, and loads one that is smaller.
    constexpr size_t Alignment = 32;

    const uint32_t key = Crc32(path.substr(0, path.size() - std::strlen(".zs")));
    const size_t end = SizeTableHeaderSize + ReadAt<uint32_t>(table, 0x0E) * SizeTableEntrySize;
    for (size_t entry = SizeTableHeaderSize; entry < end; entry += SizeTableEntrySize) {
        if (ReadAt<uint32_t>(table, entry) == key) {
            const uint32_t value = uint32_t(AlignUp(size, Alignment) + overhead);
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

// The game data list with the default of each named Bool set to true, compressed as the game's own copy is. A Bool's
// default sits inside its entry, so the list keeps its size and still fits its size table entry.
static std::string StartFlagList(Romfs& romfs, const std::string& dictionary, const std::vector<std::string>& names) {
    try {
        byml::Document list = byml::Parse(Decompress(romfs.Read(std::string(GameDataListPath)), dictionary));
        byml::Node* data = Find(list.root, "Data");
        byml::Node* bools = data == nullptr ? nullptr : Find(*data, "Bool");
        if (bools == nullptr) {
            throw std::runtime_error("the list has no Data.Bool");
        }
        for (const std::string& name : names) {
            const uint32_t hash = Murmur3(name);
            const auto entry = std::ranges::find_if(bools->array, [&](byml::Node& candidate) {
                const byml::Node* candidateHash = Find(candidate, "Hash");
                return candidateHash != nullptr && candidateHash->scalar == hash;
            });
            byml::Node* value = entry == bools->array.end() ? nullptr : Find(*entry, "DefaultValue");
            if (value == nullptr || value->type != byml::Type::Bool) {
                throw std::runtime_error(
                    std::format("start flag \"{}\" is not a true-or-false value in the list", name));
            }
            if (value->scalar != 0) {
                throw std::runtime_error(std::format("start flag \"{}\" is already true by default, or is listed twice", name));
            }
            value->scalar = 1;
        }
        return Compress(byml::Write(list), dictionary);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", GameDataListPath, e.what()));
    }
}


std::map<std::string, std::string> ModFiles(const std::filesystem::path& nup, const Plan& plan) {
    Romfs romfs(nup);
    const std::string dictionary = Dictionary(romfs, "bcett.byml.zsdic");

    std::map<std::string, byml::Document> placements;
    std::set<std::string> changed;
    for (const PlannedChest& chest : plan.chests) {
        for (const std::string& path : chest.files) {
            try {
                auto placement = placements.find(path);
                if (placement == placements.end()) {
                    placement = placements.emplace(path, byml::Parse(Decompress(romfs.Read(path), dictionary))).first;
                }
                if (SetItem(Actor(placement->second.root, chest.hash), chest.dropActor)) {
                    changed.insert(path);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("chest {} in {}: {}", chest.hash, path, e.what()));
            }
        }
    }

    const std::string zsDictionary = Dictionary(romfs, "zs.zsdic");
    std::map<std::string, bfevfl::Flow> flows;
    std::set<std::string> changedFlows;
    for (const PlannedGift& gift : plan.gifts) {
        try {
            auto flow = flows.find(gift.flow);
            if (flow == flows.end()) {
                flow = flows.emplace(gift.flow, bfevfl::Parse(Decompress(romfs.Read(gift.flow), zsDictionary))).first;
            }
            if (bfevfl::SetString(flow->second, gift.event, gift.param, gift.item)) {
                changedFlows.insert(gift.flow);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("gift {}: {}", Name(gift), e.what()));
        }
    }

    std::map<std::string, std::string> mod;
    std::string table = ReadSizeTable(romfs);
    bool tableChanged = false;
    for (const std::string& path : changed) {
        const std::string data = byml::Write(placements.at(path));
        tableChanged |= RaiseSize(table, path, data.size(), PlacementOverhead);
        mod.emplace(path, Compress(data, dictionary));
    }
    for (const std::string& path : changedFlows) {
        const std::string data = bfevfl::Write(flows.at(path));
        tableChanged |= RaiseSize(table, path, data.size(), EventFlowOverhead);
        mod.emplace(path, Compress(data, zsDictionary));
    }
    if (tableChanged) {
        mod.emplace(SizeTablePath, Compress(table));
    }
    if (!plan.startFlags.empty()) {
        mod.emplace(GameDataListPath, StartFlagList(romfs, zsDictionary, plan.startFlags));
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
