// Bounds-checked reads of little-endian values from the game's data.
#pragma once

#include <bit>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>

static_assert(std::endian::native == std::endian::little, "the game's data is read in place as little endian");

template <typename T>
T ReadAt(std::string_view data, size_t offset) {
    if (offset > data.size() || data.size() - offset < sizeof(T)) {
        throw std::runtime_error("read past the end of the data");
    }
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

// The zero-terminated string that starts at the offset.
inline std::string_view StringAt(std::string_view data, size_t offset) {
    const size_t end = offset < data.size() ? data.find('\0', offset) : std::string_view::npos;
    if (end == std::string_view::npos) {
        throw std::runtime_error("string runs past the end of the data");
    }
    return data.substr(offset, end - offset);
}
