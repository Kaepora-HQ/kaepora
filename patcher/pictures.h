#pragma once

#include "game_file.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>

// A picture of the game's: rows from the top, four bytes a pixel as red, green, blue and alpha, in sRGB.
struct Picture {
    uint32_t width;
    uint32_t height;
    std::string rgba;
};

// The textures of these names in a BNTX container, with each texture's own channel selection applied. Throws, naming
// the texture, when the container has none of that name or holds it other than as block-linear ASTC 4x4 sRGB.
std::map<std::string, Picture> ReadTextures(std::string_view bntx, std::span<const std::string> names);

// The game's pictures of these names. An item's icon is the texture of UI/Tex/Icon/<name>.bntx.zs, and a picture of
// the menus is a texture of the common layout archive. Throws, naming the picture, as ReadTextures does.
std::map<std::string, Picture> ReadPictures(GameFile& game, std::span<const std::string> names);
