#pragma once

// LeviLamina 原生插件入口。核心逻辑全部在 core/（与平台无关），
// 这里只做四件事：生命周期、命令注册、每 tick 喂玩家位置、把发块指令交给 HologramLib。

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <ll/api/event/ListenerBase.h>
#include <ll/api/mod/NativeMod.h>

#include "HoloSink.h"
#include "litematicabds/Service.h"

class Player;

namespace litematicabds::platform {

class SulfurFill;
class SelectionVisual;

class Plugin {
public:
    static Plugin& getInstance();

    bool load();
    bool enable();
    bool disable();

    ~Plugin();

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }
    [[nodiscard]] LitematicaService*  service() const { return mService.get(); }
    [[nodiscard]] HoloSink&           sink() { return mSink; }

    void tick();
    void onPlayerDisconnect(std::string const& playerName);

    // 木棍右键（useitem / 对方块 interact）打开菜单；左键挥动快速放置当前投影。
    bool tryOpenMenu(::Player& player);
    bool tryQuickPlace(::Player& player);
    bool tryLeftClick(::Player& player);
    [[nodiscard]] bool isProjectionAt(std::string const& playerName, Vec3i pos) const;
    [[nodiscard]] bool isBreakSuppressed(std::string const& playerName) const;

private:
    Plugin();

    void registerCommands();

    ll::mod::NativeMod&               mSelf;
    std::unique_ptr<LitematicaService> mService;
    HoloSink                           mSink;
    std::unique_ptr<SulfurFill>        mSulfur;
    std::unique_ptr<SelectionVisual>   mSelection;
    ll::event::ListenerPtr             mTickListener;
    ll::event::ListenerPtr             mDisconnectListener;
    ll::event::ListenerPtr             mUseItemListener;
    ll::event::ListenerPtr             mInteractListener;
    ll::event::ListenerPtr             mSwingListener;
    ll::event::ListenerPtr             mDestroyListener;
    ll::event::ListenerPtr             mAttackListener;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> mMenuCooldown;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> mPlaceCooldown;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> mBreakSuppress;
    std::uint64_t                      mTickCount{0};
    bool                               mEnabled{false};
};

} // namespace litematicabds::platform
