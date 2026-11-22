#include "size_table.h"

#include "bytes.h"
#include "compression.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <stdexcept>

namespace {

// "RESTBL", u32 version, u32 name length, u32 entry count, u32 name entry count, then the entries: the CRC-32 of a
// path without its ".zs", and a u32 size. Entries by name follow for paths whose CRC-32s collide, and those CRC-32s
// have no entry.
constexpr std::string_view SizeTableMagic = "RESTBL";
constexpr uint32_t SizeTableVersion = 1;
constexpr size_t SizeTableHeaderSize = 0x16;
constexpr size_t SizeTableEntrySize = 8;

// CRC-32 as zlib computes it.
uint32_t Crc32(std::string_view data) {
    uint32_t crc = 0xFFFFFFFF;
    for (const char byte : data) {
        crc ^= uint8_t(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
        }
    }
    return ~crc;
}

} // namespace

std::string ReadSizeTable(std::string_view frame) {
    try {
        std::string table = Decompress(frame);
        if (!table.starts_with(SizeTableMagic) || ReadAt<uint32_t>(table, 0x06) != SizeTableVersion) {
            throw std::runtime_error(std::format("not a version {} size table", SizeTableVersion));
        }
        return table;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", SizeTablePath, e.what()));
    }
}

SizeEntries::SizeEntries(std::string_view table) {
    const size_t end = SizeTableHeaderSize + ReadAt<uint32_t>(table, 0x0E) * SizeTableEntrySize;
    for (size_t entry = SizeTableHeaderSize; entry < end; entry += SizeTableEntrySize) {
        byCrc.emplace(ReadAt<uint32_t>(table, entry), ReadAt<uint32_t>(table, entry + 4));
    }
    const size_t nameSize = ReadAt<uint32_t>(table, 0x0A);
    for (size_t entry = end; entry < table.size(); entry += nameSize + 4) {
        const std::string_view named = table.substr(entry, nameSize);
        byName.emplace(named.substr(0, named.find('\0')), ReadAt<uint32_t>(table, entry + nameSize));
    }
}

std::optional<uint32_t> SizeEntries::Find(std::string_view path) const {
    const std::string_view name = path.ends_with(".zs") ? path.substr(0, path.size() - std::strlen(".zs")) : path;
    if (const auto named = byName.find(name); named != byName.end()) {
        return named->second;
    }
    if (const auto entry = byCrc.find(Crc32(name)); entry != byCrc.end()) {
        return entry->second;
    }
    return std::nullopt;
}

bool RaiseSize(std::string& table, std::string_view path, size_t size, size_t overhead) {
    const uint32_t key = Crc32(path.substr(0, path.size() - std::strlen(".zs")));
    const size_t end = SizeTableHeaderSize + ReadAt<uint32_t>(table, 0x0E) * SizeTableEntrySize;
    for (size_t entry = SizeTableHeaderSize; entry < end; entry += SizeTableEntrySize) {
        if (ReadAt<uint32_t>(table, entry) == key) {
            const uint32_t value = uint32_t(AlignUp(size, SizeAlignment) + overhead);
            if (ReadAt<uint32_t>(table, entry + 4) >= value) {
                return false;
            }
            WriteAt(table, entry + 4, value);
            return true;
        }
    }
    throw std::runtime_error(std::format("{}: the size table has no entry for this file", path));
}

void AddSize(std::string& table, std::string_view path, size_t size, size_t overhead) {
    const std::string_view name = path.substr(0, path.size() - std::strlen(".zs"));
    const uint32_t key = Crc32(name);
    const uint32_t count = ReadAt<uint32_t>(table, 0x0E);
    const size_t end = SizeTableHeaderSize + count * SizeTableEntrySize;
    const size_t nameSize = ReadAt<uint32_t>(table, 0x0A);
    for (size_t entry = end; entry < table.size(); entry += nameSize + 4) {
        const std::string_view named = std::string_view(table).substr(entry, nameSize);
        if (Crc32(named.substr(0, named.find('\0'))) == key) {
            throw std::runtime_error(std::format("{}: the CRC-32 of this file's path collides with another's", path));
        }
    }
    size_t entry = SizeTableHeaderSize;
    while (entry < end && ReadAt<uint32_t>(table, entry) < key) {
        entry += SizeTableEntrySize;
    }
    if (entry < end && ReadAt<uint32_t>(table, entry) == key) {
        throw std::runtime_error(std::format("{}: the size table already has an entry for this file", path));
    }
    std::string added(SizeTableEntrySize, '\0');
    WriteAt(added, 0, key);
    WriteAt(added, 4, uint32_t(AlignUp(size, SizeAlignment) + overhead));
    table.insert(entry, added);
    WriteAt(table, 0x0E, count + 1);
}
