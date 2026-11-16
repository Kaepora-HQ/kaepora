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
    if (argc != 4 || argv[1][0] == '\0') {
        std::fprintf(stderr, "usage: %s <seed> <spoiler.json> <plan.json>\n", argv[0]);
        return 2;
    }
    try {
        const GeneratedSeed seed = Generate(argv[1]);
        Write(argv[2], seed.spoilerLog);
        Write(argv[3], seed.plan.dump(2) + "\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
