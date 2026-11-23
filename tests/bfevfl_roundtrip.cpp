// Every flowchart in the game, parsed and written back, gives the bytes it was read from. The other event flows are
// timelines, which the patcher refuses.
#include "bfevfl.h"
#include "bytes.h"
#include "compression.h"
#include "game_file.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>

// Event flows in the game's RomFS, and how many are timelines: a timeline's file holds no flowchart in the count at
// 0x20 of its header.
constexpr int EventFlows = 914;
constexpr int Timelines = 261;

static bool RoundTrips(const std::string& path, const std::string& original) {
    try {
        const std::string written = bfevfl::Write(bfevfl::Parse(original));
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
    const std::string dictionary = Dictionary(game, "zs.zsdic");
    int files = 0;
    int timelines = 0;
    int failures = 0;
    for (const auto& [path, file] : game.Files()) {
        if (!path.starts_with("Event/EventFlow/") || !path.ends_with(".bfevfl.zs")) {
            continue;
        }
        ++files;
        const std::string data = Decompress(game.Read(path), dictionary);
        if (ReadAt<uint16_t>(data, 0x20) == 0) {
            ++timelines;
        } else {
            failures += !RoundTrips(path, data);
        }
    }
    std::printf("%d of %d flowcharts round trip; %d timelines\n", files - timelines - failures, files - timelines,
                timelines);
    return files == EventFlows && timelines == Timelines && failures == 0 ? 0 : 1;
}
