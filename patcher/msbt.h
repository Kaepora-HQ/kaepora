// MSBT version 3, the game's message files, as its text packs hold them.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace msbt {

struct Message {
    std::string label;
    std::string text; // UTF-8 with the game's control codes, without the zero byte that ends it in the file
};

// The messages in the file's order. Throws on a file with parts other than labels and texts.
std::vector<Message> Parse(std::string_view data);

std::string Write(const std::vector<Message>& messages);

// The control codes a text opens with, before its first character. A character's talk opens with its voice and face.
std::string_view Opening(std::string_view text);

constexpr std::string_view PageTag = "{page}";

// The text the tags stand for: {color:N} sets the text's colour (65535 the box's own), {size:N} its size in percent,
// {page} starts a new box, and {G:T:HEX} is any control code by its group, type and parameter bytes. A line break is
// itself. Throws, naming the tag, on a tag it does not know, and on a zero or code byte outside a tag.
std::string Encode(std::string_view tagged);

} // namespace msbt
