#pragma once

// gzip 容器 + deflate 压缩（内置 miniz，避免给插件增加 zlib 运行时依赖）。
// .litematic 是 gzip 压缩的 NBT；保存时也写成 gzip，Java 版 Litematica 可直接打开。

#include <cstdint>
#include <span>
#include <vector>

#include "litematicabds/Result.h"

namespace litematicabds::util {

[[nodiscard]] bool looksGzip(std::span<std::uint8_t const> data) noexcept;
[[nodiscard]] bool looksZlib(std::span<std::uint8_t const> data) noexcept;

// 自动识别 gzip / zlib / 原始 deflate；失败返回 Error。
[[nodiscard]] Result<std::vector<std::uint8_t>> decompress(std::span<std::uint8_t const> data);

// 输出标准 gzip（RFC 1952）。
[[nodiscard]] Result<std::vector<std::uint8_t>> gzipCompress(std::span<std::uint8_t const> data, int level = 6);

} // namespace litematicabds::util
