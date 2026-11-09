#include "SelectionVisual.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <hologramlib/HologramLib.h>

#include <ll/api/service/Bedrock.h>

#include <mc/world/actor/player/Player.h>
#include <mc/world/level/Level.h>

#include "Plugin.h"
#include "litematicabds/Service.h"

namespace litematicabds::platform {
namespace {

void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    h            = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    float const c = v * s;
    float const x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float const m = v - c;
    float       rr = 0, gg = 0, bb = 0;
    if (h < 60) {
        rr = c;
        gg = x;
    } else if (h < 120) {
        rr = x;
        gg = c;
    } else if (h < 180) {
        gg = c;
        bb = x;
    } else if (h < 240) {
        gg = x;
        bb = c;
    } else if (h < 300) {
        rr = x;
        bb = c;
    } else {
        rr = c;
        bb = x;
    }
    r = rr + m;
    g = gg + m;
    b = bb + m;
}

std::string formatVec(Vec3i v) {
    return "(" + std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z) + ")";
}

} // namespace

SelectionVisual::SelectionVisual(Plugin& plugin) : mPlugin(plugin) {}

SelectionVisual::~SelectionVisual() {
    for (auto& [name, entry] : mEntries) {
        (void)name;
        destroyVisual(entry);
    }
    mEntries.clear();
}

void SelectionVisual::destroyVisual(Entry& entry) {
    auto& lib = hologramlib::IHologramLib::getInstance();
    if (entry.shapeId >= 0) {
        lib.shapes().destroy(entry.shapeId);
        entry.shapeId = -1;
    }
    if (entry.textId >= 0) {
        lib.holograms().destroy(entry.textId);
        entry.textId = -1;
    }
}

void SelectionVisual::createVisual(::Player& player, Bounds const& bounds) {
    auto const& name = player.getRealName();
    auto&       lib  = hologramlib::IHologramLib::getInstance();
    auto const  dim  = static_cast<int>(player.getDimensionId());

    auto const size = bounds.size();
    auto const volume = static_cast<std::uint64_t>(size.x) * static_cast<std::uint64_t>(size.y)
                      * static_cast<std::uint64_t>(size.z);

    auto& entry    = mEntries[name];
    entry.bounds   = bounds;
    entry.shapeId  = lib.shapes().createBox(
        static_cast<float>(bounds.min.x),
        static_cast<float>(bounds.min.y),
        static_cast<float>(bounds.min.z),
        static_cast<float>(bounds.max.x + 1),
        static_cast<float>(bounds.max.y + 1),
        static_cast<float>(bounds.max.z + 1)
    );
    if (entry.shapeId >= 0) {
        lib.shapes().setColor(entry.shapeId, 0.15f, 0.85f, 1.0f, 0.85f);
        lib.shapes().setDimension(entry.shapeId, dim);
        lib.shapes().setDepthTest(entry.shapeId, false);
        lib.shapes().drawToPlayer(entry.shapeId, name);
    }

    auto const centerX = (static_cast<float>(bounds.min.x) + bounds.max.x + 1) * 0.5f;
    auto const centerZ = (static_cast<float>(bounds.min.z) + bounds.max.z + 1) * 0.5f;
    entry.textId       = lib.holograms().create(centerX, static_cast<float>(bounds.max.y) + 1.45f, centerZ);
    if (entry.textId < 0) return;

    lib.holograms().addLine(entry.textId, "选区 " + std::to_string(size.x) + " × " + std::to_string(size.y) + " × " + std::to_string(size.z));
    lib.holograms().addLine(entry.textId, "体积 " + std::to_string(volume));
    lib.holograms().addLine(entry.textId, "pos1 " + formatVec(bounds.min) + "  pos2 " + formatVec(bounds.max));
    lib.holograms().setScale(entry.textId, 1.15f);
    lib.holograms().setBackgroundColor(entry.textId, 0.0f, 0.0f, 0.0f, 0.35f);
    lib.holograms().setDepthTest(entry.textId, false);
    lib.holograms().setDimension(entry.textId, dim);
    lib.holograms().drawToPlayer(entry.textId, name);
    updateColor(entry, 0);
}

void SelectionVisual::updateColor(Entry& entry, std::uint64_t tickCount) {
    if (entry.textId < 0) return;
    float r = 1, g = 1, b = 1;
    hsvToRgb(static_cast<float>(tickCount * 6 % 360), 0.9f, 1.0f, r, g, b);
    hologramlib::IHologramLib::getInstance().holograms().setColor(entry.textId, r, g, b, 1.0f);
}

void SelectionVisual::updatePlayer(::Player& player) {
    auto const& name    = player.getRealName();
    auto        current = mPlugin.service()->selection(name);
    auto        it      = mEntries.find(name);

    if (!current) {
        if (it != mEntries.end()) {
            destroyVisual(it->second);
            mEntries.erase(it);
        }
        return;
    }

    if (it == mEntries.end()) {
        createVisual(player, *current);
        return;
    }
    if (!(it->second.bounds.min == current->min) || !(it->second.bounds.max == current->max)) {
        destroyVisual(it->second);
        mEntries.erase(it);
        createVisual(player, *current);
    }
}

void SelectionVisual::tick(std::uint64_t tickCount) {
    if (mPlugin.service() == nullptr) return;
    auto level = ll::service::getLevel();
    if (!level) return;

    level->forEachPlayer([this, tickCount](::Player& player) -> bool {
        updatePlayer(player);
        if (auto it = mEntries.find(player.getRealName()); it != mEntries.end() && tickCount % 2 == 0) {
            updateColor(it->second, tickCount);
        }
        return true;
    });
}

void SelectionVisual::forgetPlayer(std::string const& playerName) {
    auto it = mEntries.find(playerName);
    if (it == mEntries.end()) return;
    destroyVisual(it->second);
    mEntries.erase(it);
}

} // namespace litematicabds::platform
