// zstd frames as the game stores its .zs files.
#pragma once

#include <string>
#include <string_view>

// A frame whose header names a dictionary needs that dictionary.
std::string Decompress(std::string_view frame, std::string_view dictionary = {});

// Compresses into one frame with the header the game's own files have.
std::string Compress(std::string_view data, std::string_view dictionary);
