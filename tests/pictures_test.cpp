#include "bytes.h"
#include "game_file.h"
#include "pictures.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

static int failures = 0;

static void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("failed: %s\n", what.c_str());
        ++failures;
    }
}

constexpr size_t GobWidth = 64;
constexpr size_t GobRows = 8;
constexpr size_t GobSize = 512;

// Inside a GOB this is the Switch 1's order with offset bits 5 and 6 swapped wherever they differ, as Lichii's
// NativeGobOffset swaps them for the Switch 2.
static size_t Tiled(size_t byte, size_t row, size_t rowBytes, size_t blockHeight) {
    const size_t gobsWide = (rowBytes + GobWidth - 1) / GobWidth;
    const size_t blockRows = GobRows * blockHeight;
    const size_t gob = (row / blockRows * gobsWide + byte / GobWidth) * blockHeight + row % blockRows / GobRows;
    size_t inside = byte % 64 / 32 * 256 + row % 8 / 2 * 64 + byte % 32 / 16 * 32 + row % 2 * 16 + byte % 16;
    if ((inside >> 5 & 1) != (inside >> 6 & 1)) {
        inside ^= 0x60;
    }
    return gob * GobSize + inside;
}

struct Texture {
    std::string name;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t gobsLog2;
    std::array<uint8_t, 4> channels;
};

constexpr uint32_t Astc4x4Srgb = 0x2D06;
constexpr uint32_t Bc4 = 0x1D01;
// What a channel reads, as a BRTI header's channel selection gives it.
enum Selector : uint8_t { Zero, One, Red, Green, Blue, Alpha };
constexpr std::array<uint8_t, 4> Plain = { Red, Green, Blue, Alpha };

static std::array<uint8_t, 4> BlockColour(size_t column, size_t row) {
    return { uint8_t(column * 6), uint8_t(row * 13), 77, 200 };
}

// An ASTC block of one colour is the void-extent block: a fixed header, then the colour as four 16-bit values.
static std::string Container(const std::vector<Texture>& textures) {
    std::string file(0x40 + 8 * textures.size(), '\0');
    file.replace(0, 4, "BNTX");
    WriteAt<uint32_t>(file, 0x24, uint32_t(textures.size()));
    WriteAt<uint64_t>(file, 0x28, 0x40);
    for (size_t i = 0; i < textures.size(); ++i) {
        const Texture& texture = textures[i];
        const size_t columns = (texture.width + 3) / 4;
        const size_t rows = (texture.height + 3) / 4;
        const size_t blockHeight = size_t(1) << texture.gobsLog2;
        const size_t blocksTall = (rows + GobRows * blockHeight - 1) / (GobRows * blockHeight);
        std::string data((columns * 16 + GobWidth - 1) / GobWidth * blocksTall * blockHeight * GobSize, '\0');
        for (size_t row = 0; row < rows; ++row) {
            for (size_t column = 0; column < columns; ++column) {
                std::string block = "\xFC\xFD\xFF\xFF\xFF\xFF\xFF\xFF";
                for (const uint8_t value : BlockColour(column, row)) {
                    block += std::string(2, char(value));
                }
                for (size_t byte = 0; byte < 16; ++byte) {
                    data[Tiled(column * 16 + byte, row, columns * 16, blockHeight)] = block[byte];
                }
            }
        }

        const size_t brti = file.size();
        WriteAt<uint64_t>(file, 0x40 + 8 * i, brti);
        file.append(0x78, '\0');
        file.replace(brti, 4, "BRTI");
        WriteAt<uint16_t>(file, brti + 0x16, 1);
        WriteAt<uint32_t>(file, brti + 0x1C, texture.format);
        WriteAt<uint32_t>(file, brti + 0x24, texture.width);
        WriteAt<uint32_t>(file, brti + 0x28, texture.height);
        WriteAt<uint32_t>(file, brti + 0x34, texture.gobsLog2);
        WriteAt<uint32_t>(file, brti + 0x50, uint32_t(data.size()));
        for (size_t channel = 0; channel < 4; ++channel) {
            WriteAt<uint8_t>(file, brti + 0x58 + channel, texture.channels[channel]);
        }
        WriteAt<uint64_t>(file, brti + 0x60, file.size());
        file += std::string(1, char(texture.name.size())) + '\0' + texture.name;
        WriteAt<uint64_t>(file, brti + 0x70, file.size());
        file.append(8, '\0');
        WriteAt<uint64_t>(file, file.size() - 8, file.size());
        file += data;
    }
    return file;
}

static void ContainerTest() {
    // Neither size of "Tiled" is a multiple of 4. It is 10 GOBs wide, the last one half used, and two blocks of GOBs
    // tall, the second one partly used.
    const std::string file = Container({
        { .name = "Tiled", .format = Astc4x4Srgb, .width = 150, .height = 70, .gobsLog2 = 1, .channels = Plain },
        { .name = "Selected",
          .format = Astc4x4Srgb,
          .width = 9,
          .height = 5,
          .gobsLog2 = 0,
          .channels = { Alpha, Zero, One, Red } },
        { .name = "Other", .format = Bc4, .width = 4, .height = 4, .gobsLog2 = 0, .channels = Plain },
    });

    const std::vector<std::string> names = { "Tiled", "Selected" };
    const std::map<std::string, Picture> pictures = ReadTextures(file, names);
    for (const std::string& name : names) {
        const auto picture = pictures.find(name);
        if (picture == pictures.end()) {
            Check(false, name + " comes back");
            continue;
        }
        const Picture& read = picture->second;
        const bool selected = name == "Selected";
        Check(read.width == (selected ? 9 : 150) && read.height == (selected ? 5 : 70), name + " has its size");
        Check(read.rgba.size() == size_t(read.width) * read.height * 4, name + " has four bytes a pixel");
        if (read.rgba.size() != size_t(read.width) * read.height * 4) {
            continue;
        }
        size_t wrong = 0;
        for (size_t y = 0; y < read.height; ++y) {
            for (size_t x = 0; x < read.width; ++x) {
                const std::array<uint8_t, 4> colour = BlockColour(x / 4, y / 4);
                const std::array<uint8_t, 4> expected =
                    selected ? std::array<uint8_t, 4>{ colour[3], 0, 255, colour[0] } : colour;
                for (size_t channel = 0; channel < 4; ++channel) {
                    wrong += uint8_t(read.rgba[(y * read.width + x) * 4 + channel]) != expected[channel];
                }
            }
        }
        Check(wrong == 0,
              name + " has each block's colour where the block belongs, " + std::to_string(wrong) + " channels differ");
    }

    for (const std::string name : { "Other", "Missing" }) {
        try {
            ReadTextures(file, std::vector{ name });
            Check(false, name + " is refused");
        } catch (const std::exception& e) {
            Check(std::string(e.what()).find(name) != std::string::npos, name + " is refused by name: " + e.what());
        }
    }
}

struct Expected {
    std::string name;
    uint32_t width;
    uint32_t height;
    uint32_t crc;
};

static void GameTest(const char* nup) {
    const std::vector<Expected> expected = {
        { "Item_Hookshot", 341, 341, 0xfdbeb457 },
        { "Item_DekuStick", 683, 683, 0x5f05f88d },
        { "Item_Hammer", 1440, 1440, 0xf09ce1b6 },
        { "Item_Dungeon_Map", 171, 171, 0xad7b8b31 },
        // A song's symbol, whose channels are red, red, red and alpha.
        { "OcarinaMelodyIcon_00", 107, 107, 0xfae53267 },
        // The Forest Medallion.
        { "DataFileImportantIcon_04", 133, 133, 0x84dc3ed3 },
        { "CollectHeartPiece_00", 300, 267, 0x943c4a88 },
    };
    std::vector<std::string> names;
    for (const Expected& picture : expected) {
        names.push_back(picture.name);
    }
    GameFile game(nup);
    const std::map<std::string, Picture> pictures = ReadPictures(game, names);
    for (const Expected& picture : expected) {
        const auto read = pictures.find(picture.name);
        if (read == pictures.end()) {
            Check(false, picture.name + " comes back");
            continue;
        }
        Check(read->second.width == picture.width && read->second.height == picture.height,
              picture.name + " has its size");
        Check(Crc32(read->second.rgba) == picture.crc, picture.name + " has its pixels");
    }

    try {
        ReadPictures(game, std::vector<std::string>{ "Item_Hookshot", "Item_NoSuchThing" });
        Check(false, "a name the game does not have is refused");
    } catch (const std::exception& e) {
        Check(std::string(e.what()).find("Item_NoSuchThing") != std::string::npos,
              std::string("a name the game does not have is refused by name: ") + e.what());
    }
}

int main(int argc, char** argv) {
    if (argc > 1) {
        GameTest(argv[1]);
    } else {
        ContainerTest();
    }
    std::printf("%s\n", failures == 0 ? "all passed" : "some failed");
    return failures == 0 ? 0 : 1;
}
