#include "pictures.h"

#include "bytes.h"
#include "compression.h"

#include <astcenc.h>

#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::string_view IconFolder = "UI/Tex/Icon/";
constexpr std::string_view IconSuffix = ".bntx.zs";
constexpr std::string_view LayoutArchive = "UI/Layout2Archive/Common.Product.100.Nin_Ounce_NVN2.bntx.zs";

constexpr std::string_view BntxMagic = "BNTX";
constexpr size_t BntxTextureCount = 0x24;
constexpr size_t BntxTextureTable = 0x28;
constexpr size_t BrtiTileMode = 0x12;
constexpr size_t BrtiFormat = 0x1C;
constexpr size_t BrtiWidth = 0x24;
constexpr size_t BrtiHeight = 0x28;
constexpr size_t BrtiGobsLog2 = 0x34;
constexpr size_t BrtiChannels = 0x58;
constexpr size_t BrtiName = 0x60;
constexpr size_t BrtiLevels = 0x70;
constexpr uint16_t BlockLinear = 0;
constexpr uint32_t Astc4x4Srgb = 0x2D06;

constexpr size_t BlockSize = 16;
constexpr size_t BlockPixels = 4;
constexpr size_t GobSize = 512;
constexpr size_t GobBlocksWide = 4;
constexpr size_t GobRows = 8;

constexpr std::array<astcenc_swz, 6> Selectors = { ASTCENC_SWZ_0, ASTCENC_SWZ_1, ASTCENC_SWZ_R,
                                                   ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };

using Context = std::unique_ptr<astcenc_context, decltype(&astcenc_context_free)>;

void Check(astcenc_error status) {
    if (status != ASTCENC_SUCCESS) {
        throw std::runtime_error(std::string("astcenc: ") + astcenc_get_error_string(status));
    }
}

Context NewContext() {
    astcenc_config config;
    Check(astcenc_config_init(ASTCENC_PRF_LDR_SRGB, BlockPixels, BlockPixels, 1, ASTCENC_PRE_FASTEST,
                              ASTCENC_FLG_DECOMPRESS_ONLY, &config));
    astcenc_context* context = nullptr;
    Check(astcenc_context_alloc(&config, 1, &context, nullptr));
    return Context(context, astcenc_context_free);
}

// The Switch 2's order: the Switch 1's with offset bits 5 and 6 swapped, as NativeGobOffset in Lichii's
// src/video_core/textures/decoders.cpp swaps them.
constexpr size_t BlockInGob(size_t row, size_t column) {
    return column / 2 * 256 + row / 4 * 128 + column % 2 * 64 + row / 2 % 2 * 32 + row % 2 * 16;
}

std::string Untile(std::string_view tiled, size_t columns, size_t rows, size_t blockHeight) {
    const size_t gobsWide = (columns + GobBlocksWide - 1) / GobBlocksWide;
    const size_t blockRows = blockHeight * GobRows;
    const size_t gobsTall = (rows + blockRows - 1) / blockRows * blockHeight;
    if (gobsWide * gobsTall * GobSize > tiled.size()) {
        throw std::runtime_error("the texture's data runs past the end of the container");
    }
    std::string blocks(columns * rows * BlockSize, '\0');
    for (size_t row = 0; row < rows; ++row) {
        for (size_t column = 0; column < columns; ++column) {
            const size_t gob =
                (row / blockRows * gobsWide + column / GobBlocksWide) * blockHeight + row % blockRows / GobRows;
            std::memcpy(&blocks[(row * columns + column) * BlockSize],
                        &tiled[gob * GobSize + BlockInGob(row % GobRows, column % GobBlocksWide)], BlockSize);
        }
    }
    return blocks;
}

Picture Decode(astcenc_context* context, std::string_view bntx, size_t brti) {
    const uint32_t format = ReadAt<uint32_t>(bntx, brti + BrtiFormat);
    const uint16_t tileMode = ReadAt<uint16_t>(bntx, brti + BrtiTileMode);
    if (format != Astc4x4Srgb || tileMode != BlockLinear) {
        throw std::runtime_error(std::format(
            "its format is {:#06x} with tile mode {}, and only block-linear ASTC 4x4 sRGB is read", format, tileMode));
    }
    Picture picture{ .width = ReadAt<uint32_t>(bntx, brti + BrtiWidth),
                     .height = ReadAt<uint32_t>(bntx, brti + BrtiHeight) };
    const size_t columns = (picture.width + BlockPixels - 1) / BlockPixels;
    const size_t rows = (picture.height + BlockPixels - 1) / BlockPixels;
    const uint64_t start = ReadAt<uint64_t>(bntx, ReadAt<uint64_t>(bntx, brti + BrtiLevels));
    const std::string blocks =
        Untile(bntx.substr(start), columns, rows, size_t(1) << ReadAt<uint32_t>(bntx, brti + BrtiGobsLog2));

    picture.rgba.resize(size_t(picture.width) * picture.height * 4);
    void* pixels = picture.rgba.data();
    astcenc_image image{
        .dim_x = picture.width, .dim_y = picture.height, .dim_z = 1, .data_type = ASTCENC_TYPE_U8, .data = &pixels
    };
    const auto selector = [&](size_t channel) {
        return Selectors.at(ReadAt<uint8_t>(bntx, brti + BrtiChannels + channel));
    };
    const astcenc_swizzle swizzle{ .r = selector(0), .g = selector(1), .b = selector(2), .a = selector(3) };
    Check(astcenc_decompress_image(context, reinterpret_cast<const uint8_t*>(blocks.data()), blocks.size(), &image,
                                   &swizzle, 0));
    return picture;
}

void ReadInto(astcenc_context* context, std::string_view bntx, std::span<const std::string> names,
              std::map<std::string, Picture>& pictures) {
    if (!bntx.starts_with(BntxMagic)) {
        throw std::runtime_error("not a BNTX texture container");
    }
    std::map<std::string, size_t, std::less<>> textures;
    const uint64_t table = ReadAt<uint64_t>(bntx, BntxTextureTable);
    for (uint32_t i = 0; i < ReadAt<uint32_t>(bntx, BntxTextureCount); ++i) {
        const uint64_t brti = ReadAt<uint64_t>(bntx, table + 8 * i);
        const uint64_t name = ReadAt<uint64_t>(bntx, brti + BrtiName);
        textures.emplace(bntx.substr(name + 2, ReadAt<uint16_t>(bntx, name)), brti);
    }
    for (const std::string& name : names) {
        const auto texture = textures.find(name);
        if (texture == textures.end()) {
            throw std::runtime_error(name + ": the game has no picture of this name");
        }
        try {
            pictures.insert_or_assign(name, Decode(context, bntx, texture->second));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("{}: {}", name, e.what()));
        }
    }
}

} // namespace

std::map<std::string, Picture> ReadTextures(std::string_view bntx, std::span<const std::string> names) {
    std::map<std::string, Picture> pictures;
    ReadInto(NewContext().get(), bntx, names, pictures);
    return pictures;
}

std::map<std::string, Picture> ReadPictures(GameFile& game, std::span<const std::string> names) {
    const Context context = NewContext();
    std::map<std::string, Picture> pictures;
    std::vector<std::string> menus;
    for (const std::string& name : names) {
        const std::string icon = std::format("{}{}{}", IconFolder, name, IconSuffix);
        if (game.Files().contains(icon)) {
            ReadInto(context.get(), Decompress(game.Read(icon)), std::span(&name, 1), pictures);
        } else {
            menus.push_back(name);
        }
    }
    if (!menus.empty()) {
        ReadInto(context.get(), Decompress(game.Read(std::string(LayoutArchive))), menus, pictures);
    }
    return pictures;
}
