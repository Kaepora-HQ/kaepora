#include "compression.h"
#include "game_file.h"
#include "sarc.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>

constexpr int ActorPacks = 8373;

static bool RoundTrips(GameFile& game, const std::string& path, const std::string& dictionary) {
    try {
        const std::string original = Decompress(game.Read(path), dictionary);
        const std::string written = sarc::Write(sarc::Parse(original), sarc::ActorPackAlignment);
        if (written == original) {
            return true;
        }
        const size_t offset = std::ranges::mismatch(written, original).in1 - written.begin();
        std::printf("%s: %zu bytes written for %zu read, first difference at %#zx\n", path.c_str(), written.size(),
                    original.size(), offset);
    } catch (const std::exception& e) {
        std::printf("%s: %s\n", path.c_str(), e.what());
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <game.nup>\n", argv[0]);
        return 2;
    }
    GameFile game(argv[1]);
    const std::string dictionary = Dictionary(game, "pack.zsdic");
    int files = 0;
    int failures = 0;
    for (const auto& [path, file] : game.Files()) {
        if (path.starts_with("Pack/Actor/") && path.ends_with(".pack.zs")) {
            ++files;
            failures += !RoundTrips(game, path, dictionary);
        }
    }
    std::printf("%d of %d actor packs round trip\n", files - failures, files);
    return files == ActorPacks && failures == 0 ? 0 : 1;
}
