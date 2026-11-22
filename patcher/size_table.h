// The size table, which gives the memory the game sets aside for each of its files.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// The game reads the update's size table and ignores the base game's.
constexpr std::string_view SizeTablePath = "System/Resource/ResourceSizeTable.Product.111.Nin_Ounce_NVN2.rsizetable.zs";

// The game's own size table entries for the kinds of file the patcher writes are a file's decompressed size rounded up
// to a multiple of 32, plus an overhead for its kind. Other kinds, as parameter files and models, differ file by file.
constexpr size_t SizeAlignment = 32;
constexpr size_t PlacementOverhead = 264;
constexpr size_t EventFlowOverhead = 272;
constexpr size_t TextPackOverhead = 312;
constexpr size_t ActorPackOverhead = 312;
constexpr size_t ActorTableOverhead = 264;
constexpr size_t ActorInfoOverhead = 3064;
constexpr size_t GameDataListOverhead = 264;

// The size table in the zstd frame, decompressed. Throws unless it is a version 1 size table.
std::string ReadSizeTable(std::string_view frame);

// The entries of a decompressed size table.
class SizeEntries {
public:
    explicit SizeEntries(std::string_view table);

    // The entry of the file at this path, by the path where the table lists it by name, and otherwise by the CRC-32 of
    // the path.
    std::optional<uint32_t> Find(std::string_view path) const;

private:
    std::unordered_map<uint32_t, uint32_t> byCrc;
    std::map<std::string, uint32_t, std::less<>> byName;
};

// Raises the size table entry of the file at this path if its decompressed size needs a larger one. Returns whether
// the entry changed. The game silently skips a file that is too large for its entry, and loads one that is smaller.
bool RaiseSize(std::string& table, std::string_view path, size_t size, size_t overhead);

// Adds the entry of a file the game does not have, in the CRC-32 order the table keeps.
void AddSize(std::string& table, std::string_view path, size_t size, size_t overhead);
