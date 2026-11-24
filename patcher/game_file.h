// The game file, a Lichii .nup container, with the game's RomFS in it and the build ID of the program the
// emulator boots from it.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

class GameFile {
public:
    using Sha256 = std::array<uint8_t, 32>;

    struct File {
        uint64_t offset;
        uint64_t size;
        uint8_t codec;
        Sha256 sha256; // of the file's bytes, as the container records it
    };

    explicit GameFile(const std::filesystem::path& nup);

    // Throws if the RomFS has no file at this path.
    std::string Read(const std::string& path);

    // The file's first bytes, as many as it has up to `limit`. Throws as Read does.
    std::string ReadStart(const std::string& path, uint64_t limit);

    const std::map<std::string, File>& Files() const {
        return files;
    }

    // The build ID of the program the emulator boots, in hex, as a patch file's name gives it: the 32 bytes of the
    // program's header that hold it, without their trailing zero bytes, which the emulator puts back.
    const std::string& ProgramBuildId() const {
        return programBuildId;
    }

    // The SHA-256 of the container's table of contents, which names every file with its own SHA-256, as the
    // container's header records it. The emulator refuses a container whose table has another.
    const Sha256& TableOfContentsSha256() const {
        return tableOfContentsSha256;
    }

private:
    std::string ReadBytes(uint64_t offset, uint64_t size);

    std::ifstream nup;
    std::map<std::string, File> files;
    std::string programBuildId;
    Sha256 tableOfContentsSha256;
};

// A zstd dictionary in the game's dictionary pack, by its name there.
std::string Dictionary(GameFile& game, std::string_view name);

// Every zstd dictionary in the game's dictionary pack, by the ID a frame's header names it by.
std::map<unsigned, std::string> Dictionaries(GameFile& game);
