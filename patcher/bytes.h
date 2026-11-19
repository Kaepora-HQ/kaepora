#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

static_assert(std::endian::native == std::endian::little, "the game's data is read in place as little endian");

// Converts a value between the machine's byte order and a file's, in either direction.
template <typename T>
T ToByteOrder(T value, std::endian order) {
    if (order == std::endian::native) {
        return value;
    }
    auto bytes = std::bit_cast<std::array<char, sizeof(T)>>(value);
    std::ranges::reverse(bytes);
    return std::bit_cast<T>(bytes);
}

template <typename T>
T ReadAt(std::string_view data, size_t offset) {
    if (offset > data.size() || data.size() - offset < sizeof(T)) {
        throw std::runtime_error("read past the end of the data");
    }
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

template <typename T>
void WriteAt(std::string& data, size_t offset, T value) {
    if (offset > data.size() || data.size() - offset < sizeof(T)) {
        throw std::runtime_error("write past the end of the data");
    }
    std::memcpy(data.data() + offset, &value, sizeof(T));
}

constexpr size_t AlignUp(size_t size, size_t alignment) {
    return (size + alignment - 1) / alignment * alignment;
}

// The hash SARC archives and MSBT message files give a name, each with its own multiplier.
inline uint32_t NameHash(std::string_view name, uint32_t multiplier) {
    uint32_t hash = 0;
    for (const char byte : name) {
        hash = hash * multiplier + uint8_t(byte);
    }
    return hash;
}

// The zero-terminated string that starts at the offset.
inline std::string_view StringAt(std::string_view data, size_t offset) {
    const size_t end = data.find('\0', offset);
    if (end == std::string_view::npos) {
        throw std::runtime_error("string runs past the end of the data");
    }
    return data.substr(offset, end - offset);
}
