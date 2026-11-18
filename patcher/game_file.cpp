#include "game_file.h"

#include "bytes.h"
#include "compression.h"

#include <stdexcept>
#include <string_view>

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
                                             .codec = ReadAt<uint8_t>(data, file + 0x40) };
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
}

std::string GameFile::Read(const std::string& path) {
    const auto file = files.find(path);
    if (file == files.end()) {
        throw std::runtime_error("the game has no such file");
    }
    if (file->second.codec != RawCodec) {
        throw std::runtime_error("the container stores this file compressed, which this reader does not read");
    }
    return ReadBytes(file->second.offset, file->second.size);
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

std::string Dictionary(GameFile& game, std::string_view name) {
    const std::string archive = Decompress(game.Read(std::string(DictionaryPack)));
    // A SARC header, then the SFAT node table, then the SFNT name table with names at 4-byte steps.
    if (!archive.starts_with("SARC")) {
        throw std::runtime_error(std::string(DictionaryPack) + ": not a SARC archive");
    }
    const uint16_t headerSize = ReadAt<uint16_t>(archive, 0x04);
    const uint32_t dataOffset = ReadAt<uint32_t>(archive, 0x0C);
    const uint16_t count = ReadAt<uint16_t>(archive, headerSize + 0x06);
    const size_t nodes = headerSize + 0x0C;
    const size_t names = nodes + count * 0x10 + 0x08;
    for (size_t node = nodes; node < nodes + count * 0x10; node += 0x10) {
        if (StringAt(archive, names + (ReadAt<uint32_t>(archive, node + 0x04) & 0xFFFF) * 4) == name) {
            const uint32_t start = ReadAt<uint32_t>(archive, node + 0x08);
            const uint32_t end = ReadAt<uint32_t>(archive, node + 0x0C);
            return archive.substr(dataOffset + start, end - start);
        }
    }
    throw std::runtime_error(std::string(DictionaryPack) + " has no " + std::string(name));
}
