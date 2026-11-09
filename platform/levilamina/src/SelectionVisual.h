#pragma once

// pos1/pos2 选区可视化：HologramLib 盒子 + 悬浮字（尺寸/体积/角点，RGB 循环色）。

#include <cstdint>
#include <string>
#include <unordered_map>

#include "litematicabds/Types.h"

class Player;

namespace litematicabds::platform {

class Plugin;

class SelectionVisual {
public:
    explicit SelectionVisual(Plugin& plugin);
    ~SelectionVisual();

    SelectionVisual(SelectionVisual const&)            = delete;
    SelectionVisual& operator=(SelectionVisual const&) = delete;

    void tick(std::uint64_t tickCount);
    void forgetPlayer(std::string const& playerName);

private:
    struct Entry {
        int64_t shapeId{-1};
        int64_t textId{-1};
        Bounds  bounds{};
    };

    void updatePlayer(::Player& player);
    void createVisual(::Player& player, Bounds const& bounds);
    void destroyVisual(Entry& entry);
    void updateColor(Entry& entry, std::uint64_t tickCount);

    Plugin&                               mPlugin;
    std::unordered_map<std::string, Entry> mEntries;
};

} // namespace litematicabds::platform
