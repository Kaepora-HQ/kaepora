#include "hints.h"

#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/rng.h"
#include "soh/Enhancements/randomizer/3drando/fill.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string_view>

namespace {

enum class Kind { WayOfTheHero, Foolish, Overworld, Dungeon, NamedItem, RandomItem };

struct Draw {
    std::optional<Kind> kind;
    uint32_t weight;
};

enum class Break { Space, Line, Box };

struct Piece {
    std::string text;
    bool coloured;
};

struct Word {
    std::vector<Piece> pieces;
    Break before;

    size_t Length() const {
        size_t length = 0;
        for (const Piece& piece : pieces) {
            length += piece.text.size();
        }
        return length;
    }
};

} // namespace

constexpr std::nullopt_t Junk = std::nullopt;

// SoH's balanced distribution. Its song hints are left out, as no song is shuffled.
constexpr std::array<Draw, 7> BalancedDistribution = { {
    { Kind::WayOfTheHero, 7 },
    { Kind::Foolish, 4 },
    { Kind::Overworld, 4 },
    { Kind::Dungeon, 3 },
    { Kind::NamedItem, 10 },
    { Kind::RandomItem, 12 },
    { Junk, 6 },
} };

// The most the game's own English rumours put in a stone's text box.
constexpr size_t LineLength = 65;
constexpr size_t BoxLines = 2;

// Key words are orange, as the game's own text colours them, and then the box's colour again.
constexpr std::string_view KeyColour = "{color:0}";
constexpr std::string_view BoxColour = "{color:65535}";

static Draw& WeightedDraw(std::array<Draw, BalancedDistribution.size()>& draws) {
    uint32_t total = 0;
    for (const Draw& draw : draws) {
        total += draw.weight;
    }
    uint32_t roll = Random(0, total);
    auto draw = draws.begin();
    while (roll >= draw->weight) {
        roll -= draw->weight;
        ++draw;
    }
    return *draw;
}

static bool Fits(Kind kind, RandomizerCheck loc) {
    const Rando::ItemLocation* placed = Rando::Context::GetInstance()->GetItemLocation(loc);
    switch (kind) {
        case Kind::WayOfTheHero:
            return placed->IsWothCandidate();
        case Kind::Foolish:
            return placed->IsFoolishCandidate();
        case Kind::Overworld:
            return Rando::StaticData::GetLocation(loc)->IsOverworld();
        case Kind::Dungeon:
            return Rando::StaticData::GetLocation(loc)->IsDungeon();
        case Kind::NamedItem:
            return placed->GetPlacedItem().IsMajorItem();
        case Kind::RandomItem:
            return true;
    }
    return false;
}

static std::string Sentence(Kind kind, RandomizerCheck loc, const RemakeHintTables& tables,
                            const std::map<RandomizerCheck, std::string>& items) {
    const Rando::Location* location = Rando::StaticData::GetLocation(loc);
    const std::string& place = tables.places.at(location->GetArea());
    const std::string& item = tables.itemsByRemakeName.at(items.at(loc));
    switch (kind) {
        case Kind::WayOfTheHero:
            return "They say that #" + place + "# is on #the way of the hero#.";
        case Kind::Foolish:
            return "They say that plundering #" + place + "# is #a foolish choice#.";
        case Kind::Overworld:
        case Kind::Dungeon: {
            const ChestSentence& sentence = tables.chestSentences.at(loc);
            return sentence.beforeItem + item + sentence.afterItem;
        }
        case Kind::NamedItem:
        case Kind::RandomItem:
            break;
    }
    return location->IsDungeon() ? "They say that #" + place + "# hoards #" + item + "#."
                                 : "They say that #" + item + "# can be found at #" + place + "#.";
}

// SoH's markup: # starts and ends a coloured run, & starts a new line, ^ a new box, and @ is the player's name.
static std::vector<Word> Words(std::string_view markup) {
    std::vector<Word> words;
    bool coloured = false;
    bool inWord = false;
    Break next = Break::Space;
    for (const char c : markup) {
        if (c == '#') {
            coloured = !coloured;
            continue;
        }
        if (c == ' ' || c == '&' || c == '^') {
            next = std::max(next, c == '^' ? Break::Box : c == '&' ? Break::Line : Break::Space);
            inWord = false;
            continue;
        }
        if (!inWord) {
            words.push_back({ {}, next });
            next = Break::Space;
            inWord = true;
        }
        std::vector<Piece>& pieces = words.back().pieces;
        if (pieces.empty() || pieces.back().coloured != coloured) {
            pieces.push_back({ "", coloured });
        }
        // The remake asks for no name and calls the player Link.
        pieces.back().text += c == '@' ? "Link" : std::string(1, c);
    }
    return words;
}

static std::string Layout(const std::vector<Word>& words) {
    std::string text;
    size_t length = 0;
    size_t lines = 1;
    bool coloured = false;
    for (const Word& word : words) {
        if (!text.empty()) {
            Break before = word.before;
            if (before == Break::Space && length + 1 + word.Length() > LineLength) {
                before = Break::Line;
            }
            if (before == Break::Line && lines == BoxLines) {
                before = Break::Box;
            }
            if (coloured && !word.pieces.front().coloured) {
                text += BoxColour;
                coloured = false;
            }
            if (before == Break::Space) {
                text += ' ';
                ++length;
            } else if (before == Break::Line) {
                text += '\n';
                ++lines;
                length = 0;
            } else {
                // The patcher sets the stones' own colour again after a new box.
                text += "{page}";
                lines = 1;
                length = 0;
                coloured = false;
            }
        }
        for (const Piece& piece : word.pieces) {
            if (piece.coloured != coloured) {
                text += piece.coloured ? KeyColour : BoxColour;
                coloured = piece.coloured;
            }
            text += piece.text;
        }
        length += word.Length();
    }
    if (coloured) {
        text += BoxColour;
    }
    return text;
}

static std::string Plain(const std::vector<Word>& words) {
    std::string plain;
    for (const Word& word : words) {
        plain += plain.empty() ? "" : " ";
        for (const Piece& piece : word.pieces) {
            plain += piece.text;
        }
    }
    return plain;
}

static std::vector<RandomizerCheck> StonesReachableWithout(RandomizerCheck loc,
                                                           const std::vector<RandomizerCheck>& stones) {
    auto ctx = Rando::Context::GetInstance();
    Rando::ItemLocation* hinted = ctx->GetItemLocation(loc);
    hinted->DelayItem();
    ctx->GetLogic()->Reset();
    std::vector<RandomizerCheck> reachable = ReachabilitySearch(stones);
    hinted->SaveDelayedItem();
    return reachable;
}

std::vector<StoneHint> PickHints(const RemakeHintTables& tables, const std::map<RandomizerCheck, std::string>& items) {
    auto ctx = Rando::Context::GetInstance();
    std::map<RandomizerCheck, std::string> sentences;
    const auto emptyStones = [&] {
        std::vector<RandomizerCheck> stones;
        for (const RemakeStone& stone : tables.stones) {
            if (!sentences.contains(stone.location)) {
                stones.push_back(stone.location);
            }
        }
        return stones;
    };
    std::set<RandomizerCheck> givenAway;
    std::array<Draw, BalancedDistribution.size()> draws = BalancedDistribution;
    while (sentences.size() < tables.stones.size()) {
        Draw& draw = WeightedDraw(draws);
        if (draw.kind == Junk) {
            sentences[RandomElement(emptyStones())] = RandomElement(tables.junk);
            continue;
        }
        std::vector<RandomizerCheck> candidates;
        for (RandomizerCheck loc : ctx->allLocations) {
            if (ctx->GetItemLocation(loc)->IsHintable() && !givenAway.contains(loc) && Fits(*draw.kind, loc)) {
                candidates.push_back(loc);
            }
        }
        std::vector<RandomizerCheck> stones;
        RandomizerCheck hinted = RC_UNKNOWN_CHECK;
        while (stones.empty() && !candidates.empty()) {
            hinted = RandomElement(candidates, true);
            stones = StonesReachableWithout(hinted, emptyStones());
        }
        if (stones.empty()) {
            draw.weight = 0;
            continue;
        }
        sentences[RandomElement(stones)] = Sentence(*draw.kind, hinted, tables, items);
        givenAway.insert(hinted);
        if (*draw.kind == Kind::Foolish) {
            const RandomizerCheckArea area = Rando::StaticData::GetLocation(hinted)->GetArea();
            for (RandomizerCheck loc : ctx->allLocations) {
                if (Rando::StaticData::GetLocation(loc)->GetArea() == area) {
                    givenAway.insert(loc);
                }
            }
        }
    }

    std::vector<StoneHint> hints;
    for (const RemakeStone& stone : tables.stones) {
        const std::vector<Word> words = Words(sentences.at(stone.location));
        hints.push_back({ stone, Layout(words), Plain(words) });
    }
    return hints;
}
