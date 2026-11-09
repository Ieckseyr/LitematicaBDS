#pragma once

// Java 版方块状态 -> 基岩版方块状态（以及反向）。
//
// 正向转换是投影渲染的关键路径：.litematic 的调色板存的是 Java 名与属性，
// 基岩版同名方块的状态键经常不同（facing -> weirdo_direction、half -> upside_down_bit、
// axis -> pillar_axis…）。映射表由 tools/gen_block_map.mjs 从 LitematicaBE (MIT) 生成，
// 特殊转换逻辑在 core/mapping/StateConverter.cpp。
//
// 反向转换只用于「保存原理图」：基岩世界里的方块要写成 Java 版 Litematica 能读的
// palette。无法反向识别的方块会降级为 minecraft:stone 并记一条警告。

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <string_view>

#include "litematicabds/BlockState.h"

namespace litematicabds {

class BlockMapper {
public:
    BlockMapper();
    ~BlockMapper();
    BlockMapper(BlockMapper const&)            = delete;
    BlockMapper& operator=(BlockMapper const&) = delete;

    // 已知 Java 方块：返回映射后的基岩状态；未知方块：保留名字、丢弃属性（基岩默认态）。
    // known 非空时写入是否命中映射表。
    [[nodiscard]] BlockState javaToBedrock(
        std::string_view javaName,
        std::string_view javaProperties,
        bool*            known = nullptr
    ) const;

    // 只查名字映射，不转状态。
    [[nodiscard]] std::optional<std::string> bedrockNameOf(std::string_view javaName) const;

    // 基岩状态 -> Java 状态（保存用）。未知基岩名原样返回。
    [[nodiscard]] BlockState bedrockToJava(BlockState const& bedrock) const;

    [[nodiscard]] std::size_t tableSize() const noexcept;

    // 映射表里所有基岩方块名（带 minecraft: 前缀，去重），用于平台侧存在性自检。
    [[nodiscard]] std::vector<std::string> bedrockNames() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace litematicabds
