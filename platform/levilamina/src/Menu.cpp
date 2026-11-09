#include "Menu.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <ll/api/form/CustomForm.h>
#include <ll/api/form/SimpleForm.h>

#include <mc/world/actor/player/Player.h>

#include "Plugin.h"

namespace litematicabds::platform {
namespace {

CommandContext makeContext(::Player& player) {
    CommandContext context;
    context.player    = player.getRealName();
    auto const  pos   = player.getFeetPos();
    context.position  = {pos.x, pos.y, pos.z};
    context.dimension = static_cast<int>(player.getDimensionId());
    context.op        = static_cast<int>(player.getCommandPermissionLevel()) >= 1;
    return context;
}

void runCommand(::Player& player, std::string const& command) {
    auto* service = Plugin::getInstance().service();
    if (service == nullptr) {
        player.sendMessage("§cLitematicaBDS 尚未初始化");
        return;
    }
    auto result = service->execute(makeContext(player), command);
    for (auto const& line : result.lines) player.sendMessage(line);
}


void showSchematicMenu(::Player& player) {
    auto* service = Plugin::getInstance().service();
    if (service == nullptr) return;

    auto const names = service->schematicNames();
    ll::form::SimpleForm form("§l§bLitematicaBDS §r§7· 选择原理图", "§7点击后直接在脚下放置投影。");
    if (names.empty()) {
        form.appendLabel("§c原理图目录为空。");
        form.appendLabel("§7把 .litematic 放进 mods/LitematicaBDS/schematics/");
        form.appendButton("§7返回", [](::Player& p) { showMainMenu(p); });
        form.sendTo(player);
        return;
    }

    std::size_t shown = 0;
    for (auto const& name : names) {
        if (shown++ >= 20) break;
        form.appendButton("§f" + name, [name](::Player& p) {
            runCommand(p, "place \"" + name + "\"");
            showMainMenu(p);
        });
    }
    if (names.size() > shown) {
        form.appendLabel("§7还有 " + std::to_string(names.size() - shown) + " 个，用 /litematica list 查看");
    }
    form.appendButton("§7返回", [](::Player& p) { showMainMenu(p); });
    form.sendTo(player);
}

void showLayerForm(::Player& player) {
    ll::form::CustomForm form("§l§bLitematicaBDS §r§7· 手动选择层");
    form.appendLabel("§7层号从投影最底层（0）开始算。");
    form.appendInput("layer", "显示层号", "例如 5", "0");
    form.sendTo(player, [](::Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) return;
        auto it = result->find("layer");
        if (it == result->end()) return;
        if (auto const* value = std::get_if<std::string>(&it->second)) {
            runCommand(p, "layer " + *value);
            showMainMenu(p);
        }
    });
}

} // namespace

void showAdminMenu(::Player& player);

void showMainMenu(::Player& player) {
    auto* service = Plugin::getInstance().service();
    if (service == nullptr) {
        player.sendMessage("§cLitematicaBDS 尚未初始化");
        return;
    }

    auto context = makeContext(player);
    std::string content;
    auto        info = service->execute(context, "info");
    for (auto const& line : info.lines) {
        if (!content.empty()) content.push_back('\n');
        content += line;
    }
    if (content.empty()) content = "§7当前没有投影。";

    ll::form::SimpleForm form("§l§bLitematicaBDS", content);
    form.appendButton("§a放置 / 加载投影", [](::Player& p) { showSchematicMenu(p); });
    form.appendButton("§e逐层: 自动", [](::Player& p) {
        runCommand(p, "layer auto");
        showMainMenu(p);
    });
    form.appendButton("§e逐层: 全部", [](::Player& p) {
        runCommand(p, "layer all");
        showMainMenu(p);
    });
    form.appendButton("§e分片（脚部单层）", [](::Player& p) {
        runCommand(p, "layer slice");
        showMainMenu(p);
    });
    form.appendButton("§e下一层", [](::Player& p) {
        runCommand(p, "layer next");
        showMainMenu(p);
    });
    form.appendButton("§e上一层", [](::Player& p) {
        runCommand(p, "layer prev");
        showMainMenu(p);
    });
    form.appendButton("§e手动层号", [](::Player& p) { showLayerForm(p); });
    form.appendButton("§b旋转 90°", [](::Player& p) {
        runCommand(p, "rot next");
        showMainMenu(p);
    });
    form.appendButton("§b镜像切换", [](::Player& p) {
        runCommand(p, "mirror next");
        showMainMenu(p);
    });
    form.appendButton("§d材料清单", [](::Player& p) { runCommand(p, "materials 10"); });
    form.appendButton("§d保存当前区域为原理图", [](::Player& p) {
        runCommand(p, "save lit_" + std::to_string(std::time(nullptr)));
    });
    form.appendButton("§6设置选区 pos1", [](::Player& p) { runCommand(p, "pos1"); });
    form.appendButton("§6设置选区 pos2", [](::Player& p) { runCommand(p, "pos2"); });
    form.appendButton("§f投影信息", [](::Player& p) { runCommand(p, "info"); });
    form.appendButton("§7运行状态", [](::Player& p) { runCommand(p, "status"); });
    form.appendButton("§7重载配置", [](::Player& p) { runCommand(p, "reload"); });
    if (static_cast<int>(player.getCommandPermissionLevel()) >= 1) {
        form.appendButton("§c管理员菜单", [](::Player& p) { showAdminMenu(p); });
    }
    form.appendButton("§c清除投影", [](::Player& p) {
        runCommand(p, "clear");
        showMainMenu(p);
    });
    form.appendButton("§7命令帮助", [](::Player& p) { runCommand(p, "help"); });
    form.sendTo(player);
}

void showAdminMenu(::Player& player) {
    if (static_cast<int>(player.getCommandPermissionLevel()) < 1) {
        player.sendMessage("§c需要 OP 权限");
        return;
    }
    auto* service = Plugin::getInstance().service();
    if (service == nullptr) {
        player.sendMessage("§cLitematicaBDS 尚未初始化");
        return;
    }

    auto const& cfg = service->config();
    ll::form::CustomForm form("§l§cLitematicaBDS §r§7· 管理员配置");
    form.appendLabel("§7提交后立即写回 config.json 并触发重规划。");
    form.appendDivider();

    form.appendSlider("blocksPerTick", "每 tick 方块数", 1, 2048, 1, cfg.render.blocksPerTick);
    form.appendSlider("viewDistance", "投影视距", 8, 256, 1, cfg.render.viewDistance);
    form.appendSlider("autoLayerRadius", "自动层半径", 0, 8, 1, cfg.render.autoLayerRadius);
    form.appendSlider("maxBlocksPerViewer", "单玩家方块上限", 1000, 200000, 1000, cfg.render.maxBlocksPerViewer);
    form.appendSlider("planMoveThreshold", "重规划移动阈值", 1, 16, 1, cfg.render.planMoveThreshold);
    form.appendToggle("surfaceOnly", "只显示暴露面", cfg.render.surfaceOnly);

    form.appendDivider();
    form.appendSlider("sulfurRadius", "硫磺怪半径", 1, 32, 1, cfg.sulfur.radius);
    form.appendSlider("sulfurMaxDisplays", "硫磺怪上限", 1, 512, 1, cfg.sulfur.maxDisplays);
    form.appendToggle("sulfurEnabled", "启用硫磺怪", cfg.sulfur.enabled);
    form.appendToggle("sulfurVisible", "显示硫磺怪本体", cfg.sulfur.visible);
    form.appendToggle("sulfurFaceBlock", "跟随方块朝向", cfg.sulfur.faceBlock);

    form.appendDivider();
    form.appendToggle("fillEnabled", "启用填充", cfg.fill.enabled);
    form.appendToggle("fillRequireOp", "填充需要 OP", cfg.fill.requireOp);
    form.appendToggle("fillConsumeItem", "生存消耗方块", cfg.fill.consumeItem);
    form.appendToggle("fillQuickFill", "管理员快速填充", cfg.fill.quickFill);

    form.appendDivider();
    form.appendInput("saveMaxVolume", "保存体积上限", "例如 262144", std::to_string(cfg.save.maxVolume));
    form.appendToggle("projectionShowBounds", "显示投影包围盒", cfg.projection.showBounds);
    form.appendInput("boundsColor", "包围盒颜色", "#00FF00", cfg.projection.boundsColor);
    form.appendDropdown(
        "defaultRotation",
        "默认旋转",
        {"0°", "90°", "180°", "270°"},
        static_cast<std::size_t>(std::clamp(cfg.projection.defaultRotation / 90, 0, 3))
    );
    form.appendToggle("defaultMirrorX", "默认 X 镜像", cfg.projection.defaultMirrorX);
    form.appendToggle("defaultMirrorZ", "默认 Z 镜像", cfg.projection.defaultMirrorZ);

    form.appendDivider();
    form.appendInput("schematicsDir", "原理图目录", "schematics", cfg.storage.schematicsDir);
    form.appendInput("exportsDir", "导出目录", "exports", cfg.storage.exportsDir);
    form.appendToggle("debug", "调试日志", cfg.debug);

    form.sendTo(player, [](::Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) return;
        auto* svc = Plugin::getInstance().service();
        if (svc == nullptr) return;

        auto readUInt = [&](std::string const& key) -> std::optional<std::uint64_t> {
            auto it = result->find(key);
            if (it == result->end()) return std::nullopt;
            if (auto const* v = std::get_if<std::uint64_t>(&it->second)) return *v;
            if (auto const* v = std::get_if<double>(&it->second)) {
                return static_cast<std::uint64_t>(*v);
            }
            return std::nullopt;
        };
        auto readBool = [&](std::string const& key, bool fallback) {
            auto it = result->find(key);
            if (it == result->end()) return fallback;
            if (auto const* v = std::get_if<std::string>(&it->second)) {
                return *v == "true" || *v == "1";
            }
            if (auto const* v = std::get_if<std::uint64_t>(&it->second)) return *v != 0;
            if (auto const* v = std::get_if<double>(&it->second)) return *v != 0;
            return fallback;
        };
        auto readString = [&](std::string const& key, std::string fallback) {
            auto it = result->find(key);
            if (it == result->end()) return fallback;
            if (auto const* v = std::get_if<std::string>(&it->second)) return *v;
            return fallback;
        };
        auto readInt = [&](std::string const& key, int fallback, int lo, int hi) {
            auto value = readUInt(key);
            if (!value) return fallback;
            return std::clamp(static_cast<int>(*value), lo, hi);
        };

        Config cfg = svc->config();
        cfg.render.blocksPerTick     = readInt("blocksPerTick", cfg.render.blocksPerTick, 1, 2048);
        cfg.render.viewDistance      = readInt("viewDistance", cfg.render.viewDistance, 8, 256);
        cfg.render.autoLayerRadius   = readInt("autoLayerRadius", cfg.render.autoLayerRadius, 0, 8);
        cfg.render.maxBlocksPerViewer =
            readInt("maxBlocksPerViewer", cfg.render.maxBlocksPerViewer, 1000, 200000);
        cfg.render.planMoveThreshold = readInt("planMoveThreshold", cfg.render.planMoveThreshold, 1, 16);
        cfg.render.surfaceOnly       = readBool("surfaceOnly", cfg.render.surfaceOnly);

        cfg.sulfur.radius      = readInt("sulfurRadius", cfg.sulfur.radius, 1, 32);
        cfg.sulfur.maxDisplays = readInt("sulfurMaxDisplays", cfg.sulfur.maxDisplays, 1, 512);
        cfg.sulfur.enabled     = readBool("sulfurEnabled", cfg.sulfur.enabled);
        cfg.sulfur.visible     = readBool("sulfurVisible", cfg.sulfur.visible);
        cfg.sulfur.faceBlock   = readBool("sulfurFaceBlock", cfg.sulfur.faceBlock);

        cfg.fill.enabled     = readBool("fillEnabled", cfg.fill.enabled);
        cfg.fill.requireOp   = readBool("fillRequireOp", cfg.fill.requireOp);
        cfg.fill.consumeItem = readBool("fillConsumeItem", cfg.fill.consumeItem);
        cfg.fill.quickFill   = readBool("fillQuickFill", cfg.fill.quickFill);

        auto volumeText = readString("saveMaxVolume", std::to_string(cfg.save.maxVolume));
        int  volume     = cfg.save.maxVolume;
        std::from_chars(volumeText.data(), volumeText.data() + volumeText.size(), volume);
        cfg.save.maxVolume = std::clamp(volume, 1, 8'000'000);

        cfg.projection.showBounds = readBool("projectionShowBounds", cfg.projection.showBounds);
        cfg.projection.boundsColor = readString("boundsColor", cfg.projection.boundsColor);
        cfg.projection.defaultRotation =
            readInt("defaultRotation", cfg.projection.defaultRotation / 90, 0, 3) * 90;
        cfg.projection.defaultMirrorX = readBool("defaultMirrorX", cfg.projection.defaultMirrorX);
        cfg.projection.defaultMirrorZ = readBool("defaultMirrorZ", cfg.projection.defaultMirrorZ);

        cfg.storage.schematicsDir = readString("schematicsDir", cfg.storage.schematicsDir);
        cfg.storage.exportsDir    = readString("exportsDir", cfg.storage.exportsDir);
        cfg.debug                 = readBool("debug", cfg.debug);

        auto saved = svc->updateConfig(cfg);
        if (!saved.ok()) {
            p.sendMessage("§c配置保存失败: " + saved.error().message);
            return;
        }
        p.sendMessage("§a配置已更新并写回 config.json");
        showMainMenu(p);
    });
}

} // namespace litematicabds::platform
