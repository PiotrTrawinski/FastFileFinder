#pragma once

#define LZ4_DISABLE_DEPRECATE_WARNINGS
#include "lz4.h"
#include "lz4hc.h"
#include "lz4frame.h"

#include <tuple>
#include <vector>

static std::pair<std::vector<char>, int32_t> compress(char* data, int32_t size) {
    int maxDstSize = LZ4_compressBound(size);
    std::vector<char> compressedDataBuffer(maxDstSize);
    int compressedSize = LZ4_compress_fast(data, compressedDataBuffer.data(), size, int(compressedDataBuffer.size()), 1);
    return { compressedDataBuffer, compressedSize };
}
static std::vector<char> decompress(char* data, int32_t originalSize) {
    std::vector<char> dst(originalSize);
    LZ4_decompress_fast(data, dst.data(), originalSize);
    return dst;
}
