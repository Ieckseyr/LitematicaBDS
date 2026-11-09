#pragma once

// HologramLib IViewOverride 的适配层。
//
// 记录「已经发给某个玩家的假方块」，供靠近硫磺怪/点击填充逻辑复用；
// HologramLib 1.37.0+ 支持 BlockView.type = "#<networkId>"，可带完整方块状态，
// 解析失败时退回基础方块名。

#include <string>
#include <string_view>
#include <unordered_map>

#include "litematicabds/Render.h"
#include "litematicabds/Types.h"

namespace litematicabds::platform {

class HoloSink final : public IGhostSink {
public:
    bool setBlock(std::string_view player, Vec3i pos, std::string_view state) override;
    bool clearBlock(std::string_view player, Vec3i pos) override;

    [[nodiscard]] std::unordered_map<Vec3i, std::string, Vec3iHash> const* blocksFor(
        std::string_view player
    ) const;

    void eraseBlock(std::string_view player, Vec3i pos);
    void forgetPlayer(std::string_view player);

private:
    std::unordered_map<std::string, std::unordered_map<Vec3i, std::string, Vec3iHash>> mApplied;
};

} // namespace litematicabds::platform
