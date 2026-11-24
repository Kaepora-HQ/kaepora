#include "seed_code.h"

#include "seed_text.h"
#include "version.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <exception>
#include <iterator>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

// RFC 4648's base32 alphabet.
constexpr std::string_view Base32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
constexpr size_t CharacterBits = 5;
// A double-click selects a whole code whose parts are joined by underscores. A dash splits it.
constexpr char Joint = '_';

namespace {
struct BitWriter {
    std::vector<bool> bits;

    void value(size_t& value, size_t count) {
        for (auto bit = std::bit_width(count - 1); bit-- > 0;) {
            bits.push_back((value >> bit) & 1);
        }
    }
};

// A value outside its count reads as the first one, so the code of the settings read differs from the code read.
struct BitReader {
    std::vector<bool> bits;
    size_t needed = 0;

    void value(size_t& value, size_t count) {
        value = 0;
        for (auto bit = std::bit_width(count - 1); bit-- > 0; ++needed) {
            value = (value << 1) | (needed < bits.size() && bits[needed]);
        }
        if (value >= count) {
            value = 0;
        }
    }
};
}

template <typename Value, size_t Size>
static size_t IndexOf(const std::pair<Value, std::string_view> (&choices)[Size], Value value) {
    return std::ranges::find(choices, value, &std::pair<Value, std::string_view>::first) - std::begin(choices);
}

template <typename Bits, typename Value, size_t Size>
static void Choice(Bits& bits, Value& value, const std::pair<Value, std::string_view> (&choices)[Size]) {
    size_t index = IndexOf(choices, value);
    bits.value(index, Size);
    value = choices[index].first;
}

template <typename Bits, typename Row>
static void Rows(Bits& bits, std::set<Row>& set, const auto& table) {
    for (const Row& row : table) {
        size_t in = set.contains(row);
        bits.value(in, 2);
        if (in == 1) {
            set.insert(row);
        } else {
            set.erase(row);
        }
    }
}

template <typename Bits>
static void Price(Bits& bits, int& price) {
    size_t steps = price / ShopPriceStep;
    bits.value(steps, MostShopPrice / ShopPriceStep + 1);
    price = static_cast<int>(steps) * ShopPriceStep;
}

template <typename Bits>
static void Shops(Bits& bits, std::optional<ShuffledShops>& shops) {
    size_t goods = shops ? 1 + IndexOf(ShopCountChoices, shops->count) : 0;
    bits.value(goods, 1 + std::size(ShopCountChoices));
    if (goods == 0) {
        shops.reset();
        return;
    }
    shops = ShuffledShops{ ShopCountChoices[goods - 1].first, shops ? shops->prices : DefaultShopPrices };
    if (!ShopPricesApply(shops)) {
        return;
    }
    ShopPrices& prices = shops->prices;
    size_t kind = &ShopPricesChoice(prices) - std::begin(ShopPricesChoices);
    bits.value(kind, std::size(ShopPricesChoices));
    if (prices.index() != ShopPricesChoices[kind].first.index()) {
        prices = ShopPricesChoices[kind].first;
    }
    if (auto* one = std::get_if<OnePrice>(&prices)) {
        Price(bits, one->price);
    }
    if (auto* between = std::get_if<PricesBetween>(&prices)) {
        Price(bits, between->lowest);
        Price(bits, between->highest);
    }
}

// The writer and the reader both run the layout. A structured binding must name every member, so a new setting does
// not compile until the binding names it.
template <typename Bits>
static void Layout(Bits& bits, SeedSettings& settings) {
    auto& [shuffledDungeons, keys, mapsAndCompasses, forest, doorOfTime, bridge, trials, skips, startWith, shops, gifts,
           rewards, skulltulas, songs, looseItems, lens, gossipStones, hintsFromCharacters] = settings;
    Rows(bits, shuffledDungeons, Dungeons);
    Choice(bits, keys, KeysChoices);
    Choice(bits, mapsAndCompasses, MapsAndCompassesChoices);
    Choice(bits, forest, ForestChoices);
    Choice(bits, doorOfTime, DoorOfTimeChoices);
    Choice(bits, bridge, BridgeChoices);
    Choice(bits, trials, TrialsChoices);
    Rows(bits, skips, std::views::keys(SkipChoices));
    Rows(bits, startWith, StartSteps);
    Shops(bits, shops);
    Rows(bits, gifts, GiftGroups);
    Choice(bits, rewards, RewardsChoices);
    Choice(bits, skulltulas, SkulltulasChoices);
    Choice(bits, songs, SongsChoices);
    Choice(bits, looseItems, LooseItemsChoices);
    Choice(bits, lens, LensChoices);
    Choice(bits, gossipStones, GossipStonesChoices);
    Choice(bits, hintsFromCharacters, HintsFromCharactersChoices);
}

std::string ToSeedCode(const std::string& seed, const SeedSettings& settings) {
    BitWriter writer;
    SeedSettings written = settings;
    Layout(writer, written);
    std::string code = UpperCase(KAEPORA_VERSION) + Joint + UpperCase(seed) + Joint;
    for (size_t first = 0; first < writer.bits.size(); first += CharacterBits) {
        size_t value = 0;
        for (size_t bit = first; bit < first + CharacterBits; ++bit) {
            value = (value << 1) | (bit < writer.bits.size() && writer.bits[bit]);
        }
        code += Base32[value];
    }
    return code;
}

static bool IsVersion(std::string_view text) {
    return !text.empty() && std::isdigit(static_cast<unsigned char>(text.front())) &&
           text.find('.') != std::string_view::npos &&
           std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c) != 0 || c == '.' || c == '-'; });
}

static bool IsLettersAndDigits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c) != 0; });
}

std::variant<SharedSeed, FromAnotherVersion, Unreadable> ParseSeedCode(std::string_view text) {
    const std::string code = UpperCase(text);
    if (std::ranges::count(code, Joint) != 2) {
        return Unreadable{};
    }
    const size_t seedStart = code.find(Joint) + 1;
    const size_t settingsStart = code.find(Joint, seedStart) + 1;
    const std::string version = code.substr(0, seedStart - 1);
    const std::string seed = code.substr(seedStart, settingsStart - 1 - seedStart);
    const std::string_view letters = std::string_view(code).substr(settingsStart);
    if (!IsVersion(version) || !IsLettersAndDigits(seed) || !IsLettersAndDigits(letters)) {
        return Unreadable{};
    }
    if (version != UpperCase(KAEPORA_VERSION)) {
        return FromAnotherVersion{ version };
    }
    BitReader reader;
    for (const char letter : letters) {
        const size_t value = Base32.find(letter);
        for (size_t bit = CharacterBits; bit-- > 0;) {
            reader.bits.push_back(value != std::string_view::npos && ((value >> bit) & 1));
        }
    }
    SeedSettings read;
    Layout(reader, read);
    if (reader.needed > reader.bits.size()) {
        return Unreadable{};
    }
    SeedSettings logged;
    try {
        logged = ParseSeedSettings(ToJson(read));
    } catch (const std::exception&) {
        return Unreadable{};
    }
    if (ToSeedCode(seed, logged) != code) {
        return Unreadable{};
    }
    return SharedSeed{ seed, logged };
}
