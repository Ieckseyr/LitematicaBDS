#pragma once

// 靠近投影方块 → 换成硫磺立方体（可见/隐身可配）；手持对应方块点击立方体 →
// 在投影位置真实放置方块，并把该格从投影里移除。
//
// 交互事件来自 HologramLib 的 GhostInteractEvent（自定义实体 domain="entity"），
// 事件在收包线程入队，统一由 tick 在主线程处理。

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <hologramlib/HologramLib.h>

#include <mc/world/item/ItemStack.h>

#include "litematicabds/Types.h"

class Player;

namespace litematicabds::platform {

class Plugin;

class SulfurFill {
public:
    explicit SulfurFill(Plugin& plugin);
    ~SulfurFill();

    SulfurFill(SulfurFill const&)            = delete;
    SulfurFill& operator=(SulfurFill const&) = delete;

    void start();
    void stop();
    void tick(std::uint64_t tickCount);
    void forgetPlayer(std::string const& playerName);

    // 玩家视线/近距离是否有自己的硫磺怪（用于抑制左键“快速放置投影”）。
    [[nodiscard]] bool hasActiveNear(::Player& player) const;

    // 左键直接点投影方块（不要求先变成硫磺怪）。
    bool tryFillLook(::Player& player, std::uint64_t tickCount);

private:
    struct Active {
        int64_t     displayId{-1};
        int64_t     entityId{-1};
        int64_t     shapeId{-1};
        Vec3i       pos;
        std::string state;
        int         dimension{0};
    };

    struct InventoryCache {
        std::unordered_map<std::string, std::vector<int>> slots;
        std::unordered_map<std::string, int>              counts;
        std::uint64_t                                     refreshedAt{0};
    };

    void createDisplay(::Player& player, Vec3i pos, std::string const& state, int dimension);
    void destroyDisplay(std::string const& playerName, Active const& active, bool restoreFakeBlock);
    void handleInteraction(hologramlib::GhostInteractEvent const& event, std::uint64_t tickCount);
    bool placeBlock(::Player& player, std::string const& playerName, Active const& active, std::uint64_t tickCount);
    void reconcilePlayer(::Player& player);
    void clearPlayer(std::string const& playerName, bool restoreFakeBlock);
    void ensureBox(std::string const& playerName, Vec3i pos, int dimension);
    void destroyBox(std::string const& playerName, Vec3i pos);
    void clearBoxes(std::string const& playerName);
    void refreshInventory(::Player& player, InventoryCache& cache, std::uint64_t tickCount);
    int  takeCachedSlot(
        ::Player&          player,
        std::string const& playerName,
        std::string const& itemType,
        std::uint64_t      tickCount,
        ItemStack&         refund
    );

    Plugin& mPlugin;

    std::mutex                                 mEventMutex;
    std::vector<hologramlib::GhostInteractEvent> mEvents;
    std::uint64_t                              mListenerToken{0};

    std::unordered_map<std::string, std::unordered_map<Vec3i, Active, Vec3iHash>> mActive;
    std::unordered_map<std::string, std::unordered_map<Vec3i, int64_t, Vec3iHash>> mBoxes;
    std::unordered_map<std::int64_t, std::pair<std::string, Vec3i>>               mByEntityId;
    std::unordered_map<std::string, InventoryCache>                              mInventories;
};

} // namespace litematicabds::platform
