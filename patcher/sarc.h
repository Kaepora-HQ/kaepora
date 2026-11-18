#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace sarc {

// Each file's data by its name.
using Files = std::map<std::string, std::string>;

Files Parse(std::string_view data);

// Each file of an actor pack starts on a multiple of this from the start of the files' data.
constexpr size_t ActorPackAlignment = 8;

// The archive laid out as the game's are: the files in the order of their names' hashes, each starting on a multiple of
// the alignment.
std::string Write(const Files& files, size_t alignment);

} // namespace sarc
