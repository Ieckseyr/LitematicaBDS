#pragma once

// 服务端配置。字段都有安全的默认值：配置文件缺失/字段损坏时按默认值运行并给出警告，
// 不让一个写错的逗号把整个插件停掉。

#include <filesystem>
#include <string>
#include <vector>

#include "litematicabds/Result.h"

namespace litematicabds {

struct RenderConfig {
    int  blocksPerTick{256};          // 每 tick 每玩家最多发多少个假方块
    int  clearPerTick{512};           // 每 tick 每玩家最多还原多少个假方块
    int  viewDistance{64};            // 投影可见半径（格）
    int  autoLayerRadius{2};          // layer=auto 时上下各显示几层
    bool surfaceOnly{true};           // 只显示至少一面暴露的方块
    int  maxBlocksPerViewer{30000};   // 单个玩家同时显示上限
    int  planMoveThreshold{4};        // 玩家移动超过多少格触发重规划
    bool showStatusHud{false};        // 用 HologramLib 悬浮字显示状态（可选）
};

struct ProjectionConfig {
    bool        showBounds{true};     // 用形状画投影包围盒
    std::string boundsColor{"#00FF00"};
    int         defaultRotation{0};   // 0/90/180/270
    bool        defaultMirrorX{false};
    bool        defaultMirrorZ{false};
};

struct SulfurConfig {
    bool enabled{true};     // 靠近投影方块时换成硫磺怪
    int  radius{8};         // 触发半径（格）
    int  maxDisplays{64};   // 单玩家同时最多多少个硫磺怪
    bool visible{false};    // false = 只看到被吞的方块（硫磺怪隐身）；true = 连怪本体一起看到
    bool faceBlock{true};   // 让硫磺怪朝向尽量跟随方块的 facing
};

struct FillConfig {
    bool enabled{true};      // 手持对应方块点击硫磺怪 → 放置真实方块
    bool requireOp{false};   // 是否只允许 OP 使用填充
    bool consumeItem{true};  // 生存模式是否消耗手持方块（创造不消耗）
    bool quickFill{false};   // 管理员快速填充：木棍左键硫磺怪，从背包自动找方块并扣除
};

struct SaveConfig {
    int maxVolume{262144}; // 单次保存最大体积（默认 64³）；采样与写盘都在主线程
};

struct StorageConfig {
    std::string schematicsDir{"schematics"};
    std::string exportsDir{"exports"};
};

struct Config {
    RenderConfig     render;
    ProjectionConfig projection;
    SulfurConfig     sulfur;
    FillConfig       fill;
    SaveConfig       save;
    StorageConfig    storage;
    bool             debug{false};
};

// 读取 JSON（允许 // 与 /* */ 注释、尾随逗号）。文件不存在时返回默认配置。
// warnings 收集可恢复的问题（未知字段忽略、类型不符回退默认值）。
[[nodiscard]] Result<Config> loadConfigFile(std::filesystem::path const& path, std::vector<std::string>* warnings = nullptr);

// 覆盖写入完整配置（管理器表单提交时使用）。
[[nodiscard]] Result<void> saveConfigFile(std::filesystem::path const& path, Config const& config);

} // namespace litematicabds
