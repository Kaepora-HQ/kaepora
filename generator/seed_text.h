#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

// Seed text ignores case: owl and OWL are one seed, OWL. A seed code ignores case the same way.
inline std::string UpperCase(std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper;
}
