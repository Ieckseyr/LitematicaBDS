#include "SulfurFill.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <utility>

#include <ll/api/service/Bedrock.h>

#include <mc/deps/core/string/HashedString.h>
#include <mc/world/actor/player/Inventory.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/level/BlockSource.h>
#include <mc/world/level/Level.h>
#include <mc/world/level/GameType.h>
#include <mc/world/level/block/Block.h>

#include "HoloSink.h"
#include "Plugin.h"
#include "litematicabds/BlockState.h"
#include "litematicabds/Service.h"

namespace litematicabds::platform {
namespace {

std::string baseType(std::string_view state) {
    auto const open = state.find('[');
    return std::string(open == std::string_view::npos ? state : state.substr(0, open));
}

// 硫磺怪主手拿的是“物品”，不是“方块”。基岩有些方块名与物品名不同（草方块最典型）。
std::string itemIdForBlock(std::string_view block) {
    static const std::unordered_map<std::string_view, std::string_view> kAliases = {
        {"minecraft:grass_block", "minecraft:grass"},
        {"minecraft:lit_furnace", "minecraft:furnace"},
        {"minecraft:unpowered_repeater", "minecraft:repeater"},
        {"minecraft:powered_repeater", "minecraft:repeater"},
        {"minecraft:unpowered_comparator", "minecraft:comparator"},
        {"minecraft:powered_comparator", "minecraft:comparator"},
        {"minecraft:redstone_wire", "minecraft:redstone"},
        {"minecraft:wall_torch", "minecraft:torch"},
        {"minecraft:soul_wall_torch", "minecraft:soul_torch"},
        {"minecraft:redstone_wall_torch", "minecraft:redstone_torch"},
        {"minecraft:standing_sign", "minecraft:sign"},
        {"minecraft:wall_sign", "minecraft:sign"},
        {"minecraft:flowing_water", "minecraft:water_bucket"},
        {"minecraft:flowing_lava", "minecraft:lava_bucket"},
    };
    std::string name(block);
    if (auto it = kAliases.find(name); it != kAliases.end()) return std::string(it->second);
    if (name.size() > 10 && name.substr(name.size() - 10) == "_wall_sign") {
        name.replace(name.size() - 10, 10, "_sign");
    } else if (name.size() > 13 && name.substr(name.size() - 13) == "_double_slab") {
        name.replace(name.size() - 13, 13, "_slab");
    }
    return name;
}

std::string displayItemIdForBlock(std::string_view block) {
    if (block == "minecraft:grass_block") return "minecraft:grass_block";
    return itemIdForBlock(block);
}

std::optional<float> yawForState(std::string const& state) {
    auto parsed = parseBlockState(state);
    if (!parsed) return std::nullopt;
    auto props = splitProperties(parsed->properties);

    auto value = [&](std::string_view key) -> std::optional<std::string> {
        for (auto const& [k, v] : props) {
            if (k == key) return v;
        }
        return std::nullopt;
    };
    auto yawOf = [](std::string_view facing) -> std::optional<float> {
        if (facing == "north") return 180.0f;
        if (facing == "south") return 0.0f;
        if (facing == "west") return 90.0f;
        if (facing == "east") return 270.0f;
        return std::nullopt;
    };
    auto dirToYaw = [](int dir) -> float {
        switch (dir) {
        case 0:
            return 270.0f; // east
        case 1:
            return 90.0f; // west
        case 2:
            return 0.0f; // south
        default:
            return 180.0f; // north
        }
    };

    for (auto key : {"facing", "cardinal_direction"}) {
        if (auto v = value(key)) return yawOf(*v);
    }
    for (auto key : {"weirdo_direction", "direction"}) {
        if (auto v = value(key)) {
            int dir = 0;
            std::from_chars(v->data(), v->data() + v->size(), dir);
            return dirToYaw(dir);
        }
    }
    if (auto v = value("facing_direction")) {
        int dir = 0;
        std::from_chars(v->data(), v->data() + v->size(), dir);
        if (dir == 2) return 180.0f;
        if (dir == 3) return 0.0f;
        if (dir == 4) return 90.0f;
        if (dir == 5) return 270.0f;
    }
    return std::nullopt;
}

bool buildStates(std::string_view properties, Block::BlockStatesType& out) {    for (auto const& [key, value] : splitProperties(properties)) {
        if (key.empty()) return false;
        if (value == "true" || value == "false") {
            out.emplace_back(key, value == "true");
            continue;
        }
        int  intValue = 0;
        auto result   = std::from_chars(value.data(), value.data() + value.size(), intValue);
        if (result.ec == std::errc{} && result.ptr == value.data() + value.size()) {
            out.emplace_back(key, intValue);
            continue;
        }
        out.emplace_back(key, value);
    }
    return true;
}

// 只有方块类投影才变硫磺怪；花草/火把/铁轨/红石线等非方块只画框、不生成硫磺载体。
bool isSolidBlockState(std::string const& state) {
    auto const base = baseType(state);
    if (base == "minecraft:air" || base == "minecraft:grass") return false;
    if (base == "minecraft:mushroom_block" || base == "minecraft:brown_mushroom_block"
        || base == "minecraft:red_mushroom_block") {
        return true;
    }
    static constexpr std::string_view kNonBlockParts[] = {
        "water",       "lava",          "short_grass",   "tall_grass",
        "fern",        "flower",       "sapling",       "wheat",         "carrot",
        "potato",      "beetroot",     "torch",         "lantern",       "rail",
        "button",      "lever",        "pressure_plate", "sign",         "banner",
        "vine",        "lily",         "crop",          "seed",          "redstone_wire",
        "repeater",    "comparator",   "snow_layer",    "carpet",        "candle",
        "cake",        "mushroom",     "roots",         "sprouts",       "bud",
        "dandelion",   "poppy",        "orchid",        "allium",        "cornflower",
        "tulip",       "sunflower",    "lilac",         "rose_bush",     "peony",
        "azure_bluet", "oxeye_daisy",  "sugar_cane",    "kelp",          "seagrass",
        "dead_bush",   "bamboo",       "cocoa",         "stem",          "wart",
        "dripleaf",    "azalea",
    };
    for (auto part : kNonBlockParts) {
        if (base.find(part) != std::string::npos) return false;
    }
    return true;
}

::Player* findPlayer(std::string const& name) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ::Player* found = nullptr;
    level->forEachPlayer([&](::Player& player) -> bool {
        if (player.getRealName() == name) {
            found = &player;
            return false;
        }
        return true;
    });
    return found;
}

double distanceSquared(::Player& player, Vec3i pos) {
    auto const  p = player.getFeetPos();
    double const dx = p.x - (pos.x + 0.5);
    double const dy = p.y - (pos.y + 0.5);
    double const dz = p.z - (pos.z + 0.5);
    return dx * dx + dy * dy + dz * dz;
}

} // namespace

SulfurFill::SulfurFill(Plugin& plugin) : mPlugin(plugin) {}

SulfurFill::~SulfurFill() { stop(); }

void SulfurFill::start() {
    if (mListenerToken != 0) return;
    mListenerToken = hologramlib::IHologramLib::getInstance().addGhostInteractListener(
        [this](hologramlib::GhostInteractEvent const& event) {
            std::lock_guard lock(mEventMutex);
            if (mEvents.size() < 256) mEvents.push_back(event);
        }
    );
}

void SulfurFill::stop() {
    if (mListenerToken != 0) {
        hologramlib::IHologramLib::getInstance().removeGhostInteractListener(mListenerToken);
        mListenerToken = 0;
    }
    for (auto& [player, entries] : mActive) {
        for (auto& [pos, active] : entries) {
            (void)pos;
            hologramlib::IHologramLib::getInstance().sulfurDisplays().destroy(active.displayId);
            mByEntityId.erase(active.entityId);
            (void)player;
        }
    }
    mActive.clear();
    for (auto& [player, boxes] : mBoxes) {
        (void)player;
        for (auto const& [pos, shapeId] : boxes) {
            (void)pos;
            hologramlib::IHologramLib::getInstance().shapes().destroy(shapeId);
        }
    }
    mBoxes.clear();
    mByEntityId.clear();
    mInventories.clear();
    std::lock_guard lock(mEventMutex);
    mEvents.clear();
}

bool SulfurFill::tryFillLook(::Player& player, std::uint64_t tickCount) {
    auto const playerName = player.getRealName();
    auto const* blocks    = mPlugin.sink().blocksFor(playerName);
    if (blocks == nullptr) return false;

    auto  eye = player.getFeetPos();
    eye.y += 1.62f;
    auto const dir = player.getViewVector(1.0f);

    for (float t = 0.1f; t <= 6.0f; t += 0.1f) {
        Vec3i const pos{
            static_cast<int>(std::floor(eye.x + dir.x * t)),
            static_cast<int>(std::floor(eye.y + dir.y * t)),
            static_cast<int>(std::floor(eye.z + dir.z * t)),
        };
        auto const it = blocks->find(pos);
        if (it == blocks->end()) continue;

        // 手持物必须对应这块投影；木棍走“快速放置投影”，不在这里吞掉。
        auto const targetItem = itemIdForBlock(baseType(it->second));
        auto const held       = player.getCarriedItem();
        if (held.isNull() || held.getTypeName() != targetItem) continue;

        Active target;
        target.pos       = pos;
        target.state     = it->second;
        target.dimension = static_cast<int>(player.getDimensionId());
        // 点到投影方块即尝试填充；即使失败也吞掉这次左键，避免又触发“快速放置投影”。
        bool const placed = placeBlock(player, playerName, target, tickCount);
        if (placed) destroyBox(playerName, pos);
        return true;
    }
    return false;
}

bool SulfurFill::hasActiveNear(::Player& player) const {
    auto it = mActive.find(player.getRealName());
    if (it == mActive.end()) return false;
    auto const feet = player.getFeetPos();
    auto const view = player.getViewVector(1.0f);
    int const dim   = static_cast<int>(player.getDimensionId());
    for (auto const& [pos, active] : it->second) {
        if (active.dimension != dim) continue;
        double const dx = (pos.x + 0.5) - feet.x;
        double const dy = (pos.y + 0.5) - feet.y;
        double const dz = (pos.z + 0.5) - feet.z;
        double const d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 36.0 || d2 < 1e-6) continue;
        double const inv = 1.0 / std::sqrt(d2);
        double const dot = dx * inv * view.x + dy * inv * view.y + dz * inv * view.z;
        if (dot > 0.35) return true;
    }
    return false;
}

void SulfurFill::forgetPlayer(std::string const& playerName) {
    clearPlayer(playerName, false);
    mInventories.erase(playerName);
}

void SulfurFill::clearPlayer(std::string const& playerName, bool restoreFakeBlock) {
    auto it = mActive.find(playerName);
    if (it == mActive.end()) return;
    auto* blocks = mPlugin.sink().blocksFor(playerName);
    for (auto& [pos, active] : it->second) {
        (void)pos;
        hologramlib::IHologramLib::getInstance().sulfurDisplays().destroy(active.displayId);
        mByEntityId.erase(active.entityId);
        if (restoreFakeBlock && blocks != nullptr && blocks->contains(active.pos)) {
            mPlugin.sink().setBlock(playerName, active.pos, active.state);
        } else {
            hologramlib::IHologramLib::getInstance().viewOverrides().clearBlock(
                playerName,
                active.pos.x,
                active.pos.y,
                active.pos.z
            );
        }
    }
    mActive.erase(it);
    clearBoxes(playerName);
}

void SulfurFill::createDisplay(::Player& player, Vec3i pos, std::string const& state, int dimension) {
    auto const playerName = player.getRealName();
    auto&      lib        = hologramlib::IHologramLib::getInstance();

    // 方块位置改成硫磺怪：隐藏假方块，主手物品用对应物品 ID 表示方块。
    hologramlib::BlockView air;
    air.type = "minecraft:air";
    lib.viewOverrides().overrideBlock(playerName, pos.x, pos.y, pos.z, air);

    hologramlib::SulfurDisplaySpec spec;
    // 实体脚在方块底面，x/z 取方块中心。
    spec.x            = static_cast<float>(pos.x) + 0.5f;
    spec.y            = static_cast<float>(pos.y);
    spec.z            = static_cast<float>(pos.z) + 0.5f;
    spec.dim          = dimension;
    spec.block.type   = displayItemIdForBlock(baseType(state));
    spec.block.count  = 1;
    spec.invisible    = !mPlugin.service()->config().sulfur.visible;
    spec.scale        = 1.0f;
    spec.viewDistance = 64.0;
    spec.visiblePlayers = {playerName};

    auto const displayId = lib.sulfurDisplays().create(spec);
    if (displayId < 0) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true)) {
            mPlugin.getSelf().getLogger().warn(
                "[sulfur] 创建硫磺怪失败: player={} state={}",
                playerName,
                state
            );
        }
        mPlugin.sink().setBlock(playerName, pos, state);
        return;
    }
    auto const entityId = lib.sulfurDisplays().entityIdOf(displayId);
    if (mPlugin.service()->config().sulfur.faceBlock) {
        if (auto yaw = yawForState(state)) {
            lib.customEntities().setPlayerRotation(entityId, playerName, *yaw, 0.0f);
        }
    }
    mActive[playerName][pos] = Active{displayId, entityId, -1, pos, state, dimension};
    if (entityId > 0) mByEntityId[entityId] = {playerName, pos};
}

void SulfurFill::ensureBox(std::string const& playerName, Vec3i pos, int dimension) {
    auto& boxes = mBoxes[playerName];
    if (boxes.contains(pos)) return;
    auto& lib = hologramlib::IHologramLib::getInstance();
    int64_t shapeId = lib.shapes().createBox(
        static_cast<float>(pos.x),
        static_cast<float>(pos.y),
        static_cast<float>(pos.z),
        static_cast<float>(pos.x + 1),
        static_cast<float>(pos.y + 1),
        static_cast<float>(pos.z + 1)
    );
    if (shapeId < 0) return;
    lib.shapes().setColor(shapeId, 1.0f, 0.75f, 0.15f, 0.9f);
    lib.shapes().setDimension(shapeId, dimension);
    lib.shapes().setDepthTest(shapeId, false);
    lib.shapes().drawToPlayer(shapeId, playerName);
    boxes[pos] = shapeId;
}

void SulfurFill::destroyBox(std::string const& playerName, Vec3i pos) {
    auto it = mBoxes.find(playerName);
    if (it == mBoxes.end()) return;
    auto boxIt = it->second.find(pos);
    if (boxIt == it->second.end()) return;
    hologramlib::IHologramLib::getInstance().shapes().destroy(boxIt->second);
    it->second.erase(boxIt);
    if (it->second.empty()) mBoxes.erase(it);
}

void SulfurFill::clearBoxes(std::string const& playerName) {
    auto it = mBoxes.find(playerName);
    if (it == mBoxes.end()) return;
    for (auto const& [pos, shapeId] : it->second) {
        (void)pos;
        hologramlib::IHologramLib::getInstance().shapes().destroy(shapeId);
    }
    mBoxes.erase(it);
}

void SulfurFill::destroyDisplay(std::string const& playerName, Active const& active, bool restoreFakeBlock) {
    auto& lib = hologramlib::IHologramLib::getInstance();
    lib.sulfurDisplays().destroy(active.displayId);
    mByEntityId.erase(active.entityId);
    if (auto it = mActive.find(playerName); it != mActive.end()) {
        it->second.erase(active.pos);
        if (it->second.empty()) mActive.erase(it);
    }
    if (restoreFakeBlock) {
        mPlugin.sink().setBlock(playerName, active.pos, active.state);
    }
}

void SulfurFill::refreshInventory(::Player& player, InventoryCache& cache, std::uint64_t tickCount) {
    cache.slots.clear();
    cache.counts.clear();
    auto& inventory = player.getInventory();
    int const size  = inventory.getContainerSize();
    for (int slot = 0; slot < size; ++slot) {
        auto const& item = inventory.getItem(slot);
        if (item.isNull() || item.mCount <= 0) continue;
        auto type = item.getTypeName();
        cache.slots[type].push_back(slot);
        cache.counts[type] += item.mCount;
    }
    cache.refreshedAt = tickCount;
}

int SulfurFill::takeCachedSlot(
    ::Player&          player,
    std::string const& playerName,
    std::string const& itemType,
    std::uint64_t      tickCount,
    ItemStack&         refund
) {
    auto& cache = mInventories[playerName];
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (cache.refreshedAt == 0 || tickCount - cache.refreshedAt > 100) {
            refreshInventory(player, cache, tickCount);
        }
        auto it = cache.slots.find(itemType);
        if (it == cache.slots.end()) return -1;
        while (!it->second.empty()) {
            int const slot       = it->second.back();
            auto const& item     = player.getInventory().getItem(slot);
            if (!item.isNull() && item.getTypeName() == itemType && item.mCount > 0) {
                int const before = item.mCount;
                refund = item;
                player.getInventory().removeItem(slot, 1);
                auto const& after = player.getInventory().getItem(slot);
                bool const removed = after.isNull() || after.getTypeName() != itemType || after.mCount < before;
                if (!removed) {
                    it->second.pop_back();
                    continue;
                }
                if (after.isNull() || after.getTypeName() != itemType || after.mCount <= 0) {
                    it->second.pop_back();
                }
                if (auto countIt = cache.counts.find(itemType); countIt != cache.counts.end()) {
                    if (--countIt->second <= 0) cache.counts.erase(countIt);
                }
                return slot;
            }
            it->second.pop_back();
        }
        refreshInventory(player, cache, tickCount);
    }
    return -1;
}

bool SulfurFill::placeBlock(
    ::Player&          player,
    std::string const& playerName,
    Active const&      active,
    std::uint64_t      tickCount
) {
    auto& config = mPlugin.service()->config();
    if (!config.fill.enabled) {
        player.sendMessage("§c填充功能已在配置里关闭（fill.enabled）");
        return false;
    }
    if (config.fill.requireOp && static_cast<int>(player.getCommandPermissionLevel()) < 1) {
        player.sendMessage("§c填充需要 OP 权限");
        return false;
    }

    auto const targetBlock = baseType(active.state);
    auto const targetItem  = itemIdForBlock(targetBlock);
    auto const held        = player.getCarriedItem();
    bool const heldMatches = !held.isNull() && held.getTypeName() == targetItem;
    bool const quick = !heldMatches && !held.isNull() && held.getTypeName() == "minecraft:stick";
    if (!heldMatches && !quick) {
        player.sendMessage("§e请手持 §f" + targetItem + " §e或木棍（快速填充）再点击");
        return false;
    }
    if (quick) {
        if (!config.fill.quickFill) {
            player.sendMessage("§c快速填充未启用（fill.quickFill）");
            return false;
        }
        if (static_cast<int>(player.getCommandPermissionLevel()) < 1) {
            player.sendMessage("§c快速填充仅限管理员");
            return false;
        }
    }

    auto parsed = parseBlockState(active.state);
    if (!parsed) return false;
    Block::BlockStatesType states;
    buildStates(parsed->properties, states);
    auto block = Block::tryGetFromRegistry(HashedString{parsed->name}, states);
    if (!block) {
        player.sendMessage("§c这个方块状态在当前 BDS 无法解析: " + active.state);
        return false;
    }

    auto const& current = player.getDimensionBlockSource().getBlock(::BlockPos{active.pos.x, active.pos.y, active.pos.z});
    if (!current.isAir()) {
        player.sendMessage("§e该位置已经有方块了");
        return false;
    }

    auto&      inventory = player.getInventory();
    int        slot      = -1;
    ItemStack  refund{};
    bool       deducted  = false;

    // 先扣除，再填充。
    if (quick) {
        slot = takeCachedSlot(player, playerName, targetItem, tickCount, refund);
        if (slot < 0) {
            player.sendMessage("§c背包里没有 §f" + targetItem + " §c可扣除");
            return false;
        }
        deducted = true;
    } else if (config.fill.consumeItem && player.getPlayerGameType() != GameType::Creative) {
        slot = player.getSelectedItemSlot();
        if (slot < 0) return false;
        auto const& item = inventory.getItem(slot);
        if (item.isNull() || item.getTypeName() != targetItem || item.mCount <= 0) {
            player.sendMessage("§c手上没有 §f" + targetItem);
            return false;
        }
        int const before = item.mCount;
        refund = item;
        inventory.removeItem(slot, 1);
        auto const& after = inventory.getItem(slot);
        if (!after.isNull() && after.getTypeName() == targetItem && after.mCount >= before) {
            player.sendMessage("§c扣除手持方块失败");
            return false;
        }
        deducted = true;
    }

    auto& source = player.getDimensionBlockSource();
    if (!source.setBlockAndRetainCompatibleBlockActor(
            ::BlockPos{active.pos.x, active.pos.y, active.pos.z},
            *block,
            3
        )) {
        if (deducted) inventory.add(refund);
        player.sendMessage("§c放置失败（服务端拒绝）");
        return false;
    }

    // 清掉客户端方块覆盖，显示刚放下的真实方块。
    hologramlib::IHologramLib::getInstance().viewOverrides().clearBlock(
        playerName,
        active.pos.x,
        active.pos.y,
        active.pos.z
    );
    mPlugin.sink().eraseBlock(playerName, active.pos);
    if (auto* service = mPlugin.service()) service->markBlockPlaced(playerName, active.pos);

    player.sendMessage("§a已放置 §f" + targetBlock + (quick ? " §7（快速填充，已扣除）" : ""));
    return true;
}

void SulfurFill::handleInteraction(hologramlib::GhostInteractEvent const& event, std::uint64_t tickCount) {
    // 左键 Attack=2；右键（1）留给普通交互。
    if (event.domain != "entity" || event.action != 2) return;
    auto it = mByEntityId.find(event.id);
    if (it == mByEntityId.end()) return;

    auto const& [playerName, pos] = it->second;
    auto*        player           = findPlayer(playerName);
    if (player == nullptr) return;

    auto pit = mActive.find(playerName);
    if (pit == mActive.end()) return;
    auto ait = pit->second.find(pos);
    if (ait == pit->second.end()) return;

    auto active = ait->second;
    if (placeBlock(*player, playerName, active, tickCount)) {
        destroyBox(playerName, active.pos);
        hologramlib::IHologramLib::getInstance().sulfurDisplays().destroy(active.displayId);
        mByEntityId.erase(active.entityId);
        pit->second.erase(ait);
        if (pit->second.empty()) mActive.erase(pit);
    }
}

void SulfurFill::reconcilePlayer(::Player& player) {
    auto* service = mPlugin.service();
    if (service == nullptr) return;

    auto const& config    = service->config();
    auto const  playerName = player.getRealName();
    if (!config.sulfur.enabled) {
        clearPlayer(playerName, true);
        return;
    }

    auto const* blocks = mPlugin.sink().blocksFor(playerName);
    if (blocks == nullptr) {
        clearPlayer(playerName, false);
        return;
    }

    double const radius2 = static_cast<double>(config.sulfur.radius) * config.sulfur.radius;
    struct Candidate {
        Vec3i       pos;
        std::string state;
        double      distance2;
        bool        solid;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(blocks->size());
    for (auto const& [pos, state] : *blocks) {
        double const d2 = distanceSquared(player, pos);
        if (d2 <= radius2) candidates.push_back({pos, state, d2, isSolidBlockState(state)});
    }
    auto const byDistance = [](Candidate const& a, Candidate const& b) { return a.distance2 < b.distance2; };
    auto const maxCount   = static_cast<std::size_t>(std::max(1, config.sulfur.maxDisplays));
    if (candidates.size() > maxCount) {
        std::nth_element(
            candidates.begin(),
            candidates.begin() + static_cast<std::ptrdiff_t>(maxCount),
            candidates.end(),
            byDistance
        );
        candidates.resize(maxCount);
    }
    std::sort(candidates.begin(), candidates.end(), byDistance);

    std::unordered_map<Vec3i, bool, Vec3iHash> wanted;
    wanted.reserve(candidates.size() * 2 + 8);
    for (auto const& candidate : candidates) {
        wanted[candidate.pos] = candidate.solid;
        ensureBox(playerName, candidate.pos, static_cast<int>(player.getDimensionId()));
    }
    if (auto boxIt = mBoxes.find(playerName); boxIt != mBoxes.end()) {
        std::vector<Vec3i> stale;
        for (auto const& [pos, shapeId] : boxIt->second) {
            (void)shapeId;
            if (!wanted.contains(pos)) stale.push_back(pos);
        }
        for (auto const& pos : stale) destroyBox(playerName, pos);
    }

    std::vector<Active> toRemove;
    if (auto it = mActive.find(playerName); it != mActive.end()) {
        for (auto const& [pos, entry] : it->second) {
            auto const wantedIt = wanted.find(pos);
            if (wantedIt == wanted.end() || !wantedIt->second) toRemove.push_back(entry);
        }
    }
    for (auto const& entry : toRemove) {
        bool const restore = blocks->contains(entry.pos);
        destroyDisplay(playerName, entry, restore);
        if (!restore) {
            hologramlib::IHologramLib::getInstance().viewOverrides().clearBlock(
                playerName,
                entry.pos.x,
                entry.pos.y,
                entry.pos.z
            );
        }
    }

    // 核心重规划时可能重新发过假方块；对活跃硫磺格补一次 air 覆盖，避免方块盖住硫磺怪。
    if (auto it = mActive.find(playerName); it != mActive.end()) {
        hologramlib::BlockView air;
        air.type = "minecraft:air";
        auto& views = hologramlib::IHologramLib::getInstance().viewOverrides();
        for (auto const& [pos, entry] : it->second) {
            (void)entry;
            views.overrideBlock(playerName, pos.x, pos.y, pos.z, air);
        }
    }

    int const dimension = static_cast<int>(player.getDimensionId());
    for (auto const& candidate : candidates) {
        if (!candidate.solid) continue;
        auto it = mActive.find(playerName);
        if (it != mActive.end() && it->second.contains(candidate.pos)) continue;
        createDisplay(player, candidate.pos, candidate.state, dimension);
    }
}

void SulfurFill::tick(std::uint64_t tickCount) {
    if (tickCount % 4 != 0) return;

    std::vector<hologramlib::GhostInteractEvent> events;
    {
        std::lock_guard lock(mEventMutex);
        events.swap(mEvents);
    }
    for (auto const& event : events) handleInteraction(event, tickCount);

    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([this](::Player& player) -> bool {
        reconcilePlayer(player);
        return true;
    });
}

} // namespace litematicabds::platform
