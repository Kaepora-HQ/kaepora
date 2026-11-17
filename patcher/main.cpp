#include "mod.h"

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>

static Plan ReadPlan(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("cannot open");
        }
        return ParsePlan(nlohmann::json::parse(file));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", path, e.what()));
    }
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
        const Plan plan = ReadPlan(argv[2]);
        // Every check passes before writing starts, so a bad plan leaves no file behind.
        for (const auto& [path, data] : ModFiles(argv[1], plan)) {
            const std::filesystem::path out = modDir / "romfs" / path;
            WriteFile(out, data);
            std::printf("%s\n", out.string().c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
