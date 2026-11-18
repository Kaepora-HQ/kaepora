// The game file, a Lichii .nup container, and the game's RomFS in it.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

class GameFile {
public:
    struct File {
        uint64_t offset;
        uint64_t size;
        uint8_t codec;
    };

    explicit GameFile(const std::filesystem::path& nup);

    // Throws if the RomFS has no file at this path.
    std::string Read(const std::string& path);

    const std::map<std::string, File>& Files() const {
        return files;
    }

private:
    std::string ReadBytes(uint64_t offset, uint64_t size);

    std::ifstream nup;
    std::map<std::string, File> files;
};

// A zstd dictionary in the game's dictionary pack, by its name there.
std::string Dictionary(GameFile& game, std::string_view name);
