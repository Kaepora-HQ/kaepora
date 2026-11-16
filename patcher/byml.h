// Little-endian BYML version 7, with the node types the game's placement files use.
#pragma once

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
    UInt64 = 0xD5,
};

struct Entry;

// The type decides which member holds the value. Bool, Int, Float, UInt and UInt64 keep their raw bits in scalar.
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

Node Parse(std::string_view data);

// Lays the file out as the game's own files are: sorted key and string tables, then the 64-bit values in order of
// first use, then the containers depth first, each distinct value and container written once.
std::string Write(const Node& root);

} // namespace byml
