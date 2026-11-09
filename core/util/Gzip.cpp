#include "Gzip.h"

#include <cstring>
#include <limits>

#include "miniz.h"

namespace litematicabds::util {
namespace {

constexpr std::uint8_t kGzipMagic0 = 0x1F;
constexpr std::uint8_t kGzipMagic1 = 0x8B;

void appendLe32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

// 跳过 gzip 头，返回 deflate 数据起点；失败返回 0。
std::size_t skipGzipHeader(std::span<std::uint8_t const> data) {
    if (data.size() < 18 || data[0] != kGzipMagic0 || data[1] != kGzipMagic1 || data[2] != 8) return 0;
    std::uint8_t const flags = data[3];
    std::size_t      pos     = 10;

    if (flags & 0x04) { // FEXTRA
        if (pos + 2 > data.size()) return 0;
        std::size_t const xlen = static_cast<std::size_t>(data[pos]) | (static_cast<std::size_t>(data[pos + 1]) << 8);
        pos += 2;
        if (pos + xlen > data.size()) return 0;
        pos += xlen;
    }
    if (flags & 0x08) { // FNAME
        while (pos < data.size() && data[pos] != 0) ++pos;
        if (pos >= data.size()) return 0;
        ++pos;
    }
    if (flags & 0x10) { // FCOMMENT
        while (pos < data.size() && data[pos] != 0) ++pos;
        if (pos >= data.size()) return 0;
        ++pos;
    }
    if (flags & 0x02) { // FHCRC
        pos += 2;
    }
    if (pos + 8 > data.size()) return 0; // 至少留下 8 字节 trailer
    return pos;
}

} // namespace

bool looksGzip(std::span<std::uint8_t const> data) noexcept {
    return data.size() >= 2 && data[0] == kGzipMagic0 && data[1] == kGzipMagic1;
}

bool looksZlib(std::span<std::uint8_t const> data) noexcept {
    return data.size() >= 2 && (data[0] & 0x0F) == 8 && (static_cast<unsigned>(data[0]) << 8 | data[1]) % 31 == 0;
}

Result<std::vector<std::uint8_t>> decompress(std::span<std::uint8_t const> data) {
    if (data.empty()) return Error("压缩数据为空");

    std::size_t                        offset = 0;
    int                                flags  = TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
    std::span<std::uint8_t const>      input  = data;

    if (looksGzip(data)) {
        offset = skipGzipHeader(data);
        if (offset == 0) return Error("gzip 头损坏");
        input = data.subspan(offset);
        flags = 0; // 原始 deflate
    } else if (looksZlib(data)) {
        flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
    }

    std::size_t outLen   = 0;
    void*       outBytes = tinfl_decompress_mem_to_heap(input.data(), input.size(), &outLen, flags);
    if (outBytes == nullptr) return Error("deflate 解压失败");
    if (outLen > (std::numeric_limits<std::size_t>::max)()) {
        mz_free(outBytes);
        return Error("解压结果过大");
    }

    std::vector<std::uint8_t> out(outLen);
    if (outLen > 0) std::memcpy(out.data(), outBytes, outLen);
    mz_free(outBytes);
    return out;
}

Result<std::vector<std::uint8_t>> gzipCompress(std::span<std::uint8_t const> data, int level) {
    if (level < 0) level = 0;
    if (level > 9) level = 9;

    mz_uint const flags = tdefl_create_comp_flags_from_zip_params(level, -15, MZ_DEFAULT_STRATEGY);
    std::size_t   outLen = 0;
    void*         raw    = tdefl_compress_mem_to_heap(data.data(), data.size(), &outLen, static_cast<int>(flags));
    if (raw == nullptr) return Error("deflate 压缩失败");

    std::vector<std::uint8_t> out;
    out.reserve(outLen + 18);
    // gzip header：无文件名、mtime=0、OS=255
    out.insert(out.end(), {kGzipMagic0, kGzipMagic1, 8, 0, 0, 0, 0, 0, 0, 255});
    out.insert(out.end(), static_cast<std::uint8_t*>(raw), static_cast<std::uint8_t*>(raw) + outLen);
    mz_free(raw);

    std::uint32_t const crc = static_cast<std::uint32_t>(
        mz_crc32(0, data.empty() ? nullptr : data.data(), data.size())
    );
    appendLe32(out, crc);
    appendLe32(out, static_cast<std::uint32_t>(data.size()));
    return out;
}

} // namespace litematicabds::util
