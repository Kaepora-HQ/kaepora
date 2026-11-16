// BYML version 7 in either byte order, with the node types the game's placement files and game data list use.
#pragma once

#include <bit>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace byml {

enum class Type : uint8_t {
    String = 0xA0,
    Array = 0xC0,
    Dict = 0xC1,
    Bool = 0xD0,
    Int = 0xD1,
    Float = 0xD2,
    UInt = 0xD3,
    Int64 = 0xD4,
    UInt64 = 0xD5,
};

struct Entry;

// The type decides which member holds the value. Bool, Int, Float, UInt, Int64 and UInt64 keep their raw bits in
// scalar.
struct Node {
    Type type;
    uint64_t scalar = 0;
    std::string string;
    std::vector<Node> array;
    std::vector<Entry> dict; // in key order

    std::strong_ordering operator<=>(const Node&) const = default;
};

struct Entry {
    std::string key;
    Node value;

    std::strong_ordering operator<=>(const Entry&) const = default;
};

struct Document {
    std::endian order;
    Node root;
};

Document Parse(std::string_view data);

// Lays the file out as the game's own files are: sorted key and string tables, then the 64-bit values in order of
// first use, then the containers depth first, each distinct container and each distinct 64-bit value of a type
// written once.
std::string Write(const Document& document);

} // namespace byml
