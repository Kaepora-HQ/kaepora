#include "bfevfl.h"

#include "bytes.h"

#include <stdexcept>
#include <utility>

namespace bfevfl {

namespace {

// The header.
constexpr std::string_view Magic("BFEVFL\0\0", 8);
constexpr uint16_t Version = 0x0400;
constexpr uint16_t LittleEndian = 0xFEFF;
constexpr size_t RelocationOffset = 0x18; // u32
constexpr size_t FileSize = 0x1C;         // u32

// The relocation table: "RELT", its own offset, its section count, then its one section's data end, the section's
// entry count, and from 0x28 each entry's base offset and mask. Bit k of the mask lists a pointer field at base + 8k.
constexpr std::string_view RelocationMagic = "RELT";
constexpr size_t DataEnd = 0x1C;
constexpr size_t EntryCount = 0x24;
constexpr size_t Entries = 0x28;
constexpr size_t PointersPerEntry = 32;

} // namespace

Flow Parse(std::string_view data) {
    if (!data.starts_with(Magic) || ReadAt<uint16_t>(data, 0x08) != Version ||
        ReadAt<uint16_t>(data, 0x0C) != LittleEndian) {
        throw std::runtime_error("not a little-endian BFEVFL version 4 file");
    }
    const uint32_t table = ReadAt<uint32_t>(data, RelocationOffset);
    const uint32_t end = ReadAt<uint32_t>(data, table + DataEnd);
    const uint32_t entries = ReadAt<uint32_t>(data, table + EntryCount);
    if (ReadAt<uint32_t>(data, FileSize) != data.size() || data.substr(table, 4) != RelocationMagic ||
        ReadAt<uint32_t>(data, table + 0x08) != 1 || end > table ||
        table + Entries + 8 * uint64_t(entries) != data.size()) {
        throw std::runtime_error("the relocation table is not one section at the end of the file");
    }
    Flow flow{ .data = std::string(data.substr(0, end)) };
    for (size_t entry = table + Entries; entry < data.size(); entry += 8) {
        const uint32_t base = ReadAt<uint32_t>(data, entry);
        const uint32_t mask = ReadAt<uint32_t>(data, entry + 4);
        for (uint32_t k = 0; k < PointersPerEntry; ++k) {
            if ((mask >> k & 1) != 0) {
                flow.pointers.push_back(base + 8 * k);
            }
        }
    }
    return flow;
}

std::string Write(const Flow& flow) {
    // Each entry starts at the lowest field not yet listed and lists every field in the 32 places from it. Pointer
    // fields sit on 8-byte boundaries.
    std::vector<std::pair<uint32_t, uint32_t>> entries;
    for (const uint32_t field : flow.pointers) {
        if (entries.empty() || field >= entries.back().first + 8 * PointersPerEntry) {
            entries.emplace_back(field, 0);
        }
        entries.back().second |= 1u << (field - entries.back().first) / 8;
    }
    std::string file = flow.data;
    const uint32_t table = uint32_t(AlignUp(file.size(), 8));
    file.resize(table + Entries + 8 * entries.size());
    file.replace(table, RelocationMagic.size(), RelocationMagic);
    WriteAt<uint32_t>(file, table + 0x04, table);
    WriteAt<uint32_t>(file, table + 0x08, 1);
    WriteAt<uint32_t>(file, table + DataEnd, uint32_t(flow.data.size()));
    WriteAt<uint32_t>(file, table + EntryCount, uint32_t(entries.size()));
    for (size_t i = 0; i < entries.size(); ++i) {
        WriteAt<uint32_t>(file, table + Entries + 8 * i, entries[i].first);
        WriteAt<uint32_t>(file, table + Entries + 8 * i + 4, entries[i].second);
    }
    WriteAt<uint32_t>(file, RelocationOffset, table);
    WriteAt<uint32_t>(file, FileSize, uint32_t(file.size()));
    return file;
}

} // namespace bfevfl
