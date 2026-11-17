#pragma once

#include <string>
#include <stdint.h>

class Text {
  public:
    Text();
    Text(std::string english_, std::string french_, std::string german_);
    explicit Text(std::string english_);

    const std::string& GetEnglish() const;
    const std::string& GetFrench() const;
    const std::string& GetGerman() const;

    bool operator==(const Text& right) const;
    bool operator==(const std::string& right) const;
    bool operator!=(const Text& right) const;

    void Replace(const std::string& oldStr, const std::string& newStr);
    void Replace(const std::string& oldStr, const Text& newText);

    std::string english = "";
    std::string french = "";
    std::string german = "";
    std::string spanish = "";
};
