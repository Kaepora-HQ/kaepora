#pragma once

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <vector>

// A table of remake/ built into the program: each row's fields, after the header given. Throws when the header is
// another, the last line has no newline, or a row has another number of fields.
inline std::vector<std::vector<std::string_view>> ReadTable(std::string_view name, std::string_view table,
                                                            std::string_view header) {
    if (!table.starts_with(std::format("{}\n", header)) || !table.ends_with('\n')) {
        throw std::runtime_error(
            std::format("{}: the header is not \"{}\", or the last line has no newline", name, header));
    }
    const size_t fields = std::ranges::count(header, '\t') + 1;
    std::vector<std::vector<std::string_view>> rows;
    for (size_t start = header.size() + 1; start < table.size(); start = table.find('\n', start) + 1) {
        std::vector<std::string_view>& row = rows.emplace_back();
        for (const auto field : std::views::split(table.substr(start, table.find('\n', start) - start), '\t')) {
            row.emplace_back(field.begin(), field.end());
        }
        if (row.size() != fields) {
            throw std::runtime_error(std::format("{} line {}: wrong number of fields", name, rows.size() + 1));
        }
    }
    return rows;
}
