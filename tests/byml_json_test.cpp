// A BYML file prints as the JSON kaepora-dump gives the game testing library: every node type, binary included, as
// that library reads it, and a Float that JSON cannot hold stops it.
#include "byml.h"

#include <nlohmann/json.hpp>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// Little-endian bytes, built by hand, since the patcher's writer does not write binary nodes.
struct Bytes {
    std::string data;

    template <typename T>
    size_t Add(T value) {
        const size_t at = data.size();
        data.append(reinterpret_cast<const char*>(&value), sizeof(T));
        return at;
    }

    void Put(size_t at, uint32_t value) {
        data.replace(at, sizeof(value), reinterpret_cast<const char*>(&value), sizeof(value));
    }

    void Align() {
        data.resize((data.size() + 3) / 4 * 4);
    }

    size_t Header(uint8_t type, size_t count) {
        return Add(uint32_t(type | count << 8));
    }

    size_t Table(const std::vector<std::string>& names) {
        const size_t at = Header(0xC2, names.size());
        uint32_t offset = uint32_t(4 + 4 * (names.size() + 1));
        for (const std::string& name : names) {
            Add(offset);
            offset += uint32_t(name.size() + 1);
        }
        Add(offset);
        for (const std::string& name : names) {
            data.append(name.c_str(), name.size() + 1);
        }
        Align();
        return at;
    }
};

static std::string EveryNodeType() {
    Bytes file{ .data = "YB" };
    file.Add(uint16_t(7));
    file.data.resize(16);
    file.Put(4, uint32_t(file.Table(
                    { "Array", "Binary", "Bool", "Dict", "Float", "Int", "Int64", "String", "UInt", "UInt64" })));
    file.Put(8, uint32_t(file.Table({ "text" })));
    const size_t int64 = file.Add(int64_t(-9007199254740993));
    const size_t uint64 = file.Add(std::numeric_limits<uint64_t>::max());
    const size_t binary = file.Add(uint32_t(4));
    file.data += std::string("\x00\x01\x02\xFF", 4);
    const size_t array = file.Header(0xC0, 2);
    file.data += "\xD3\xA0";
    file.Align();
    file.Add(uint32_t(7));
    file.Add(uint32_t(0));
    const size_t dict = file.Header(0xC1, 1);
    file.Add(uint32_t(2 | 0xD0 << 24));
    file.Add(uint32_t(0));
    file.Put(12, uint32_t(file.Header(0xC1, 10)));
    const std::vector<std::pair<uint8_t, uint32_t>> values = {
        { 0xC0, uint32_t(array) },
        { 0xA1, uint32_t(binary) },
        { 0xD0, 1 },
        { 0xC1, uint32_t(dict) },
        { 0xD2, std::bit_cast<uint32_t>(0.1f) },
        { 0xD1, uint32_t(-2) },
        { 0xD4, uint32_t(int64) },
        { 0xA0, 0 },
        { 0xD3, std::numeric_limits<uint32_t>::max() },
        { 0xD5, uint32_t(uint64) },
    };
    for (uint32_t key = 0; key < values.size(); ++key) {
        file.Add(key | uint32_t(values[key].first) << 24);
        file.Add(values[key].second);
    }
    return file.data;
}

static bool Stops(float value) {
    const byml::Node root{
        .type = byml::Type::Dict,
        .dict = { { .key = "Float", .value = { .type = byml::Type::Float, .scalar = std::bit_cast<uint32_t>(value) } } }
    };
    try {
        byml::Json(byml::Parse(byml::Write({ .order = std::endian::little, .root = root })).root);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("failed: %s\n", what);
            ++failures;
        }
    };
    const nlohmann::json expected = nlohmann::json::parse(R"({
        "Array": [7, "text"], "Binary": {"binary": "AAEC/w=="}, "Bool": true, "Dict": {"Bool": false},
        "Float": 0.10000000149011612, "Int": -2, "Int64": "-9007199254740993", "String": "text",
        "UInt": 4294967295, "UInt64": "18446744073709551615"})");
    try {
        const nlohmann::json printed = byml::Json(byml::Parse(EveryNodeType()).root);
        if (printed != expected) {
            std::printf("printed %s\n", printed.dump().c_str());
        }
        check(printed == expected, "every node type prints as the library reads it");
    } catch (const std::exception& e) {
        std::printf("%s\n", e.what());
        check(false, "every node type prints as the library reads it");
    }
    check(Stops(std::numeric_limits<float>::quiet_NaN()), "a NaN Float stops the print");
    check(Stops(std::numeric_limits<float>::infinity()), "an infinite Float stops the print");
    std::printf("%s\n", failures == 0 ? "all passed" : "some failed");
    return failures == 0 ? 0 : 1;
}
