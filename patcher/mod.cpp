#include "mod.h"

#include "byml.h"
#include "compression.h"
#include "romfs.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <set>
#include <stdexcept>

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

Plan ParsePlan(const nlohmann::json& json) {
    const auto chests = json.at("chests").get<std::vector<nlohmann::json>>();
    Plan plan;
    for (size_t i = 0; i < chests.size(); ++i) {
        try {
            plan.chests.push_back(ParseChest(chests[i]));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("chests[{}]: {}", i, e.what()));
        }
    }
    return plan;
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

std::map<std::string, std::string> ModFiles(const std::filesystem::path& nup, const Plan& plan) {
    Romfs romfs(nup);
    const std::string dictionary = Dictionary(romfs, "bcett.byml.zsdic");

    std::map<std::string, byml::Node> placements;
    std::set<std::string> changed;
    for (const PlannedChest& chest : plan.chests) {
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

    std::map<std::string, std::string> mod;
    for (const std::string& path : changed) {
        mod.emplace(path, Compress(byml::Write(placements.at(path)), dictionary));
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
