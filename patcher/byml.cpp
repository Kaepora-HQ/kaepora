#include "byml.h"

#include "bytes.h"

#include <format>
#include <map>
#include <stdexcept>
#include <utility>

namespace byml {

namespace {

constexpr uint16_t Version = 7;
constexpr uint8_t StringTable = 0xC2;
constexpr size_t HeaderSize = 0x10;

std::string_view Magic(std::endian order) {
    return order == std::endian::little ? "YB" : "BY";
}

struct Reader {
    std::string_view data;
    std::endian order;
    std::vector<std::string_view> keys;
    std::vector<std::string_view> strings;

    template <typename T>
    T Read(size_t offset) const {
        return ToByteOrder(ReadAt<T>(data, offset), order);
    }

    // A container's count and a dict entry's key index are 24-bit numbers.
    uint32_t Read24(size_t offset) const {
        uint32_t value = 0;
        for (size_t i = 0; i < 3; ++i) {
            value |= uint32_t(ReadAt<uint8_t>(data, offset + i)) << 8 * (order == std::endian::little ? i : 2 - i);
        }
        return value;
    }

    std::vector<std::string_view> Table(uint32_t offset) const {
        if (ReadAt<uint8_t>(data, offset) != StringTable) {
            throw std::runtime_error("BYML string table expected");
        }
        std::vector<std::string_view> table(Read24(offset + 1));
        for (size_t i = 0; i < table.size(); ++i) {
            table[i] = StringAt(data, offset + Read<uint32_t>(offset + 4 + 4 * i));
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
            case Type::Int64:
            case Type::UInt64:
                return { .type = type, .scalar = Read<uint64_t>(value) };
            case Type::Array:
            case Type::Dict:
                return Container(value);
        }
        throw std::runtime_error(std::format("unsupported BYML node type {:#x}", uint8_t(type)));
    }

    Node Container(uint32_t offset) const {
        const uint32_t count = Read24(offset + 1);
        Node node{ .type = Type(ReadAt<uint8_t>(data, offset)) };
        if (node.type == Type::Array) {
            const size_t values = offset + 4 + AlignUp(count, 4);
            for (uint32_t i = 0; i < count; ++i) {
                node.array.push_back(
                    Value(Type(ReadAt<uint8_t>(data, offset + 4 + i)), Read<uint32_t>(values + 4 * i)));
            }
        } else if (node.type == Type::Dict) {
            for (uint32_t i = 0; i < count; ++i) {
                const size_t entry = offset + 4 + 8 * i;
                node.dict.push_back(
                    { .key = std::string(keys.at(Read24(entry))),
                      .value = Value(Type(ReadAt<uint8_t>(data, entry + 3)), Read<uint32_t>(entry + 4)) });
            }
        } else {
            throw std::runtime_error(std::format("unsupported BYML container type {:#x}", uint8_t(node.type)));
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
    explicit Writer(std::endian order) : order(order) {
    }

    std::string Write(const Node& root) {
        Collect(root);
        out.resize(HeaderSize);
        const uint32_t keysOffset = Table(keys);
        const uint32_t stringsOffset = Table(strings);
        for (const Value64& value : longs) {
            longOffsets[value] = Offset();
            Append(value.second);
        }
        const uint32_t rootOffset = Container(root);

        out.replace(0, 2, Magic(order));
        Put<uint16_t>(2, Version);
        Put<uint32_t>(4, keysOffset);
        Put<uint32_t>(8, stringsOffset);
        Put<uint32_t>(12, rootOffset);
        return out;
    }

private:
    // The game's files store a 64-bit value once for each type that uses it.
    using Value64 = std::pair<Type, uint64_t>;

    // Gathers the table entries and the 64-bit values in depth-first order.
    void Collect(const Node& node) {
        if (node.type == Type::String) {
            strings.emplace(node.string, 0);
        } else if ((node.type == Type::Int64 || node.type == Type::UInt64) &&
                   longOffsets.emplace(Value64(node.type, node.scalar), 0).second) {
            longs.emplace_back(node.type, node.scalar);
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
        AppendHeader(StringTable, table.size());
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
        out.resize(AlignUp(out.size(), 4));
        return offset;
    }

    uint32_t Container(const Node& node) {
        const auto [written, isNew] = offsets.try_emplace(&node, Offset());
        if (!isNew) {
            return written->second;
        }
        if (node.type == Type::Array) {
            AppendHeader(uint8_t(Type::Array), node.array.size());
            for (const Node& item : node.array) {
                out += char(item.type);
            }
            out.resize(AlignUp(out.size(), 4));
            const size_t values = out.size();
            out.resize(values + 4 * node.array.size());
            for (size_t i = 0; i < node.array.size(); ++i) {
                Put(values + 4 * i, Value(node.array[i]));
            }
        } else {
            AppendHeader(uint8_t(Type::Dict), node.dict.size());
            const size_t entries = out.size();
            out.resize(entries + 8 * node.dict.size());
            for (size_t i = 0; i < node.dict.size(); ++i) {
                const auto& [key, value] = node.dict[i];
                Put24(entries + 8 * i, keys.at(key));
                out[entries + 8 * i + 3] = char(value.type);
                Put(entries + 8 * i + 4, Value(value));
            }
        }
        return written->second;
    }

    uint32_t Value(const Node& node) {
        switch (node.type) {
            case Type::String:
                return strings.at(node.string);
            case Type::Int64:
            case Type::UInt64:
                return longOffsets.at({ node.type, node.scalar });
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

    // A container or string table starts with its type and its count.
    void AppendHeader(uint8_t type, size_t count) {
        out += char(type);
        out.resize(out.size() + 3);
        Put24(out.size() - 3, uint32_t(count));
    }

    void Put24(size_t offset, uint32_t value) {
        for (size_t i = 0; i < 3; ++i) {
            out[offset + i] = char(value >> 8 * (order == std::endian::little ? i : 2 - i));
        }
    }

    template <typename T>
    void Append(T value) {
        out.resize(out.size() + sizeof(T));
        Put(out.size() - sizeof(T), value);
    }

    template <typename T>
    void Put(size_t offset, T value) {
        WriteAt(out, offset, ToByteOrder(value, order));
    }

    std::endian order;
    std::map<std::string, uint32_t> keys;
    std::map<std::string, uint32_t> strings;
    std::vector<Value64> longs;
    std::map<Value64, uint32_t> longOffsets;
    std::map<const Node*, uint32_t, ContentLess> offsets;
    std::string out;
};

} // namespace

static Reader Open(std::string_view data) {
    Reader reader{ .data = data,
                   .order = data.starts_with(Magic(std::endian::big)) ? std::endian::big : std::endian::little };
    if (!data.starts_with(Magic(reader.order)) || reader.Read<uint16_t>(2) != Version) {
        throw std::runtime_error("not a BYML version 7 file");
    }
    reader.keys = reader.Table(reader.Read<uint32_t>(4));
    return reader;
}

Document Parse(std::string_view data) {
    Reader reader = Open(data);
    reader.strings = reader.Table(reader.Read<uint32_t>(8));
    return { .order = reader.order, .root = reader.Container(reader.Read<uint32_t>(12)) };
}

void SetRootUInt(std::string& data, std::string_view key, uint32_t from, uint32_t to) {
    const Reader reader = Open(data);
    const uint32_t root = reader.Read<uint32_t>(12);
    if (Type(ReadAt<uint8_t>(data, root)) != Type::Dict) {
        throw std::runtime_error("the BYML file's root is not a dict");
    }
    for (uint32_t i = 0; i < reader.Read24(root + 1); ++i) {
        const size_t entry = root + 4 + 8 * i;
        if (reader.keys.at(reader.Read24(entry)) != key) {
            continue;
        }
        if (Type(ReadAt<uint8_t>(data, entry + 3)) != Type::UInt || reader.Read<uint32_t>(entry + 4) != from) {
            throw std::runtime_error(std::format("{} at the BYML file's root is not the UInt {}", key, from));
        }
        WriteAt(data, entry + 4, ToByteOrder(to, reader.order));
        return;
    }
    throw std::runtime_error(std::format("the BYML file's root has no {}", key));
}

std::string Write(const Document& document) {
    return Writer(document.order).Write(document.root);
}

} // namespace byml
