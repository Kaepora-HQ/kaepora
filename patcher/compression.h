// zstd frames as the game stores its .zs files.
#pragma once

#include <string>
#include <string_view>

// A frame whose header names a dictionary needs that dictionary.
std::string Decompress(std::string_view frame, std::string_view dictionary = {});

// Compresses into one frame with the header the game's own files have.
std::string Compress(std::string_view data, std::string_view dictionary = {});

// The ID of the dictionary a frame's header names, or 0 when it names none.
unsigned FrameDictionaryId(std::string_view frame);

// The ID a dictionary gives the frames made with it.
unsigned DictionaryId(std::string_view dictionary);
