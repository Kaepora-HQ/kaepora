#include "bfevfl.h"

#include "bytes.h"

#include <stdexcept>
#include <utility>

namespace bfevfl {

namespace {

// The header. Every other offset in the file sits in a pointer field the relocation table lists, apart from the
// flowchart's offset to its string pool, and the flowchart's own offset at 0x16, which comes before any event's
// parameters.
constexpr std::string_view Magic("BFEVFL\0\0", 8);
constexpr uint16_t Version = 0x0400;
constexpr uint16_t LittleEndian = 0xFEFF;
constexpr size_t NameOffset = 0x10;       // u32, the file's name in the string pool
constexpr size_t RelocationOffset = 0x18; // u32
constexpr size_t FileSize = 0x1C;         // u32
constexpr size_t FlowchartCount = 0x20;   // u16, 0 in a timeline's file
constexpr size_t Flowcharts = 0x28;       // a pointer to the pointer to the flowchart

// The relocation table: "RELT", its own offset, its section count, then its one section's data end, the section's
// entry count, and from 0x28 each entry's base offset and mask. Bit k of the mask lists a pointer field at base + 8k.
constexpr std::string_view RelocationMagic = "RELT";
constexpr size_t DataEnd = 0x1C;
constexpr size_t EntryCount = 0x24;
constexpr size_t Entries = 0x28;
constexpr size_t PointersPerEntry = 32;

// The flowchart's string pool's offset from the flowchart's start.
constexpr size_t PoolOffset = 0x04;

enum EventType : uint8_t { Action, Switch, Fork, Join, Subflow, SubflowSwitch, Return };
constexpr uint8_t StringValue = 5;

// A string: its u16 length, its bytes, and a zero byte.
std::string_view Text(std::string_view data, uint64_t offset) {
    const std::string_view text = StringAt(data, offset + 2);
    if (text.size() != ReadAt<uint16_t>(data, offset)) {
        throw std::runtime_error("a string's length does not match its text");
    }
    return text;
}

uint64_t FindEvent(std::string_view data, uint64_t flowchart, std::string_view name) {
    // The flowchart holds its event count at 0x16 and points to its events at 0x30. An event is 0x30 bytes and
    // points to its name first.
    const uint64_t events = ReadAt<uint64_t>(data, flowchart + 0x30);
    for (uint16_t i = 0; i < ReadAt<uint16_t>(data, flowchart + 0x16); ++i) {
        const uint64_t event = events + 0x30 * i;
        if (Text(data, ReadAt<uint64_t>(data, event)) == name) {
            return event;
        }
    }
    throw std::runtime_error("no event in the flow has this name");
}

// The offset of the value of the event's parameter with this name.
uint64_t FindParam(std::string_view data, uint64_t event, std::string_view name) {
    // An event holds its type at 0x08. Actions, switches, sub-flow calls and sub-flow switches point to their
    // parameters at 0x10.
    const auto type = EventType(ReadAt<uint8_t>(data, event + 0x08));
    const bool hasParams = type == Action || type == Switch || type == Subflow || type == SubflowSwitch;
    const uint64_t params = hasParams ? ReadAt<uint64_t>(data, event + 0x10) : 0;
    if (params != 0) {
        // The parameters hold their count at 0x02, point to a DIC of their names at 0x08, and from 0x10 point to each
        // value in the DIC's order. The DIC's 16-byte nodes start at 0x08 with its tree's root, and each points to its
        // name at 0x08.
        const uint64_t names = ReadAt<uint64_t>(data, params + 0x08);
        for (uint16_t i = 0; i < ReadAt<uint16_t>(data, params + 0x02); ++i) {
            if (Text(data, ReadAt<uint64_t>(data, names + 0x08 + 0x10 * (i + 1) + 0x08)) == name) {
                return ReadAt<uint64_t>(data, params + 0x10 + 8 * i);
            }
        }
    }
    throw std::runtime_error("the event has no parameter with this name");
}

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

bool SetString(Flow& flow, std::string_view event, std::string_view param, std::string_view value) {
    if (ReadAt<uint16_t>(flow.data, FlowchartCount) == 0) {
        throw std::runtime_error("the file is a timeline, which has no events");
    }
    const uint64_t flowchart = ReadAt<uint64_t>(flow.data, ReadAt<uint64_t>(flow.data, Flowcharts));
    const uint64_t found = FindParam(flow.data, FindEvent(flow.data, flowchart, event), param);
    if (ReadAt<uint8_t>(flow.data, found) != StringValue) {
        throw std::runtime_error("the parameter is not a string");
    }
    // A string value points at 0x10 to its own text, which follows the pointer.
    const uint64_t start = ReadAt<uint64_t>(flow.data, found + 0x10);
    const std::string_view old = Text(flow.data, start);
    if (old == value) {
        return false;
    }

    // The next part of the file starts at the 8-byte boundary after the string. It and everything after it move by
    // the change in the string's rounded size, and so keep their alignment.
    const uint64_t end = AlignUp(start + 3 + old.size(), 8);
    const uint64_t newEnd = AlignUp(start + 3 + value.size(), 8);
    const auto moved = [&](uint64_t offset) { return offset < end ? offset : offset - end + newEnd; };
    for (const uint32_t field : flow.pointers) {
        WriteAt<uint64_t>(flow.data, field, moved(ReadAt<uint64_t>(flow.data, field)));
    }
    WriteAt<uint32_t>(flow.data, NameOffset, uint32_t(moved(ReadAt<uint32_t>(flow.data, NameOffset))));
    const uint64_t pool = flowchart + ReadAt<uint32_t>(flow.data, flowchart + PoolOffset);
    WriteAt<uint32_t>(flow.data, flowchart + PoolOffset, uint32_t(moved(pool) - flowchart));

    std::string text(newEnd - start, '\0');
    WriteAt<uint16_t>(text, 0, uint16_t(value.size()));
    text.replace(2, value.size(), value);
    flow.data.replace(start, end - start, text);
    for (uint32_t& field : flow.pointers) {
        field = uint32_t(moved(field));
    }
    return true;
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
