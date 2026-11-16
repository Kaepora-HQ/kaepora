// Generates one seed and writes its spoiler log and the plan the patcher reads.
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/3drando/playthrough.hpp"
#include "soh/util.h"
#include "remake.h"
#include "setup.h"
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

// SpoilerLog_Write fills this.
extern nlohmann::ordered_json jsonData;

// No location is excluded and no trick is enabled, so both sets are empty.
static bool Generate(const std::string& seed, const std::vector<RemakeChest>& chests) {
    auto ctx = Rando::Context::GetInstance();
    ctx->SetSeedString(seed);
    ctx->SetSeed(SohUtils::Hash(seed));
    ctx->ClearItemLocations();
    return Playthrough::Playthrough_Init(ctx->GetSeed(), {}, {}, chests) > 0;
}

static void Write(const char* path, const nlohmann::ordered_json& json, int indent) {
    std::ofstream out(path);
    out << json.dump(indent) << std::endl;
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", path);
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    if (argc != 4 || argv[1][0] == '\0') {
        std::fprintf(stderr, "usage: %s <seed> <spoiler.json> <plan.json>\n", argv[0]);
        return 2;
    }

    InitializeRandomizer();
    const std::vector<RemakeChest> chests = LoadRemakeChests();
    if (!Generate(argv[1], chests)) {
        std::fprintf(stderr, "generation failed\n");
        return 1;
    }
    Write(argv[2], jsonData, 4);
    Write(argv[3], RemakePlan(argv[1], chests), 2);
    return 0;
}
