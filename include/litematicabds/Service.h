#pragma once

// 顶层服务：平台层唯一需要认识的类。
//
//   core（本类）          配置 / 原理图仓库 / 每个玩家的投影会话 / 渲染队列 / 命令
//   platform/levilamina   注册 /litematica、每 tick 喂玩家位置、把 BlockChange 交给 HologramLib
//   platform/endstone     Python 套壳：同样喂位置、轮询 BlockChange 交给 HologramLib Python
//
// 所有方法都在服务器主线程调用（和 HologramLib 的约定一致），内部不加锁。

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "litematicabds/Config.h"
#include "litematicabds/Mapping.h"
#include "litematicabds/Render.h"
#include "litematicabds/Schematic.h"
#include "litematicabds/Result.h"
#include "litematicabds/Types.h"

namespace litematicabds {

enum class LogLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
};

using LogFn = std::function<void(LogLevel, std::string_view)>;

// 平台提供的方块状态校验器：返回规范化的状态字符串；nullopt = 该状态在
// 当前 BDS 上无法解析。LeviLamina 侧用 Block::tryGetFromRegistry 实现，
// endstone 侧用 HologramLib 的 fakeblock.resolve 实现。
using StateValidator = std::function<std::optional<std::string>(std::string_view state)>;

// 平台提供的世界方块采样器：返回 nullopt = 空气/读不到（保存时跳过）。
using BlockSampler = std::function<std::optional<BlockState>(std::string_view player, Vec3i pos, int dimension)>;

struct CommandContext {
    std::string player; // 空 = 控制台
    Vec3d       position;
    int         dimension{0};
    bool        op{false};
};

struct CommandResult {
    bool                     ok{true};
    std::vector<std::string> lines;
};

struct ViewerStatus {
    std::string player;
    std::string schematic;
    bool        active{false};
    Vec3i       origin;
    int         dimension{0};
    Vec3i       size;
    std::uint64_t totalBlocks{0};
    std::uint64_t appliedBlocks{0};
    std::uint64_t pendingBlocks{0};
    std::string layer{"all"};
    std::string rotation{"none"};
    std::string mirror{"none"};
};

class LitematicaService {
public:
    struct Options {
        std::filesystem::path dataDir;
        std::filesystem::path schematicsDir; // 为空 = dataDir/schematics
        std::filesystem::path exportsDir;    // 为空 = dataDir/exports
        std::filesystem::path configPath;    // 为空 = dataDir/config.json
    };

    explicit LitematicaService(Options options);
    ~LitematicaService();

    LitematicaService(LitematicaService const&)            = delete;
    LitematicaService& operator=(LitematicaService const&) = delete;

    void setLogger(LogFn logger);
    void setStateValidator(StateValidator validator);
    void setBlockSampler(BlockSampler sampler);

    // 读取配置 + 扫描原理图目录。构造后必须调用一次。
    Result<void> initialize();

    // 重读配置并重新扫描原理图（不清除在线玩家的投影）。
    void reload();

    // 管理员表单提交：写回 config.json、应用新配置并触发重规划。
    [[nodiscard]] Result<void> updateConfig(Config const& config);

    // 执行一条命令（不含 /litematica 前缀）。线程：主线程。
    [[nodiscard]] CommandResult execute(CommandContext const& context, std::string_view commandLine);

    // 每 tick 由平台调用：先更新所有在线玩家，再 tick 一次。
    void updateViewer(std::string_view player, Vec3d position, int dimension);
    void removeViewer(std::string_view player);

    // 玩家通过硫磺怪点击真实放置了一格：从该玩家的投影里移除并记住，避免再画回去。
    void markBlockPlaced(std::string_view player, Vec3i pos);

    // pos1/pos2 选区（闭区间）；未设置完整时返回 nullopt。
    [[nodiscard]] std::optional<Bounds> selection(std::string_view player) const;

    // 处理投影队列：对每个玩家最多发 blocksPerTick 条 set / clear。
    void tick(IGhostSink& sink, std::uint64_t nowMs);

    // 停服/禁用：尽量把已经发出去的假方块还原。
    void shutdown(IGhostSink& sink);

    [[nodiscard]] Config const& config() const;

    // 只读映射表（平台侧自检/诊断用）。
    [[nodiscard]] BlockMapper const& mapper() const;

    [[nodiscard]] std::vector<ViewerStatus> viewers() const;

    // 原理图仓库（只读）。
    [[nodiscard]] std::vector<std::string> schematicNames() const;

    // 把一段世界方块写成 schematics/<name>.litematic；重名拒绝。
    [[nodiscard]] Result<std::filesystem::path> saveSchematic(
        SaveRequest const&        request,
        std::vector<std::string>* warnings = nullptr
    );

    [[nodiscard]] std::filesystem::path const& schematicsDir() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace litematicabds
