#include "text.h"
#include <functional>
#include "z64.h"

Text::Text() = default;

Text::Text(std::string english_, std::string french_, std::string german_)
    : english(std::move(english_)), french(std::move(french_)), german(std::move(german_)), spanish("") {
    spanish = english;
}

Text::Text(std::string english_) : english(std::move(english_)), french(""), german(""), spanish("") {
    french = spanish = german = english;
}

static bool IsMissing(const std::string& translation) {
    return translation.empty() || translation == TODO_TRANSLATE;
}

const std::string& Text::GetEnglish() const {
    return english;
}

const std::string& Text::GetFrench() const {
    return IsMissing(french) ? english : french;
}

const std::string& Text::GetGerman() const {
    return IsMissing(german) ? english : german;
}

bool Text::operator==(const Text& right) const {
    return english == right.english;
}

bool Text::operator==(const std::string& right) const {
    return english == right || french == right || german == right || spanish == right;
}

bool Text::operator!=(const Text& right) const {
    return !operator==(right);
}

static void replaceAll(std::string& target, const std::string& oldStr, const std::string& replacement) {
    size_t position = target.find(oldStr);
    while (position != std::string::npos) {
        target.replace(position, oldStr.length(), replacement);
        position = target.find(oldStr);
    }
}

void Text::Replace(const std::string& oldStr, const std::string& newStr) {
    for (std::string& str : { std::ref(english), std::ref(french), std::ref(german), std::ref(spanish) }) {
        replaceAll(str, oldStr, newStr);
    }
}

void Text::Replace(const std::string& oldStr, const Text& newText) {
    replaceAll(english, oldStr, newText.GetEnglish());
    replaceAll(french, oldStr, newText.GetFrench());
    replaceAll(german, oldStr, newText.GetGerman());
}
