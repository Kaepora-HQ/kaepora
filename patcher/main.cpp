// Writes a chest plan into a mod folder: each placement file the plan changes, at its path in the game's RomFS.
#include "byml.h"
#include "compression.h"
#include "romfs.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
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
        const std::string dictionary = PlacementDictionary(romfs);

        std::map<std::string, byml::Node> placements;
        std::set<std::string> changed;
        for (const PlannedChest& chest : plan) {
            for (const std::string& path : chest.files) {
                try {
                    auto placement = placements.find(path);
                    if (placement == placements.end()) {
                        placement = placements.emplace(path, byml::Parse(Decompress(romfs.Read(path), dictionary))).first;
                    }
                    std::string& dropActor = DropActor(placement->second, chest.hash);
                    if (dropActor != chest.dropActor) {
                        dropActor = chest.dropActor;
                        changed.insert(path);
                    }
                } catch (const std::exception& e) {
                    throw std::runtime_error(std::format("chest {} in {}: {}", chest.hash, path, e.what()));
                }
            }
        }

        // Every check above passed, so writing starts only now and a bad plan leaves no file behind.
        for (const std::string& path : changed) {
            const std::filesystem::path out = modDir / "romfs" / path;
            const std::string data = Compress(byml::Write(placements.at(path)), dictionary);
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
