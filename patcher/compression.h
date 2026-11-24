// zstd frames as the game stores its .zs files.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// A frame whose header names a dictionary needs that dictionary.
std::string Decompress(std::string_view frame, std::string_view dictionary = {});

// The most bytes a frame's magic number and header take in zstd's format; they hold its decompressed size.
constexpr size_t FrameHeaderLimit = 18;

// The decompressed size a frame's header gives. Throws unless the header gives it.
size_t FrameContentSize(std::string_view frame);

// Compresses into one frame with the header the game's own files have.
std::string Compress(std::string_view data, std::string_view dictionary = {});

// The ID of the dictionary a frame's header names, or 0 when it names none.
unsigned FrameDictionaryId(std::string_view frame);

// The ID a dictionary gives the frames made with it.
unsigned DictionaryId(std::string_view dictionary);
