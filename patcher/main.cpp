// Writes a chest plan into a mod folder: each placement file the plan changes, and the size table if a file outgrows
// its entry, at their paths in the game's RomFS.
#include "byml.h"
#include "bytes.h"
#include "compression.h"
#include "romfs.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

struct PlannedChest {
    uint64_t hash;
    std::vector<std::string> files; // RomFS paths of the placement files that hold the chest
    std::string dropActor;
};

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

static std::vector<PlannedChest> ReadPlan(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("cannot open");
        }
        const auto chests = nlohmann::json::parse(file).at("chests").get<std::vector<nlohmann::json>>();
        std::vector<PlannedChest> plan;
        for (size_t i = 0; i < chests.size(); ++i) {
            try {
                plan.push_back(ParseChest(chests[i]));
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("chests[{}]: {}", i, e.what()));
            }
        }
        return plan;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", path, e.what()));
    }
}

static byml::Node* Find(byml::Node& dict, std::string_view key) {
    const auto entry = std::ranges::find(dict.dict, key, &byml::Entry::key);
    return entry == dict.dict.end() ? nullptr : &entry->value;
}

// The Dynamic.Drop__DropActor string of the actor with this hash.
static std::string& DropActor(byml::Node& placement, uint64_t hash) {
    byml::Node* actors = Find(placement, "Actors");
    if (actors != nullptr) {
        for (byml::Node& actor : actors->array) {
            const byml::Node* actorHash = Find(actor, "Hash");
            if (actorHash == nullptr || actorHash->scalar != hash) {
                continue;
            }
            byml::Node* dynamic = Find(actor, "Dynamic");
            byml::Node* dropActor = dynamic == nullptr ? nullptr : Find(*dynamic, "Drop__DropActor");
            if (dropActor == nullptr || dropActor->type != byml::Type::String) {
                throw std::runtime_error("the chest's actor has no Dynamic.Drop__DropActor to set");
            }
            return dropActor->string;
        }
    }
    throw std::runtime_error("no actor in the file has this hash");
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

// Raises the size table entry of the file at this path if its decompressed size needs a larger one. Returns whether
// the entry changed.
static bool RaiseSize(std::string& table, std::string_view path, size_t size) {
    // The game silently skips a file that is too large for its entry, and loads one that is smaller. Its own entries
    // are the size rounded up to a multiple of 32, plus 264.
    constexpr size_t Overhead = 264;
    constexpr size_t Alignment = 32;

    const uint32_t key = Crc32(path.substr(0, path.size() - std::strlen(".zs")));
    const size_t end = SizeTableHeaderSize + ReadAt<uint32_t>(table, 0x0E) * SizeTableEntrySize;
    for (size_t entry = SizeTableHeaderSize; entry < end; entry += SizeTableEntrySize) {
        if (ReadAt<uint32_t>(table, entry) == key) {
            const uint32_t value = uint32_t((size + Alignment - 1) / Alignment * Alignment + Overhead);
            if (ReadAt<uint32_t>(table, entry + 4) >= value) {
                return false;
            }
            std::memcpy(table.data() + entry + 4, &value, sizeof(value));
            return true;
        }
    }
    throw std::runtime_error(std::format("{}: the size table has no entry for this file", path));
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <game.nup> <plan.json> <mod-dir>\n", argv[0]);
        return 2;
    }
    try {
        // A file left by another plan would stay in the mod, so the folder must be new.
        const std::filesystem::path modDir = argv[3];
        if (std::filesystem::exists(modDir)) {
            throw std::runtime_error(std::format("{}: already exists", modDir.string()));
        }
        const std::vector<PlannedChest> plan = ReadPlan(argv[2]);
        Romfs romfs(argv[1]);
        const std::string dictionary = Dictionary(romfs, "bcett.byml.zsdic");

        std::map<std::string, byml::Document> placements;
        std::set<std::string> changed;
        for (const PlannedChest& chest : plan) {
            for (const std::string& path : chest.files) {
                try {
                    auto placement = placements.find(path);
                    if (placement == placements.end()) {
                        placement = placements.emplace(path, byml::Parse(Decompress(romfs.Read(path), dictionary))).first;
                    }
                    std::string& dropActor = DropActor(placement->second.root, chest.hash);
                    if (dropActor != chest.dropActor) {
                        dropActor = chest.dropActor;
                        changed.insert(path);
                    }
                } catch (const std::exception& e) {
                    throw std::runtime_error(std::format("chest {} in {}: {}", chest.hash, path, e.what()));
                }
            }
        }

        std::map<std::string, std::string> mod; // the files to write, by RomFS path
        std::string table = ReadSizeTable(romfs);
        bool tableChanged = false;
        for (const std::string& path : changed) {
            const std::string data = byml::Write(placements.at(path));
            tableChanged |= RaiseSize(table, path, data.size());
            mod.emplace(path, Compress(data, dictionary));
        }
        if (tableChanged) {
            mod.emplace(SizeTablePath, Compress(table));
        }

        // Every check above passed, so writing starts only now and a bad plan leaves no file behind.
        for (const auto& [path, data] : mod) {
            const std::filesystem::path out = modDir / "romfs" / path;
            std::filesystem::create_directories(out.parent_path());
            std::ofstream file(out, std::ios::binary);
            file.write(data.data(), std::streamsize(data.size()));
            file.close();
            if (!file) {
                throw std::runtime_error(std::format("{}: cannot write", out.string()));
            }
            std::printf("%s\n", out.c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
