#include "Plugin.h"

#include "HoloSink.h"
#include "SelectionVisual.h"
#include "SulfurFill.h"
#include "Menu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <hologramlib/HologramLib.h>

#include <ll/api/event/EventBus.h>
#include <ll/api/event/player/PlayerAttackEvent.h>
#include <ll/api/event/player/PlayerDestroyBlockEvent.h>
#include <ll/api/event/player/PlayerDisconnectEvent.h>
#include <ll/api/event/player/PlayerInteractBlockEvent.h>
#include <ll/api/event/player/PlayerSwingEvent.h>
#include <ll/api/event/player/PlayerUseItemEvent.h>
#include <ll/api/event/world/ServerLevelTickEvent.h>
#include <ll/api/mod/RegisterHelper.h>
#include <ll/api/service/Bedrock.h>

#include <mc/deps/core/string/HashedString.h>
#include <mc/deps/nbt/ByteTag.h>
#include <mc/deps/nbt/CompoundTag.h>
#include <mc/deps/nbt/DoubleTag.h>
#include <mc/deps/nbt/FloatTag.h>
#include <mc/deps/nbt/Int64Tag.h>
#include <mc/deps/nbt/IntTag.h>
#include <mc/deps/nbt/ShortTag.h>
#include <mc/deps/nbt/StringTag.h>
#include <mc/deps/nbt/Tag.h>
#include <mc/world/level/Level.h>
#include <mc/world/item/ItemStack.h>
#include <mc/world/level/BlockSource.h>
#include <mc/world/level/block/Block.h>

#include "litematicabds/BlockState.h"
#include "litematicabds/Version.h"

namespace litematicabds::platform {
namespace {

using hologramlib::IHologramLib;

// 状态串 -> BlockStatesType，仅用于校验。
bool buildStates(std::string_view properties, Block::BlockStatesType& out) {
    for (auto const& [key, value] : splitProperties(properties)) {
        if (key.empty()) return false;
        if (value == "true" || value == "false") {
            out.emplace_back(key, value == "true");
            continue;
        }
        if (!value.empty()) {
            int  intValue = 0;
            auto result   = std::from_chars(value.data(), value.data() + value.size(), intValue);
            if (result.ec == std::errc{} && result.ptr == value.data() + value.size()) {
                out.emplace_back(key, static_cast<int>(intValue));
                continue;
            }
            char* end        = nullptr;
            double floatValue = std::strtod(std::string(value).c_str(), &end);
            if (end != nullptr && *end == '\0') {
                out.emplace_back(key, static_cast<float>(floatValue));
                continue;
            }
        }
        out.emplace_back(key, value);
    }
    return true;
}

// 用 BDS 方块注册表校验状态串。无法解析返回 nullopt，核心会降级到基础方块名。
std::optional<std::string> validateBlockState(std::string_view state) {
    auto parsed = parseBlockState(state);
    if (!parsed || parsed->name.empty()) return std::nullopt;

    Block::BlockStatesType states;
    if (!buildStates(parsed->properties, states)) return std::nullopt;

    auto block = Block::tryGetFromRegistry(HashedString{parsed->name}, states);
    if (!block) return std::nullopt;
    return std::string(state);
}


std::string tagToStateValue(Tag const& tag) {
    switch (tag.getId()) {
    case Tag::Type::String:
        return static_cast<StringTag const&>(tag);
    case Tag::Type::Byte:
        return std::to_string(static_cast<int>(static_cast<ByteTag const&>(tag).data));
    case Tag::Type::Short:
        return std::to_string(static_cast<int>(static_cast<ShortTag const&>(tag).data));
    case Tag::Type::Int:
        return std::to_string(static_cast<IntTag const&>(tag).data);
    case Tag::Type::Int64:
        return std::to_string(static_cast<Int64Tag const&>(tag).data);
    case Tag::Type::Float:
        return std::to_string(static_cast<FloatTag const&>(tag).data);
    case Tag::Type::Double:
        return std::to_string(static_cast<DoubleTag const&>(tag).data);
    default:
        return {};
    }
}

// 方块序列化 id -> "minecraft:name[key=value,...]"。
std::optional<BlockState> serializeBlockState(Block const& block) {
    auto const& serialization = block.mSerializationId.get();
    auto const* nameTag       = serialization.get("name");
    if (nameTag == nullptr || nameTag->getId() != Tag::Type::String) return std::nullopt;

    std::string name = static_cast<StringTag const&>(*nameTag);
    if (name.empty()) return std::nullopt;

    std::string properties;
    if (auto const* statesTag = serialization.get("states");
        statesTag != nullptr && statesTag->getId() == Tag::Type::Compound) {
        auto const& states = static_cast<CompoundTag const&>(*statesTag);
        for (auto const& [key, variant] : states.mTags) {
            auto const  value = tagToStateValue(variant.get());
            if (value.empty()) continue;
            std::string shortKey(key);
            constexpr std::string_view kPrefix = "minecraft:";
            if (shortKey.starts_with(kPrefix)) shortKey.erase(0, kPrefix.size());
            if (!properties.empty()) properties.push_back(',');
            properties += shortKey;
            properties.push_back('=');
            properties += value;
        }
    }

    return BlockState{name, canonicalProperties(properties)};
}

std::optional<BlockState> sampleBlock(std::string_view playerName, Vec3i pos, int dimension) {
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;

    ::Player* target = nullptr;
    level->forEachPlayer([&](::Player& player) -> bool {
        if (player.getRealName() == playerName) {
            target = &player;
            return false;
        }
        return true;
    });
    if (target == nullptr || static_cast<int>(target->getDimensionId()) != dimension) return std::nullopt;

    auto const& block = target->getDimensionBlockSource().getBlock(::BlockPos{pos.x, pos.y, pos.z});
    if (block.isAir()) return std::nullopt;

    if (auto state = serializeBlockState(block)) return state;
    return BlockState{block.getTypeName(), ""};
}

} // namespace

Plugin::Plugin() : mSelf(*ll::mod::NativeMod::current()) {}

Plugin& Plugin::getInstance() {
    static Plugin instance;
    return instance;
}

Plugin::~Plugin() = default;

bool Plugin::load() {
    getSelf().getLogger().info("LitematicaBDS {} loading (HologramLib ABI 0x{:06X})", LITEMATICABDS_VERSION, HOLOGLIB_API_VERSION);
    return true;
}

bool Plugin::enable() {
    auto& logger = getSelf().getLogger();

    auto const modDir = getSelf().getModDir();
    logger.info("LitematicaBDS 数据目录: {}", modDir.string());

    mService = std::make_unique<LitematicaService>(LitematicaService::Options{modDir, {}, {}, {}});
    mService->setLogger([this](LogLevel level, std::string_view message) {
        auto& log = getSelf().getLogger();
        switch (level) {
        case LogLevel::Debug: log.debug("{}", message); break;
        case LogLevel::Info:  log.info("{}", message); break;
        case LogLevel::Warn:  log.warn("{}", message); break;
        case LogLevel::Error: log.error("{}", message); break;
        default:              log.info("{}", message); break;
        }
    });
    mService->setStateValidator(validateBlockState);
    mService->setBlockSampler(sampleBlock);

    auto initialized = mService->initialize();
    if (!initialized.ok()) {
        logger.error("LitematicaBDS 初始化失败: {}", initialized.error().message);
        mService.reset();
        return false;
    }

    registerCommands();

    mTickListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::world::ServerLevelTickEvent>(
        [](ll::event::world::ServerLevelTickEvent&) { Plugin::getInstance().tick(); }
    );
    mDisconnectListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerDisconnectEvent>(
        [](ll::event::player::PlayerDisconnectEvent& event) {
            Plugin::getInstance().onPlayerDisconnect(event.self().getRealName());
        }
    );

    // 木棍右键：打开菜单（useitem / 对方块 interact）；左键：快速放置当前投影。
    mUseItemListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerUseItemEvent>(
        [](ll::event::player::PlayerUseItemEvent& event) {
            auto& player = event.self();
            auto const& item = player.getCarriedItem();
            if (item.isNull() || item.getTypeName() != "minecraft:stick") return;
            if (Plugin::getInstance().tryOpenMenu(player)) event.cancel();
        }
    );
    mInteractListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerInteractBlockEvent>(
        [](ll::event::player::PlayerInteractBlockEvent& event) {
            auto& player = event.self();
            auto const& item = player.getCarriedItem();
            if (item.isNull() || item.getTypeName() != "minecraft:stick") return;
            if (Plugin::getInstance().tryOpenMenu(player)) event.cancel();
        }
    );
    mSwingListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerSwingEvent>(
        [](ll::event::player::PlayerSwingEvent& event) {
            Plugin::getInstance().tryLeftClick(event.self());
        }
    );
    mDestroyListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerDestroyBlockEvent>(
        [](ll::event::player::PlayerDestroyBlockEvent& event) {
            auto& player = event.self();
            auto const& item = player.getCarriedItem();
            bool const stick = !item.isNull() && item.getTypeName() == "minecraft:stick";
            auto const& pos = event.pos();
            bool const projection = Plugin::getInstance().isProjectionAt(
                player.getRealName(),
                Vec3i{pos.x, pos.y, pos.z}
            );
            if (stick || projection || Plugin::getInstance().isBreakSuppressed(player.getRealName())) {
                event.cancel();
            }
        }
    );
    mAttackListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerAttackEvent>(
        [](ll::event::player::PlayerAttackEvent& event) {
            auto& player = event.self();
            auto const& item = player.getCarriedItem();
            if (!item.isNull() && item.getTypeName() == "minecraft:stick") event.cancel();
        }
    );


    mSulfur = std::make_unique<SulfurFill>(*this);
    mSulfur->start();
    mSelection = std::make_unique<SelectionVisual>(*this);

    mEnabled = true;
    auto const version = IHologramLib::getInstance().version();
    logger.info(
        "LitematicaBDS 已启用 — 纯服务端投影，HologramLib 运行时 ABI 0x{:06X}，命令 /litematica（别名 /lit）",
        version
    );
    return true;
}

bool Plugin::disable() {
    if (!mEnabled) return true;

    if (mUseItemListener) {
        ll::event::EventBus::getInstance().removeListener(mUseItemListener);
        mUseItemListener = nullptr;
    }
    if (mInteractListener) {
        ll::event::EventBus::getInstance().removeListener(mInteractListener);
        mInteractListener = nullptr;
    }
    if (mSwingListener) {
        ll::event::EventBus::getInstance().removeListener(mSwingListener);
        mSwingListener = nullptr;
    }
    if (mDestroyListener) {
        ll::event::EventBus::getInstance().removeListener(mDestroyListener);
        mDestroyListener = nullptr;
    }
    if (mAttackListener) {
        ll::event::EventBus::getInstance().removeListener(mAttackListener);
        mAttackListener = nullptr;
    }
    if (mDisconnectListener) {
        ll::event::EventBus::getInstance().removeListener(mDisconnectListener);
        mDisconnectListener = nullptr;
    }
    if (mTickListener) {
        ll::event::EventBus::getInstance().removeListener(mTickListener);
        mTickListener = nullptr;
    }
    if (mSelection) {
        mSelection.reset();
    }
    if (mSulfur) {
        mSulfur->stop();
        mSulfur.reset();
    }
    if (mService) {
        mService->shutdown(mSink);
        mService.reset();
    }
    mEnabled = false;
    getSelf().getLogger().info("LitematicaBDS 已禁用");
    return true;
}

void Plugin::tick() {
    if (!mService) return;
    auto level = ll::service::getLevel();
    if (!level) return;

    level->forEachPlayer([this](::Player& player) -> bool {
        auto const  position = player.getFeetPos();
        mService->updateViewer(
            player.getRealName(),
            {position.x, position.y, position.z},
            static_cast<int>(player.getDimensionId())
        );
        return true;
    });

    mService->tick(mSink, ++mTickCount);
    if (mSulfur) mSulfur->tick(mTickCount);
    if (mSelection) mSelection->tick(mTickCount);
}

void Plugin::onPlayerDisconnect(std::string const& playerName) {
    mMenuCooldown.erase(playerName);
    mPlaceCooldown.erase(playerName);
    mBreakSuppress.erase(playerName);
    if (mSelection) mSelection->forgetPlayer(playerName);
    if (mSulfur) mSulfur->forgetPlayer(playerName);
    if (mService) mService->removeViewer(playerName);
}

bool Plugin::tryOpenMenu(::Player& player) {
    auto const now = std::chrono::steady_clock::now();
    auto&      last = mMenuCooldown[player.getRealName()];
    if (last.time_since_epoch().count() != 0 && now - last < std::chrono::milliseconds(500)) {
        return false;
    }
    last = now;
    getSelf().getLogger().info("[menu] {} 用木棍右键打开了投影菜单", player.getRealName());
    showMainMenu(player);
    return true;
}

bool Plugin::isProjectionAt(std::string const& playerName, Vec3i pos) const {
    auto const* blocks = mSink.blocksFor(playerName);
    return blocks != nullptr && blocks->contains(pos);
}

bool Plugin::isBreakSuppressed(std::string const& playerName) const {
    auto it = mBreakSuppress.find(playerName);
    return it != mBreakSuppress.end() && std::chrono::steady_clock::now() < it->second;
}

bool Plugin::tryLeftClick(::Player& player) {
    if (!mService) return false;
    auto const& held = player.getCarriedItem();
    bool const  stick = !held.isNull() && held.getTypeName() == "minecraft:stick";

    // 视线内已有硫磺怪时交给 GhostInteractEvent；否则先尝试直接填充投影方块。
    if (mSulfur && !mSulfur->hasActiveNear(player) && mSulfur->tryFillLook(player, mTickCount)) {
        // 填充会立刻把该格从投影移除；本次左键后续的破坏事件要拦掉，避免打掉刚放下的方块。
        mBreakSuppress[player.getRealName()] = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        return true;
    }
    return stick ? tryQuickPlace(player) : false;
}

bool Plugin::tryQuickPlace(::Player& player) {
    if (!mService) return false;
    // 视线内已有硫磺怪时，左键用于填充，不再触发“快速放置投影”。
    if (mSulfur && mSulfur->hasActiveNear(player)) return false;
    auto const now  = std::chrono::steady_clock::now();
    auto&      last = mPlaceCooldown[player.getRealName()];
    if (last.time_since_epoch().count() != 0 && now - last < std::chrono::milliseconds(500)) {
        return false;
    }
    last = now;

    // 某些客户端右键使用物品也会带一次 swing；若刚开过菜单，跳过快速放置。
    if (auto it = mMenuCooldown.find(player.getRealName()); it != mMenuCooldown.end()) {
        if (now - it->second < std::chrono::milliseconds(300)) return false;
    }

    CommandContext context;
    context.player = player.getRealName();
    auto const  pos = player.getFeetPos();
    context.position = {pos.x, pos.y, pos.z};
    context.dimension = static_cast<int>(player.getDimensionId());
    context.op = static_cast<int>(player.getCommandPermissionLevel()) >= 1;

    auto result = mService->execute(context, "place");
    for (auto const& line : result.lines) player.sendMessage(line);
    return true;
}


} // namespace litematicabds::platform

LL_REGISTER_MOD(litematicabds::platform::Plugin, litematicabds::platform::Plugin::getInstance());
