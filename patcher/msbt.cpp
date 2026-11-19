#include "msbt.h"

#include "bytes.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace msbt {

namespace {

// The header: "MsgStdBn", the byte order mark, the encoding, the version, the section count and the file's size.
// Two sections follow: LBL1, the labels, and TXT2, the texts. A section has a 16-byte header with its magic and its
// body's size, then its body, padded to 16 bytes.
constexpr std::string_view Magic = "MsgStdBn";
constexpr uint16_t LittleEndian = 0xFEFF;
constexpr uint8_t Utf8 = 0;
constexpr uint8_t Version = 3;
constexpr uint16_t SectionCount = 2;
constexpr size_t FileSize = 0x12;
constexpr size_t HeaderSize = 0x20;
constexpr std::string_view LabelsMagic = "LBL1";
constexpr std::string_view TextsMagic = "TXT2";
constexpr size_t SectionHeaderSize = 0x10;
constexpr size_t SectionAlignment = 0x10;
constexpr char SectionPadding = char(0xAB);

// The labels are a hash table. A label sits in the slot its hash picks, after the labels of earlier messages. The
// table has a slot count, then each slot's label count and the offset of its first label, then the labels: a length
// byte, the label, and the index of its message.
constexpr uint32_t LabelHashMultiplier = 0x492;
constexpr uint32_t MaxSlots = 101;

uint32_t SlotCount(size_t messages) {
    const auto prime = [](uint32_t n) {
        for (uint32_t k = 2; k * k <= n; ++k) {
            if (n % k == 0) {
                return false;
            }
        }
        return n >= 2;
    };
    uint32_t slots = uint32_t(messages / 2 + 1);
    while (!prime(slots)) {
        ++slots;
    }
    return std::min(slots, MaxSlots);
}

std::string_view Section(std::string_view data, size_t offset, std::string_view magic) {
    if (data.substr(offset, magic.size()) != magic) {
        throw std::runtime_error("the file's sections are not its labels then its texts");
    }
    const uint32_t size = ReadAt<uint32_t>(data, offset + 4);
    if (offset + SectionHeaderSize + uint64_t(size) > data.size()) {
        throw std::runtime_error("a section runs past the end of the file");
    }
    return data.substr(offset + SectionHeaderSize, size);
}

} // namespace

std::vector<Message> Parse(std::string_view data) {
    if (!data.starts_with(Magic) || ReadAt<uint16_t>(data, 0x08) != LittleEndian ||
        ReadAt<uint8_t>(data, 0x0C) != Utf8 || ReadAt<uint8_t>(data, 0x0D) != Version ||
        ReadAt<uint32_t>(data, FileSize) != data.size()) {
        throw std::runtime_error("not a little-endian UTF-8 MSBT version 3 file");
    }
    if (ReadAt<uint16_t>(data, 0x0E) != SectionCount) {
        throw std::runtime_error("the file has parts other than its labels and texts");
    }
    const std::string_view labels = Section(data, HeaderSize, LabelsMagic);
    const std::string_view texts =
        Section(data, HeaderSize + SectionHeaderSize + AlignUp(labels.size(), SectionAlignment), TextsMagic);

    // The texts section has the message count, then each text's offset.
    std::vector<Message> messages(ReadAt<uint32_t>(texts, 0));
    for (size_t i = 0; i < messages.size(); ++i) {
        const uint32_t start = ReadAt<uint32_t>(texts, 4 + 4 * i);
        const uint32_t end = i + 1 < messages.size() ? ReadAt<uint32_t>(texts, 4 + 4 * (i + 1)) : texts.size();
        if (start >= end || end > texts.size() || texts[end - 1] != '\0') {
            throw std::runtime_error("a text does not end in a zero byte");
        }
        messages[i].text = texts.substr(start, end - 1 - start);
    }
    const uint32_t slots = ReadAt<uint32_t>(labels, 0);
    for (uint32_t slot = 0; slot < slots; ++slot) {
        const uint32_t count = ReadAt<uint32_t>(labels, 4 + 8 * slot);
        size_t at = ReadAt<uint32_t>(labels, 8 + 8 * slot);
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t size = ReadAt<uint8_t>(labels, at);
            messages.at(ReadAt<uint32_t>(labels, at + 1 + size)).label = labels.substr(at + 1, size);
            at += 1 + size + 4;
        }
    }
    if (std::ranges::any_of(messages, &std::string::empty, &Message::label)) {
        throw std::runtime_error("a message has no label");
    }
    return messages;
}

std::string Write(const std::vector<Message>& messages) {
    const uint32_t slots = SlotCount(messages.size());
    std::vector<uint32_t> counts(slots);
    std::vector<std::string> entries(slots);
    for (size_t i = 0; i < messages.size(); ++i) {
        const uint32_t slot = NameHash(messages[i].label, LabelHashMultiplier) % slots;
        std::string& entry = entries[slot];
        ++counts[slot];
        entry += char(messages[i].label.size());
        entry += messages[i].label;
        entry.resize(entry.size() + 4);
        WriteAt(entry, entry.size() - 4, uint32_t(i));
    }
    std::string labels(4 + 8 * slots, '\0');
    WriteAt(labels, 0, slots);
    for (uint32_t slot = 0; slot < slots; ++slot) {
        WriteAt(labels, 4 + 8 * slot, counts[slot]);
        WriteAt(labels, 8 + 8 * slot, uint32_t(labels.size()));
        labels += entries[slot];
    }

    std::string texts(4 + 4 * messages.size(), '\0');
    WriteAt(texts, 0, uint32_t(messages.size()));
    for (size_t i = 0; i < messages.size(); ++i) {
        WriteAt(texts, 4 + 4 * i, uint32_t(texts.size()));
        texts += messages[i].text;
        texts += '\0';
    }

    std::string file(HeaderSize, '\0');
    file.replace(0, Magic.size(), Magic);
    WriteAt(file, 0x08, LittleEndian);
    WriteAt(file, 0x0C, Utf8);
    WriteAt(file, 0x0D, Version);
    WriteAt(file, 0x0E, SectionCount);
    for (const auto& [magic, body] : { std::pair(LabelsMagic, &labels), std::pair(TextsMagic, &texts) }) {
        const size_t section = file.size();
        file.resize(section + SectionHeaderSize);
        file.replace(section, magic.size(), magic);
        WriteAt(file, section + 4, uint32_t(body->size()));
        file += *body;
        file.resize(AlignUp(file.size(), SectionAlignment), SectionPadding);
    }
    WriteAt(file, FileSize, uint32_t(file.size()));
    return file;
}

} // namespace msbt
