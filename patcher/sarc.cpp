#include "sarc.h"

#include "bytes.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace sarc {

namespace {

// The header: "SARC", its size, the byte order mark, the archive's size, the offset of the files' data, and the
// version. The SFAT table follows: "SFAT", its header's size, the file count and the hash multiplier, then a 16-byte
// node for each file. Then "SFNT", its header's size, and the names, each ending in a zero byte and padded to 4.
constexpr std::string_view Magic = "SARC";
constexpr uint16_t HeaderSize = 0x14;
constexpr uint16_t LittleEndian = 0xFEFF;
constexpr size_t ArchiveSize = 0x08;
constexpr size_t DataOffset = 0x0C;
constexpr uint16_t Version = 0x0100;
constexpr std::string_view TableMagic = "SFAT";
constexpr uint16_t TableHeaderSize = 0x0C;
constexpr uint32_t HashMultiplier = 0x65;
constexpr size_t Nodes = 0x20;
constexpr size_t NodeSize = 0x10;
constexpr std::string_view NamesMagic = "SFNT";
constexpr uint16_t NamesHeaderSize = 0x08;
constexpr size_t NameAlignment = 4;
constexpr size_t DataAlignment = 8;

// A node holds its name's hash, then this flag with its name's offset in 4-byte steps, then the start and end of its
// data from the data offset.
constexpr uint32_t HasName = 0x01000000;

} // namespace

Files Parse(std::string_view data) {
    if (!data.starts_with(Magic) || ReadAt<uint16_t>(data, 0x04) != HeaderSize ||
        ReadAt<uint16_t>(data, 0x06) != LittleEndian || ReadAt<uint32_t>(data, ArchiveSize) != data.size() ||
        data.substr(HeaderSize, 4) != TableMagic || ReadAt<uint16_t>(data, HeaderSize + 4) != TableHeaderSize) {
        throw std::runtime_error("not a little-endian SARC archive");
    }
    const uint16_t count = ReadAt<uint16_t>(data, HeaderSize + 6);
    const size_t namesHeader = Nodes + NodeSize * count;
    if (data.substr(namesHeader, 4) != NamesMagic) {
        throw std::runtime_error("the archive's name table is not after its nodes");
    }
    const size_t names = namesHeader + NamesHeaderSize;
    const uint32_t files = ReadAt<uint32_t>(data, DataOffset);
    Files out;
    for (size_t node = Nodes; node < namesHeader; node += NodeSize) {
        const uint32_t name = ReadAt<uint32_t>(data, node + 4);
        const uint32_t start = ReadAt<uint32_t>(data, node + 8);
        const uint32_t end = ReadAt<uint32_t>(data, node + 12);
        if ((name & ~0xFFFFFFu) != HasName) {
            throw std::runtime_error("a file in the archive has no name");
        }
        if (start > end || files + uint64_t(end) > data.size()) {
            throw std::runtime_error("a file's data runs past the end of the archive");
        }
        out.emplace(StringAt(data, names + (name & 0xFFFFFF) * NameAlignment), data.substr(files + start, end - start));
    }
    return out;
}

std::string Write(const Files& files, size_t alignment) {
    std::vector<std::pair<uint32_t, const Files::value_type*>> nodes;
    for (const auto& file : files) {
        nodes.emplace_back(NameHash(file.first, HashMultiplier), &file);
    }
    std::ranges::sort(nodes, {}, [](const auto& node) { return node.first; });

    std::string names;
    for (const auto& [hash, file] : nodes) {
        names += file->first;
        names.resize(AlignUp(names.size() + 1, NameAlignment), '\0');
    }
    const size_t namesStart = Nodes + NodeSize * nodes.size() + NamesHeaderSize;
    const size_t dataStart = AlignUp(namesStart + names.size(), DataAlignment);
    std::string archive(dataStart, '\0');
    archive.replace(0, Magic.size(), Magic);
    WriteAt(archive, 0x04, HeaderSize);
    WriteAt(archive, 0x06, LittleEndian);
    WriteAt(archive, DataOffset, uint32_t(dataStart));
    WriteAt(archive, 0x10, Version);
    archive.replace(HeaderSize, TableMagic.size(), TableMagic);
    WriteAt(archive, HeaderSize + 4, TableHeaderSize);
    WriteAt(archive, HeaderSize + 6, uint16_t(nodes.size()));
    WriteAt(archive, HeaderSize + 8, HashMultiplier);
    archive.replace(namesStart - NamesHeaderSize, NamesMagic.size(), NamesMagic);
    WriteAt(archive, namesStart - NamesHeaderSize + 4, NamesHeaderSize);
    archive.replace(namesStart, names.size(), names);

    size_t name = 0;
    size_t node = Nodes;
    for (const auto& [hash, file] : nodes) {
        archive.resize(dataStart + AlignUp(archive.size() - dataStart, alignment), '\0');
        const size_t start = archive.size() - dataStart;
        WriteAt(archive, node, hash);
        WriteAt(archive, node + 4, uint32_t(HasName | name / NameAlignment));
        WriteAt(archive, node + 8, uint32_t(start));
        WriteAt(archive, node + 12, uint32_t(start + file->second.size()));
        archive += file->second;
        name += AlignUp(file->first.size() + 1, NameAlignment);
        node += NodeSize;
    }
    WriteAt(archive, ArchiveSize, uint32_t(archive.size()));
    return archive;
}

} // namespace sarc
