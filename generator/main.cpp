// Generates one seed and writes its spoiler log and the plan the patcher reads.
#include "generate.h"

#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>

static void Write(const char* path, const std::string& text) {
    std::ofstream out(path);
    out << text;
    out.close();
    if (!out) {
        throw std::runtime_error(std::string("cannot write ") + path);
    }
}

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5 || argv[1][0] == '\0') {
        std::fprintf(stderr, "usage: %s <seed> <spoiler.json> <plan.json> [<settings>]\n", argv[0]);
        return 2;
    }
    try {
        const SeedSettings settings = argc == 5 ? ParseSeedSettings(nlohmann::json::parse(argv[4])) : SeedSettings{};
        const GeneratedSeed seed = Generate(argv[1], settings);
        Write(argv[2], seed.spoilerLog);
        Write(argv[3], seed.plan.dump(2) + "\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
