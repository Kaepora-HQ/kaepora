#pragma once

#include "game_values.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

struct CellView {
    std::string block;
    int row;
    int column;
    std::string name;
    std::string picture;
    bool lit;
    std::optional<int> count;
    std::optional<int> total;
    std::string tint;
    std::string label;

    bool operator==(const CellView&) const = default;
};

inline const SavedValue MenuState{ "0x578d41e1", ValueType::Enum };
// Empty from the boot until a game's first scene, while the menu state already reads None.
inline const SavedValue Scene{ "Sequence_CurrentBanc", ValueType::String64 };

class Tracker {
public:
    Tracker();

    const std::vector<SavedValue>& values() const;
    // Every picture a cell can show, by its name, once each.
    std::vector<std::string> pictures() const;
    // Takes one read of values(), in their order. Returns whether what cells() gives changed.
    bool take(std::span<const quint32> read);
    // Whether a game is in play, as of the last read.
    bool inPlay() const;
    std::vector<CellView> cells() const;

private:
    struct Stage {
        std::string picture;
        size_t value;
        quint32 when;
    };
    struct Count {
        size_t value;
        int per;
    };
    struct Cell {
        CellView view;
        std::vector<Stage> stages;
        std::optional<Count> count;
    };

    std::vector<SavedValue> saved;
    std::vector<Cell> table;
    std::optional<std::vector<quint32>> playing;
};
