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

} // namespace msbt
