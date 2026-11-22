// Every text pack in the game, and every message file in it, parsed and written back, gives the bytes it was read
// from, and every text, turned into tags and back, gives its own.
#include "compression.h"
#include "game_file.h"
#include "msbt.h"
#include "sarc.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

constexpr int Languages = 16;
constexpr int Packs = Languages * 2; // a base and an update version of each
constexpr int MessageFiles = Packs * 540;
constexpr int Messages = Packs * 21138;

static void Report(const std::string& name, const std::string& written, const std::string& original) {
    const size_t offset = std::ranges::mismatch(written, original).in1 - written.begin();
    std::printf("%s: %zu bytes written for %zu read, first difference at %#zx\n", name.c_str(), written.size(),
                original.size(), offset);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <game.nup>\n", argv[0]);
        return 2;
    }
    GameFile game(argv[1]);
    const std::string dictionary = Dictionary(game, "zs.zsdic");
    int packs = 0;
    int files = 0;
    int messages = 0;
    int failures = 0;
    for (const auto& [path, file] : game.Files()) {
        if (!path.starts_with("Mals/") || !path.ends_with(".sarc.zs")) {
            continue;
        }
        ++packs;
        try {
            const std::string original = Decompress(game.Read(path), dictionary);
            const sarc::Files members = sarc::Parse(original);
            for (const auto& [name, data] : members) {
                ++files;
                const std::vector<msbt::Message> parsed = msbt::Parse(data);
                messages += int(parsed.size());
                for (const auto& [label, text] : parsed) {
                    if (const std::string encoded = msbt::Encode(msbt::Decode(text)); encoded != text) {
                        Report(path + ":" + name + ":" + label, encoded, text);
                        ++failures;
                    }
                }
                if (const std::string written = msbt::Write(parsed); written != data) {
                    Report(path + ":" + name, written, data);
                    ++failures;
                }
            }
            if (const std::string written = sarc::Write(members, sarc::TextPackAlignment); written != original) {
                Report(path, written, original);
                ++failures;
            }
        } catch (const std::exception& e) {
            std::printf("%s: %s\n", path.c_str(), e.what());
            ++failures;
        }
    }
    std::printf("%d text packs, %d message files, %d messages; %d failed to round trip\n", packs, files, messages,
                failures);
    return packs == Packs && files == MessageFiles && messages == Messages && failures == 0 ? 0 : 1;
}
