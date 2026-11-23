#include "tracker.h"

#include "bytes.h"
#include "table.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <format>
#include <stdexcept>

static constexpr char trackerTable[] = {
#embed "../remake/tracker.tsv"
};

static int Number(const std::string& where, std::string_view text) {
    int number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc() || end != text.data() + text.size()) {
        throw std::runtime_error(std::format("{}\"{}\" is not a number", where, text));
    }
    return number;
}

static ValueType Type(const std::string& where, std::string_view type) {
    if (type == "Bool") {
        return ValueType::Bool;
    }
    if (type == "Int") {
        return ValueType::Int;
    }
    if (type == "Enum") {
        return ValueType::Enum;
    }
    throw std::runtime_error(std::format("{}the type \"{}\" is not Bool, Int or Enum", where, type));
}

Tracker::Tracker() {
    const auto indexOf = [&](const std::string& where, std::string_view name, ValueType type) {
        const auto found = std::ranges::find(saved, name, &SavedValue::name);
        if (found == saved.end()) {
            saved.push_back({ std::string(name), type });
            return saved.size() - 1;
        }
        if (found->type != type) {
            throw std::runtime_error(std::format("{}{} has another type on an earlier line", where, name));
        }
        return size_t(found - saved.begin());
    };
    size_t line = 1;
    for (const auto& fields : ReadTable("remake/tracker.tsv", { trackerTable, sizeof trackerTable },
                                        "block\trow\tcolumn\tname\tpicture\tvalue\ttype\twhen\tcount\tcount_type\tper\t"
                                        "total\ttint\tlabel")) {
        const std::string where = std::format("remake/tracker.tsv line {}: ", ++line);
        const auto& [block, row, column, name, picture, value, type, when, count, countType, per, total, tint, label] =
            std::tie(fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], fields[7], fields[8],
                     fields[9], fields[10], fields[11], fields[12], fields[13]);
        if (!name.empty()) {
            Cell& cell = table.emplace_back();
            cell.view = { .block = std::string(block),
                          .row = Number(where, row),
                          .column = Number(where, column),
                          .name = std::string(name),
                          .tint = std::string(tint),
                          .label = std::string(label) };
            if (!count.empty()) {
                if (Type(where, countType) != ValueType::Int) {
                    throw std::runtime_error(where + "a count is an Int");
                }
                cell.count = Count{ indexOf(where, count, ValueType::Int), per.empty() ? 1 : Number(where, per) };
                if (!total.empty()) {
                    cell.view.total = Number(where, total);
                }
            } else if (!total.empty()) {
                throw std::runtime_error(where + "a total is of a count");
            }
        } else if (table.empty() ||
                   !(block.empty() && row.empty() && column.empty() && count.empty() && countType.empty() &&
                     per.empty() && total.empty() && tint.empty() && label.empty())) {
            throw std::runtime_error(where +
                                     "a row with no name adds a stage to the cell above, and gives nothing else");
        }
        const ValueType kind = Type(where, type);
        if (kind == ValueType::Bool && when != "true") {
            throw std::runtime_error(where + "a Bool's stage holds when it is true");
        }
        const quint32 at = kind == ValueType::Int    ? quint32(Number(where, when))
                           : kind == ValueType::Enum ? Murmur3(when)
                                                     : 1;
        table.back().stages.push_back({ std::string(picture), indexOf(where, value, kind), at });
    }
    saved.push_back(MenuState);
    saved.push_back(Scene);
}

const std::vector<SavedValue>& Tracker::values() const {
    return saved;
}

std::vector<std::string> Tracker::pictures() const {
    std::vector<std::string> names;
    for (const Cell& cell : table) {
        for (const Stage& stage : cell.stages) {
            names.push_back(stage.picture);
        }
    }
    std::ranges::sort(names);
    names.erase(std::ranges::unique(names).begin(), names.end());
    return names;
}

bool Tracker::take(std::span<const quint32> read) {
    assert(read.size() == saved.size());
    const std::vector<CellView> shown = cells();
    const quint32 menu = read[read.size() - 2];
    if (menu == Murmur3("Title")) {
        playing.reset();
    } else if (menu == Murmur3("None") && read.back() != Murmur3("")) {
        playing.emplace(read.begin(), read.end());
    }
    return cells() != shown;
}

bool Tracker::inPlay() const {
    return playing.has_value();
}

std::vector<CellView> Tracker::cells() const {
    std::vector<CellView> views;
    for (const Cell& cell : table) {
        CellView& view = views.emplace_back(cell.view);
        view.picture = cell.stages.front().picture;
        if (!playing) {
            continue;
        }
        const std::vector<quint32>& read = *playing;
        for (const Stage& stage : cell.stages) {
            if (saved[stage.value].type == ValueType::Int ? qint32(read[stage.value]) >= qint32(stage.when)
                                                          : read[stage.value] == stage.when) {
                view.picture = stage.picture;
                view.lit = true;
            }
        }
        if (cell.count) {
            view.count = qint32(read[cell.count->value]) / cell.count->per;
        }
    }
    return views;
}
