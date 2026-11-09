#pragma once

// 原理图模型与 .litematic 读写。
//
// 一个 Schematic 是「加载后、未放置」的原理图：坐标已归一化到 (0,0,0)，palette 里
// 同时保留 Java 原始状态（保存/诊断用）与基岩渲染状态（投影用），blocks 只存
// 相对坐标 + palette 下标。surface 位图在加载时算好，渲染时先做面剔除，
// 这是纯服务端投影能把发包量压下来的关键。

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "litematicabds/BlockState.h"
#include "litematicabds/Mapping.h"
#include "litematicabds/Result.h"
#include "litematicabds/Types.h"

namespace litematicabds {

struct PaletteEntry {
    std::string javaName;
    std::string javaProperties; // 规范化后的 Java 属性
    std::string bedrockState;   // 完整 "minecraft:xxx[...]"，渲染直接使用
    bool        air{false};
    bool        renderable{true}; // 平台校验失败（基岩版解析不了）时为 false
};

struct SchematicBlock {
    Vec3i         pos; // 相对原理图原点
    std::uint32_t palette{0};
};

struct RegionInfo {
    std::string name;
    Vec3i       position;
    Vec3i       size;
};

struct SchematicMeta {
    std::string name;
    std::string author;
    std::string description;
    std::string sourcePath;
    Vec3i       size{}; // 包围尺寸（各区域并集）
    std::int64_t timeCreated{0};
    std::int32_t regionCount{0};
    std::uint64_t totalBlocks{0}; // 非空气方块数
    std::uint64_t volume{0};
};

class Schematic {
public:
    SchematicMeta                meta;
    std::vector<PaletteEntry>    palette;
    std::vector<SchematicBlock>  blocks;
    std::vector<std::uint8_t>    surface; // 与 blocks 等长：1 = 至少一面暴露
    std::vector<RegionInfo>      regions;

    // 16³ 分块索引：key -> blocks 下标。渲染按玩家附近的块取，避免每次重规划扫全表。
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> chunks;

    static constexpr int kChunkSize = 16;

    void buildIndices();

    [[nodiscard]] Bounds       bounds() const noexcept { return {{0, 0, 0}, {meta.size.x - 1, meta.size.y - 1, meta.size.z - 1}}; }
    [[nodiscard]] std::uint64_t nonAirCount() const noexcept { return blocks.size(); }

    // 面剔除统计（诊断）。
    [[nodiscard]] std::uint64_t visibleCount() const noexcept;

    // 材料统计：按基岩方块名（不含属性）聚合，按数量降序。
    [[nodiscard]] std::vector<std::pair<std::string, std::uint64_t>> materialCounts() const;

    [[nodiscard]] static std::uint64_t chunkKey(int cx, int cy, int cz) noexcept;
    [[nodiscard]] static std::uint64_t chunkKeyFor(Vec3i local) noexcept;
};

struct LoadOptions {
    std::uint64_t maxBlocks{4'000'000};
    std::uint64_t maxVolume{64'000'000};
    bool          computeSurface{true};
};

struct LoadResult {
    std::shared_ptr<Schematic> schematic;
    std::vector<std::string>   warnings;
};

// 读取 .litematic（gzip 或未压缩 NBT 都接受）。
[[nodiscard]] Result<LoadResult> loadLitematicFile(
    std::filesystem::path const& path,
    BlockMapper const&           mapper,
    LoadOptions const&           options = {}
);

// 从已解压的 NBT 字节解析（测试/嵌入式用）。
[[nodiscard]] Result<LoadResult> parseLitematic(
    std::span<std::uint8_t const> nbt,
    std::string                   sourceName,
    BlockMapper const&            mapper,
    LoadOptions const&            options = {}
);

struct BuildBlock {
    Vec3i      pos; // 相对坐标
    BlockState state; // 基岩状态
};

struct SaveRequest {
    std::string              name;
    std::string              author{"LitematicaBDS"};
    std::string              description;
    Vec3i                    size;
    std::vector<BuildBlock>  blocks;
    std::int32_t             dataVersion{0};
};

// 写 .litematic（gzip 压缩）。blocks 会按 palette 去重；未知方块降级并写 warnings。
[[nodiscard]] Result<void> writeLitematicFile(
    std::filesystem::path const& path,
    SaveRequest const&           request,
    BlockMapper const&           mapper,
    std::vector<std::string>*    warnings = nullptr
);

} // namespace litematicabds
