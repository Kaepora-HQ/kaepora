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

// MurmurHash3 x86_32 with seed 0. The game data list keys each value by this hash of its name.
inline uint32_t Murmur3(std::string_view name) {
    const auto mix = [](uint32_t k) { return std::rotl(k * 0xCC9E2D51, 15) * 0x1B873593; };
    const size_t blocks = name.size() / 4 * 4;
    uint32_t hash = 0;
    for (size_t i = 0; i < blocks; i += 4) {
        hash = std::rotl(hash ^ mix(ReadAt<uint32_t>(name, i)), 13) * 5 + 0xE6546B64;
    }
    uint32_t tail = 0;
    for (size_t i = blocks; i < name.size(); ++i) {
        tail |= uint32_t(uint8_t(name[i])) << 8 * (i - blocks);
    }
    hash ^= mix(tail) ^ uint32_t(name.size());
    hash = (hash ^ hash >> 16) * 0x85EBCA6B;
    hash = (hash ^ hash >> 13) * 0xC2B2AE35;
    return hash ^ hash >> 16;
}

// The zero-terminated string that starts at the offset.
inline std::string_view StringAt(std::string_view data, size_t offset) {
    const size_t end = data.find('\0', offset);
    if (end == std::string_view::npos) {
        throw std::runtime_error("string runs past the end of the data");
    }
    return data.substr(offset, end - offset);
}
