#pragma once

// 渲染抽象：核心只产出「把某玩家的某坐标看成某状态 / 还原」的指令，
// 具体发包由平台实现（LeviLamina 用 HologramLib IViewOverride，endstone 用
// HologramLib Python 的 fakeblock）。核心不认识任何引擎类型。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "litematicabds/Types.h"

namespace litematicabds {

struct ViewerSnapshot {
    std::string name;
    Vec3d       position;
    int         dimension{0};
};

struct BlockChange {
    Vec3i       pos;
    std::string state; // 空 = 调用方只需坐标（清理）
};

class IGhostSink {
public:
    virtual ~IGhostSink() = default;

    // 把 player 眼里的 pos 显示成 state。返回是否成功交给底层。
    virtual bool setBlock(std::string_view player, Vec3i pos, std::string_view state) = 0;

    // 还原 pos 的真实方块。
    virtual bool clearBlock(std::string_view player, Vec3i pos) = 0;
};

} // namespace litematicabds
