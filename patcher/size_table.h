// The size table, which gives the memory the game sets aside for each of its files.
#pragma once

#include "game_file.h"

#include <cstddef>
#include <string>
#include <string_view>

// The game reads the update's size table and ignores the base game's.
constexpr std::string_view SizeTablePath = "System/Resource/ResourceSizeTable.Product.111.Nin_Ounce_NVN2.rsizetable.zs";

// The game's own size table entries are a file's decompressed size rounded up to a multiple of 32, plus an overhead
// for its kind of file.
constexpr size_t SizeAlignment = 32;
constexpr size_t PlacementOverhead = 264;
constexpr size_t EventFlowOverhead = 272;
constexpr size_t TextPackOverhead = 312;
constexpr size_t ActorPackOverhead = 312;
constexpr size_t ActorTableOverhead = 264;
constexpr size_t ActorInfoOverhead = 3064;
constexpr size_t GameDataListOverhead = 264;

std::string ReadSizeTable(GameFile& game);

// Raises the size table entry of the file at this path if its decompressed size needs a larger one. Returns whether
// the entry changed. The game silently skips a file that is too large for its entry, and loads one that is smaller.
bool RaiseSize(std::string& table, std::string_view path, size_t size, size_t overhead);

// Adds the entry of a file the game does not have, in the CRC-32 order the table keeps.
void AddSize(std::string& table, std::string_view path, size_t size, size_t overhead);
