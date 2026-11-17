// Makes seed folders with the window's MakeSeed, each three arguments one call, all in one process.
#include "kaepora/seed.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    if (argc < 4 || (argc - 1) % 3 != 0) {
        std::fprintf(stderr, "usage: %s <seed> <game.nup> <seed-dir> [<seed> <game.nup> <seed-dir>]...\n", argv[0]);
        return 2;
    }
    try {
        for (int i = 1; i < argc; i += 3) {
            MakeSeed(argv[i], {}, argv[i + 1], argv[i + 2]);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
