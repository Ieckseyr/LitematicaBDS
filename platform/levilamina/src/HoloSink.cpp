#include "HoloSink.h"

#include <atomic>
#include <charconv>
#include <optional>

#include <hologramlib/HologramLib.h>

#include <ll/api/mod/NativeMod.h>

#include <mc/deps/core/string/HashedString.h>
#include <mc/world/level/block/Block.h>

#include "litematicabds/BlockState.h"

namespace litematicabds::platform {

namespace {

// HologramLib 1.37.0+：BlockView.type 支持 "#<networkId>"，由调用方带完整状态。
std::optional<std::uint32_t> networkIdForState(std::string_view state) {
    auto parsed = parseBlockState(state);
    if (!parsed) return std::nullopt;

    Block::BlockStatesType states;
    for (auto const& [rawKey, value] : splitProperties(parsed->properties)) {
        auto key = rawKey;
        if (key.size() > 10 && key.rfind("minecraft:", 0) == 0) key.erase(0, 10);
        if (key.empty()) return std::nullopt;
        if (value == "true" || value == "false") {
            states.emplace_back(key, value == "true");
            continue;
        }
        int  intValue = 0;
        auto result   = std::from_chars(value.data(), value.data() + value.size(), intValue);
        if (result.ec == std::errc{} && result.ptr == value.data() + value.size()) {
            states.emplace_back(key, intValue);
        } else {
            states.emplace_back(key, value);
        }
    }
    auto block = Block::tryGetFromRegistry(HashedString{parsed->name}, states);
    if (!block) return std::nullopt;
    return static_cast<std::uint32_t>(block->mNetworkId);
}

} // namespace

bool HoloSink::setBlock(std::string_view player, Vec3i pos, std::string_view state) {
    auto&     viewOverrides = hologramlib::IHologramLib::getInstance().viewOverrides();
    hologramlib::BlockView view;
    auto playerName = std::string(player);
    auto apply      = [&](std::string const& type) {
        view.type = type;
        return viewOverrides.overrideBlock(playerName, pos.x, pos.y, pos.z, view);
    };

    // 新接口：完整状态 -> network id，HologramLib 自己维护逐玩家覆盖和 chunk 重发。
    if (auto networkId = networkIdForState(state)) {
        if (apply("#" + std::to_string(*networkId))) {
            mApplied[playerName][pos] = std::string(state);
            return true;
        }
    }

    // 回退：只发基础方块名（状态丢失，但至少方块可见）
    auto const open = state.find('[');
    auto const base = std::string(open == std::string_view::npos ? state : state.substr(0, open));
    if (apply(base)) {
        mApplied[playerName][pos] = std::string(state);
        return true;
    }

    static std::atomic<bool> warned{false};
    if (!warned.exchange(true)) {
        ll::mod::NativeMod::current()->getLogger().warn(
            "[render] HologramLib 拒绝方块覆盖: player={} state={} @ {}, {}, {}",
            playerName,
            state,
            pos.x,
            pos.y,
            pos.z
        );
    }
    return false;
}

bool HoloSink::clearBlock(std::string_view player, Vec3i pos) {
    auto name = std::string(player);
    if (auto it = mApplied.find(name); it != mApplied.end()) {
        it->second.erase(pos);
        if (it->second.empty()) mApplied.erase(it);
    }
    return hologramlib::IHologramLib::getInstance().viewOverrides().clearBlock(name, pos.x, pos.y, pos.z);
}

std::unordered_map<Vec3i, std::string, Vec3iHash> const* HoloSink::blocksFor(std::string_view player) const {
    auto it = mApplied.find(std::string(player));
    return it == mApplied.end() ? nullptr : &it->second;
}

void HoloSink::eraseBlock(std::string_view player, Vec3i pos) {
    auto it = mApplied.find(std::string(player));
    if (it == mApplied.end()) return;
    it->second.erase(pos);
    if (it->second.empty()) mApplied.erase(it);
}

void HoloSink::forgetPlayer(std::string_view player) { mApplied.erase(std::string(player)); }

} // namespace litematicabds::platform
