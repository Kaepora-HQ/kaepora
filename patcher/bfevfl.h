// BFEVFL version 4, the game's event flows, kept as the bytes they were read from with the place of every pointer
// field.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bfevfl {

struct Flow {
    std::string data;               // the file up to the end of its string pool, where its relocation table's data ends
    std::vector<uint32_t> pointers; // the offset of every pointer field the relocation table lists, in order
};

Flow Parse(std::string_view data);

// The file with its relocation table, laid out as the game's own files are.
std::string Write(const Flow& flow);

} // namespace bfevfl
