#include "game_file.h"

#include "bytes.h"
#include "compression.h"
#include "sarc.h"

#include <algorithm>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

// Lichii's container layout, from src/core/file_sys/nup_container.cpp in Lichii.
constexpr std::string_view NupMagic = "NUP\x01";
constexpr uint32_t TocMagic = 0x01434F54;
constexpr size_t SuperblockSize = 0x80;
constexpr size_t TocHeaderSize = 0x30;
constexpr size_t PackageSize = 0x40;
constexpr size_t DirSize = 0x18;
constexpr size_t FileSize = 0x50;
constexpr uint32_t NoDir = 0xFFFFFFFF;
constexpr uint8_t BasePackage = 0;
constexpr uint8_t UpdatePackage = 1;
constexpr uint8_t RawCodec = 0;

constexpr std::string_view NsoMagic = "NSO0";
constexpr size_t BuildIdOffset = 0x40;
constexpr size_t BuildIdSize = 0x20;

// The dictionaries sit in a SARC archive inside this zstd file.
constexpr std::string_view DictionaryPack = "Pack/ZsDic.pack.zs";

struct Toc {
    std::string_view data;
    uint64_t dirCount;
    size_t dirs;
    size_t files;
    size_t children;
    size_t pool;

    std::string Name(size_t record) const {
        const uint32_t offset = ReadAt<uint32_t>(data, record);
        const uint32_t length = ReadAt<uint32_t>(data, record + 4);
        if (pool + offset + length > data.size()) {
            throw std::runtime_error("container name runs past the end of its table of contents");
        }
        return std::string(data.substr(pool + offset, length));
    }

    void Walk(uint64_t dir, const std::string& prefix, std::map<std::string, GameFile::File>& out) const {
        const size_t record = dirs + dir * DirSize;
        const uint32_t first = ReadAt<uint32_t>(data, record + 8);
        const uint32_t count = ReadAt<uint32_t>(data, record + 12);
        for (uint64_t i = first; i < uint64_t(first) + count; ++i) {
            const uint32_t child = ReadAt<uint32_t>(data, children + 4 * i);
            if (child < dirCount) {
                Walk(child, prefix + Name(dirs + child * DirSize) + "/", out);
            } else {
                const size_t file = files + (child - dirCount) * FileSize;
                out[prefix + Name(file)] = { .offset = ReadAt<uint64_t>(data, file + 0x10),
                                             .size = ReadAt<uint64_t>(data, file + 0x08),
                                             .codec = ReadAt<uint8_t>(data, file + 0x40),
                                             .sha256 = ReadAt<GameFile::Sha256>(data, file + 0x20) };
            }
        }
    }
};

} // namespace

GameFile::GameFile(const std::filesystem::path& path) : nup(path, std::ios::binary) {
    if (!nup) {
        throw std::runtime_error(path.string() + ": cannot open");
    }
    const std::string superblock = ReadBytes(0, SuperblockSize);
    if (!superblock.starts_with(NupMagic)) {
        throw std::runtime_error(path.string() + ": not a Lichii .nup container");
    }
    tableOfContentsSha256 = ReadAt<Sha256>(superblock, 0x18);
    const std::string toc = ReadBytes(ReadAt<uint64_t>(superblock, 0x08), ReadAt<uint64_t>(superblock, 0x10));
    if (ReadAt<uint32_t>(toc, 0) != TocMagic) {
        throw std::runtime_error(path.string() + ": the container has no table of contents");
    }
    const uint32_t packages = ReadAt<uint32_t>(toc, 0x04);
    const uint64_t dirCount = ReadAt<uint64_t>(toc, 0x08);
    const size_t dirs = TocHeaderSize + packages * PackageSize;
    const size_t fileRecords = dirs + dirCount * DirSize;
    const Toc reader{ .data = toc,
                      .dirCount = dirCount,
                      .dirs = dirs,
                      .files = fileRecords,
                      .children = fileRecords + ReadAt<uint64_t>(toc, 0x10) * FileSize,
                      .pool = ReadAt<uint64_t>(toc, 0x18) };

    // The game sees the base package's RomFS with the update's files laid over it.
    for (const uint8_t kind : { BasePackage, UpdatePackage }) {
        for (uint32_t i = 0; i < packages; ++i) {
            const size_t package = TocHeaderSize + i * PackageSize;
            const uint32_t romfs = ReadAt<uint32_t>(toc, package + 0x10);
            if (ReadAt<uint8_t>(toc, package + 0x18) == kind && romfs != NoDir) {
                reader.Walk(romfs, "", files);
            }
        }
    }

    // The emulator boots the program of the update with the highest version, or the base package's when that update
    // has none.
    std::optional<size_t> base;
    std::optional<size_t> update;
    for (uint32_t i = 0; i < packages; ++i) {
        const size_t package = TocHeaderSize + i * PackageSize;
        const uint8_t kind = ReadAt<uint8_t>(toc, package + 0x18);
        if (kind == BasePackage && !base) {
            base = package;
        } else if (kind == UpdatePackage &&
                   (!update || ReadAt<uint32_t>(toc, package + 0x14) > ReadAt<uint32_t>(toc, *update + 0x14))) {
            update = package;
        }
    }
    const auto exefs = [&](std::optional<size_t> package) {
        return package ? ReadAt<uint32_t>(toc, *package + 0x0C) : NoDir;
    };
    std::map<std::string, File> program;
    reader.Walk(exefs(update) != NoDir ? exefs(update) : exefs(base), "", program);
    const auto main = program.find("main");
    if (main == program.end()) {
        throw std::runtime_error(path.string() + ": the container holds no program the emulator boots");
    }
    const std::string header = ReadBytes(main->second.offset, BuildIdOffset + BuildIdSize);
    if (!header.starts_with(NsoMagic)) {
        throw std::runtime_error(path.string() + ": the container's program is not an NSO");
    }
    const std::string_view id = std::string_view(header).substr(BuildIdOffset);
    for (const char byte : id.substr(0, id.find_last_not_of('\0') + 1)) {
        programBuildId += std::format("{:02X}", uint8_t(byte));
    }
}

std::string GameFile::Read(const std::string& path) {
    return ReadStart(path, std::numeric_limits<uint64_t>::max());
}

std::string GameFile::ReadStart(const std::string& path, uint64_t limit) {
    const auto file = files.find(path);
    if (file == files.end()) {
        throw std::runtime_error("the game has no such file");
    }
    if (file->second.codec != RawCodec) {
        throw std::runtime_error("the container stores this file compressed, which this reader does not read");
    }
    return ReadBytes(file->second.offset, std::min(file->second.size, limit));
}

std::string GameFile::ReadBytes(uint64_t offset, uint64_t size) {
    std::string data(size, '\0');
    nup.seekg(offset);
    nup.read(data.data(), size);
    if (!nup) {
        throw std::runtime_error("the container ends early");
    }
    return data;
}

std::map<unsigned, std::string> Dictionaries(GameFile& game) {
    std::map<unsigned, std::string> dictionaries;
    for (auto& [name, dictionary] : sarc::Parse(Decompress(game.Read(std::string(DictionaryPack))))) {
        dictionaries.emplace(DictionaryId(dictionary), std::move(dictionary));
    }
    return dictionaries;
}

std::string Dictionary(GameFile& game, std::string_view name) {
    sarc::Files archive = sarc::Parse(Decompress(game.Read(std::string(DictionaryPack))));
    const auto dictionary = archive.find(std::string(name));
    if (dictionary == archive.end()) {
        throw std::runtime_error(std::string(DictionaryPack) + " has no " + std::string(name));
    }
    return std::move(dictionary->second);
}
