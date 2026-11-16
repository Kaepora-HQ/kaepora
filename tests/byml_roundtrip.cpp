// Every placement file in the game and its game data list, parsed and written back, give the bytes they were read
// from.
#include "byml.h"
#include "compression.h"
#include "romfs.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>

// Placement files in the game's RomFS.
constexpr int PlacementFiles = 160;

constexpr const char* GameDataList = "GameData/GameDataList.Product.110.byml.zs";

static bool RoundTrips(Romfs& romfs, const std::string& path, const std::string& dictionary) {
    try {
        const std::string original = Decompress(romfs.Read(path), dictionary);
        const std::string written = byml::Write(byml::Parse(original));
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
    Romfs romfs(argv[1]);
    const std::string dictionary = Dictionary(romfs, "bcett.byml.zsdic");
    int files = 0;
    int failures = 0;
    for (const auto& [path, file] : romfs.Files()) {
        if (path.starts_with("Banc/") && path.ends_with(".bcett.byml.zs")) {
            ++files;
            failures += !RoundTrips(romfs, path, dictionary);
        }
    }
    std::printf("%d of %d placement files round trip\n", files - failures, files);
    const bool list = RoundTrips(romfs, GameDataList, Dictionary(romfs, "zs.zsdic"));
    std::printf("%s %s\n", GameDataList, list ? "round trips" : "does not round trip");
    return files == PlacementFiles && failures == 0 && list ? 0 : 1;
}
