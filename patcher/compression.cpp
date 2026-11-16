#include "compression.h"

#include <zstd.h>

#include <memory>
#include <stdexcept>

constexpr int Level = 19;

static size_t Check(size_t result) {
    if (ZSTD_isError(result)) {
        throw std::runtime_error(std::string("zstd: ") + ZSTD_getErrorName(result));
    }
    return result;
}

std::string Decompress(std::string_view frame, std::string_view dictionary) {
    const unsigned long long size = ZSTD_getFrameContentSize(frame.data(), frame.size());
    if (size == ZSTD_CONTENTSIZE_UNKNOWN || size == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("not a zstd frame with its size in the header");
    }
    std::string data(size, '\0');
    const std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
    Check(ZSTD_decompress_usingDict(context.get(), data.data(), data.size(), frame.data(), frame.size(),
                                    dictionary.data(), dictionary.size()));
    return data;
}

std::string Compress(std::string_view data, std::string_view dictionary) {
    std::string frame(ZSTD_compressBound(data.size()), '\0');
    const std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> context(ZSTD_createCCtx(), ZSTD_freeCCtx);
    // libzstd's defaults give the frame header the game's files have: the content size, the dictionary ID, and no
    // checksum.
    frame.resize(Check(ZSTD_compress_usingDict(context.get(), frame.data(), frame.size(), data.data(), data.size(),
                                               dictionary.data(), dictionary.size(), Level)));
    return frame;
}
