#include "byml.h"

#include "bytes.h"

#include <cstring>
#include <format>
#include <map>
#include <stdexcept>

namespace byml {

namespace {

constexpr std::string_view Magic = "YB";
constexpr uint16_t Version = 7;
constexpr uint8_t StringTable = 0xC2;
constexpr size_t HeaderSize = 0x10;

size_t Align4(size_t size) {
    return (size + 3) & ~size_t(3);
}

struct Reader {
    std::string_view data;
    std::vector<std::string_view> keys;
    std::vector<std::string_view> strings;

    std::vector<std::string_view> Table(uint32_t offset) const {
        const uint32_t header = ReadAt<uint32_t>(data, offset);
        if ((header & 0xFF) != StringTable) {
            throw std::runtime_error("BYML string table expected");
        }
        std::vector<std::string_view> table(header >> 8);
        for (size_t i = 0; i < table.size(); ++i) {
            table[i] = StringAt(data, offset + ReadAt<uint32_t>(data, offset + 4 + 4 * i));
        }
        return table;
    }

    Node Value(Type type, uint32_t value) const {
        switch (type) {
            case Type::String:
                return { .type = type, .string = std::string(strings.at(value)) };
            case Type::Bool:
            case Type::Int:
            case Type::Float:
            case Type::UInt:
                return { .type = type, .scalar = value };
            case Type::UInt64:
                return { .type = type, .scalar = ReadAt<uint64_t>(data, value) };
            case Type::Array:
            case Type::Dict:
                return Container(value);
        }
        throw std::runtime_error(std::format("unsupported BYML node type {:#x}", uint8_t(type)));
    }

    Node Container(uint32_t offset) const {
        const uint32_t header = ReadAt<uint32_t>(data, offset);
        const uint32_t count = header >> 8;
        Node node{ .type = Type(header & 0xFF) };
        if (node.type == Type::Array) {
            const size_t values = offset + 4 + Align4(count);
            for (uint32_t i = 0; i < count; ++i) {
                node.array.push_back(
                    Value(Type(ReadAt<uint8_t>(data, offset + 4 + i)), ReadAt<uint32_t>(data, values + 4 * i)));
            }
        } else if (node.type == Type::Dict) {
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t entry = ReadAt<uint32_t>(data, offset + 4 + 8 * i);
                node.dict.push_back({ .key = std::string(keys.at(entry & 0xFFFFFF)),
                                      .value = Value(Type(entry >> 24), ReadAt<uint32_t>(data, offset + 8 + 8 * i)) });
            }
        } else {
            throw std::runtime_error(std::format("unsupported BYML container type {:#x}", header & 0xFF));
        }
        return node;
    }
};

struct ContentLess {
    bool operator()(const Node* a, const Node* b) const {
        return *a < *b;
    }
};

class Writer {
public:
    std::string Write(const Node& root) {
        Collect(root);
        out.resize(HeaderSize);
        const uint32_t keysOffset = Table(keys);
        const uint32_t stringsOffset = Table(strings);
        for (const uint64_t value : longs) {
            longOffsets[value] = Offset();
            Append(value);
        }
        const uint32_t rootOffset = Container(root);

        out.replace(0, Magic.size(), Magic);
        Put<uint16_t>(2, Version);
        Put<uint32_t>(4, keysOffset);
        Put<uint32_t>(8, stringsOffset);
        Put<uint32_t>(12, rootOffset);
        return out;
    }

private:
    // Gathers the table entries and the 64-bit values in depth-first order.
    void Collect(const Node& node) {
        if (node.type == Type::String) {
            strings.emplace(node.string, 0);
        } else if (node.type == Type::UInt64 && longOffsets.emplace(node.scalar, 0).second) {
            longs.push_back(node.scalar);
        }
        for (const Node& item : node.array) {
            Collect(item);
        }
        for (const auto& [key, value] : node.dict) {
            keys.emplace(key, 0);
            Collect(value);
        }
    }

    // Writes a table of the sorted names and numbers each name by its place in it.
    uint32_t Table(std::map<std::string, uint32_t>& table) {
        const uint32_t offset = Offset();
        Append<uint32_t>(StringTable | uint32_t(table.size()) << 8);
        uint32_t position = uint32_t(4 + 4 * (table.size() + 1));
        uint32_t index = 0;
        for (auto& [name, number] : table) {
            number = index++;
            Append(position);
            position += uint32_t(name.size() + 1);
        }
        Append(position);
        for (const auto& [name, number] : table) {
            out.append(name.c_str(), name.size() + 1);
        }
        out.resize(Align4(out.size()));
        return offset;
    }

    uint32_t Container(const Node& node) {
        const auto [written, isNew] = offsets.try_emplace(&node, Offset());
        if (!isNew) {
            return written->second;
        }
        if (node.type == Type::Array) {
            Append<uint32_t>(uint8_t(Type::Array) | uint32_t(node.array.size()) << 8);
            for (const Node& item : node.array) {
                out += char(item.type);
            }
            out.resize(Align4(out.size()));
            const size_t values = out.size();
            out.resize(values + 4 * node.array.size());
            for (size_t i = 0; i < node.array.size(); ++i) {
                Put(values + 4 * i, Value(node.array[i]));
            }
        } else {
            Append<uint32_t>(uint8_t(Type::Dict) | uint32_t(node.dict.size()) << 8);
            const size_t entries = out.size();
            out.resize(entries + 8 * node.dict.size());
            for (size_t i = 0; i < node.dict.size(); ++i) {
                const auto& [key, value] = node.dict[i];
                Put<uint32_t>(entries + 8 * i, keys.at(key) | uint32_t(value.type) << 24);
                Put(entries + 8 * i + 4, Value(value));
            }
        }
        return written->second;
    }

    uint32_t Value(const Node& node) {
        switch (node.type) {
            case Type::String:
                return strings.at(node.string);
            case Type::UInt64:
                return longOffsets.at(node.scalar);
            case Type::Array:
            case Type::Dict:
                return Container(node);
            default:
                return uint32_t(node.scalar);
        }
    }

    uint32_t Offset() const {
        return uint32_t(out.size());
    }

    template <typename T>
    void Append(T value) {
        out.append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <typename T>
    void Put(size_t offset, T value) {
        std::memcpy(out.data() + offset, &value, sizeof(T));
    }

    std::map<std::string, uint32_t> keys;
    std::map<std::string, uint32_t> strings;
    std::vector<uint64_t> longs;
    std::map<uint64_t, uint32_t> longOffsets;
    std::map<const Node*, uint32_t, ContentLess> offsets;
    std::string out;
};

} // namespace

Node Parse(std::string_view data) {
    if (!data.starts_with(Magic) || ReadAt<uint16_t>(data, 2) != Version) {
        throw std::runtime_error("not a little-endian BYML version 7 file");
    }
    Reader reader{ .data = data };
    reader.keys = reader.Table(ReadAt<uint32_t>(data, 4));
    reader.strings = reader.Table(ReadAt<uint32_t>(data, 8));
    return reader.Container(ReadAt<uint32_t>(data, 12));
}

std::string Write(const Node& root) {
    return Writer().Write(root);
}

} // namespace byml
