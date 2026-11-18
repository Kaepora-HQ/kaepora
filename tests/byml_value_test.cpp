// A UInt at the root of a BYML file changes in place, in either byte order, and only from the value the caller expects.
#include "byml.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>

static bool Throws(std::string data, std::string_view key, uint32_t from) {
    try {
        byml::SetRootUInt(data, key, from, 0);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static uint64_t Value(const byml::Node& root, std::string_view key) {
    for (const byml::Entry& entry : root.dict) {
        if (entry.key == key) {
            return entry.value.scalar;
        }
    }
    return ~0ull;
}

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("failed: %s\n", what);
            ++failures;
        }
    };
    for (const std::endian order : { std::endian::little, std::endian::big }) {
        const byml::Node root{ .type = byml::Type::Dict,
                               .dict = { { .key = "Grade", .value = { .type = byml::Type::UInt, .scalar = 7 } },
                                         { .key = "Kind", .value = { .type = byml::Type::UInt, .scalar = 7 } },
                                         { .key = "Name", .value = { .type = byml::Type::String, .string = "a" } } } };
        const std::string file = byml::Write({ .order = order, .root = root });
        std::string changed = file;
        byml::SetRootUInt(changed, "Grade", 7, 3);
        const byml::Document read = byml::Parse(changed);
        check(changed.size() == file.size(), "the file keeps its size");
        check(Value(read.root, "Grade") == 3, "the value changes");
        check(Value(read.root, "Kind") == 7, "a value under another key stays");
        check(Throws(file, "Grade", 8), "a value other than the one expected is refused");
        check(Throws(file, "Name", 7), "a value that is not a UInt is refused");
        check(Throws(file, "Missing", 7), "a key the root does not have is refused");
    }
    std::printf("%s\n", failures == 0 ? "all passed" : "some failed");
    return failures == 0 ? 0 : 1;
}
