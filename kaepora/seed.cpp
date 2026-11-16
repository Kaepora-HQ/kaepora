#include "seed.h"

#include "generator/generate.h"
#include "mod.h"

#include <format>
#include <map>
#include <stdexcept>
#include <system_error>

void MakeSeed(const std::string& seed, const std::filesystem::path& nup, const std::filesystem::path& seedDir) {
    // A file left by another seed would stay in the mod, so the folder must be new.
    if (std::filesystem::exists(seedDir)) {
        throw std::runtime_error(std::format("{}: already exists", seedDir.string()));
    }
    const GeneratedSeed generated = Generate(seed);
    const std::map<std::string, std::string> files = ModFiles(nup, ParsePlan(generated.plan));
    try {
        for (const auto& [path, data] : files) {
            WriteFile(seedDir / "romfs" / path, data);
        }
        WriteFile(seedDir / "spoiler.json", generated.spoilerLog);
    } catch (...) {
        // The folder did not exist, so everything in it is this call's.
        std::error_code ignored;
        std::filesystem::remove_all(seedDir, ignored);
        throw;
    }
}
