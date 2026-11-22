// BYML version 7 in either byte order, with the node types the game's placement files, game data list and tables use.
#pragma once

#include <nlohmann/json_fwd.hpp>

#include <bit>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace byml {

enum class Type : uint8_t {
    String = 0xA0,
    Binary = 0xA1,
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
// scalar, and Binary keeps its bytes in string.
struct Node {
    Type type;
    uint64_t scalar = 0;
    std::string string;
    std::vector<Node> array;
    std::vector<Entry> dict; // in key order

    bool operator==(const Node&) const = default;
    std::strong_ordering operator<=>(const Node&) const;
};

struct Entry {
    std::string key;
    Node value;

    std::strong_ordering operator<=>(const Entry&) const = default;
};

// Member by member, as a defaulted <=> would. Clang cannot default it: inside Node, Entry is incomplete, and defaulted
// after Entry, the comparison recurses into itself through std::vector<Node>.
inline std::strong_ordering Node::operator<=>(const Node& other) const {
    return std::tie(type, scalar, string, array, dict) <=>
           std::tie(other.type, other.scalar, other.string, other.array, other.dict);
}

// A Float node's value.
inline float FloatOf(const Node& node) {
    return std::bit_cast<float>(uint32_t(node.scalar));
}

struct Document {
    std::endian order;
    Node root;
};

Document Parse(std::string_view data);

// The tree as JSON: Bool as a boolean; Int, UInt and Float as numbers, a Float exactly; Int64 and UInt64 as decimal
// text, since a JSON number holds an integer exactly only up to 2^53; Binary as {"binary": "<base64>"}. Throws on a NaN
// or infinite Float, which JSON cannot hold.
nlohmann::json Json(const Node& node);

Node* Find(Node& dict, std::string_view key);

// The dict's value for the key, added in key order as the game's dicts are when the dict has none.
Node& FindOrAdd(Node& dict, const std::string& key, Type type);

// Lays the file out as the game's own files are: sorted key and string tables, then the 64-bit values in order of
// first use, then the containers depth first, each distinct container and each distinct 64-bit value of a type
// written once.
std::string Write(const Document& document);

// Sets the UInt under the key at the root of the file, where its dict entry holds it, so every other byte stays.
// Throws when the root has no UInt under the key, or the UInt is not `from`.
void SetRootUInt(std::string& data, std::string_view key, uint32_t from, uint32_t to);

} // namespace byml
