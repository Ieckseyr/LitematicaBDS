"""LitematicaBDS 的 endstone 插件入口。

职责刻意做薄：核心（litematica_core.dll）已经负责原理图解析、投影调度、层过滤、
材料统计与命令解析；这里只做平台接线：

  · 每 tick 把在线玩家位置喂给核心
  · 把核心产出的 BlockChange 交给 HologramLib 的 fakeblock 域
  · 把 /litematica 命令转发给核心
"""

from __future__ import annotations

import ctypes
import time
from pathlib import Path
from typing import Any

from endstone import Player
from endstone.command import Command, CommandSender
from endstone.event import PlayerInteractEvent, PlayerQuitEvent, event_handler
from endstone.form import ActionForm
from endstone.plugin import Plugin

from .native import CoreError, NativeCore

DEFAULT_CONFIG = """{
  // 渲染：每 tick 每玩家最多发多少个假方块
  "render": {
    "blocksPerTick": 256,
    "clearPerTick": 512,
    "viewDistance": 64,
    "autoLayerRadius": 2,
    "surfaceOnly": true,
    "maxBlocksPerViewer": 30000,
    "planMoveThreshold": 4,
    "showStatusHud": false
  },
  "projection": {
    "showBounds": true,
    "boundsColor": "#00FF00",
    "defaultRotation": 0,
    "defaultMirrorX": false,
    "defaultMirrorZ": false
  },
  "sulfur": {
    "enabled": true,
    "radius": 8,
    "maxDisplays": 64,
    "visible": false,
    "faceBlock": true
  },
  "fill": {
    "enabled": true,
    "requireOp": false,
    "consumeItem": true,
    "quickFill": false
  },
  "save": {
    "maxVolume": 262144
  },
  "storage": {
    "schematicsDir": "schematics",
    "exportsDir": "exports"
  },
  "debug": false
}
"""


def _dimension_id(player: Player) -> int:
    try:
        return int(player.dimension.type.value)
    except Exception:
        return 0


class _InteractListener:
    def __init__(self, plugin: "LitematicaBDSPlugin") -> None:
        self._plugin = plugin

    def _is_stick(self, event: PlayerInteractEvent) -> bool:
        if not event.has_item or event.item is None:
            return False
        try:
            return event.item.type.id == "minecraft:stick"
        except Exception:
            return False

    @event_handler
    def on_player_interact(self, event: PlayerInteractEvent) -> None:
        if not self._is_stick(event):
            return
        if event.action in (PlayerInteractEvent.RIGHT_CLICK_AIR, PlayerInteractEvent.RIGHT_CLICK_BLOCK):
            event.cancel()
            self._plugin.show_menu(event.player)
        elif event.action in (PlayerInteractEvent.LEFT_CLICK_AIR, PlayerInteractEvent.LEFT_CLICK_BLOCK):
            event.cancel()
            self._plugin.quick_place(event.player)


class _QuitListener:
    def __init__(self, plugin: "LitematicaBDSPlugin") -> None:
        self._plugin = plugin

    @event_handler
    def on_player_quit(self, event: PlayerQuitEvent) -> None:
        self._plugin.on_player_quit(event.player.name)


class LitematicaBDSPlugin(Plugin):
    api_version = "0.11"
    name = "LitematicaBDS"
    version = "0.1.0"
    description = "纯服务端投影：HologramLib 假方块渲染 .litematic（C++ 核心 + Python 套壳）"
    authors = ["Ieckseyr"]
    load = "POSTWORLD"
    depend = ["HologramLib"]

    commands = {
        "litematica": {
            "description": "LitematicaBDS 纯服务端投影",
            "usages": [
                "/litematica help",
                "/litematica list",
                "/litematica load <名字>",
                "/litematica place [名字]",
                "/litematica placeat <x> <y> <z> [名字]",
                "/litematica clear",
                "/litematica layer <all|auto|层号>",
                "/litematica rot <0|90|180|270>",
                "/litematica mirror <none|x|z|xz>",
                "/litematica info",
                "/litematica materials [数量]",
                "/litematica status",
                "/litematica reload",
            ],
            "permissions": ["litematicabds.command.litematica"],
        },
        "lit": {
            "description": "LitematicaBDS 简写",
            "usages": ["/lit <子命令>"],
            "permissions": ["litematicabds.command.litematica"],
        },
    }

    permissions = {
        "litematicabds.command.litematica": {
            "description": "允许使用 /litematica 投影命令",
            "default": "true",
        }
    }

    def __init__(self) -> None:
        super().__init__()
        self._core: NativeCore | None = None
        self._holo: Any = None
        self._tick_task: Any = None

    # ── 生命周期 ──────────────────────────────────────────────────────────
    def on_enable(self) -> None:
        try:
            self._holo = self.server.plugin_manager.get_plugin("HologramLib")
        except Exception as exc:
            self.logger.error(f"未找到 HologramLib 前置: {exc}")
            return
        if self._holo is None:
            self.logger.error("未找到 HologramLib 前置，LitematicaBDS 不启用")
            return

        data = Path(self.data_folder)
        data.mkdir(parents=True, exist_ok=True)
        (data / "schematics").mkdir(exist_ok=True)
        (data / "exports").mkdir(exist_ok=True)
        config = data / "config.json"
        if not config.exists():
            config.write_text(DEFAULT_CONFIG, encoding="utf-8")

        try:
            self._core = NativeCore()
            self.logger.info(f"litematica_core {self._core.version} (ABI {self._core.api_version})")
            self._core.create(data)
            self._core.initialize()
            self._core.set_sampler(self._sample_block)
        except (CoreError, OSError) as exc:
            self.logger.error(f"LitematicaBDS 核心初始化失败: {exc}")
            self._core = None
            return

        self.register_events(_InteractListener(self))
        self.register_events(_QuitListener(self))
        self._tick_task = self.server.scheduler.run_task(self, self._tick, delay=0, period=1)
        self.logger.info("LitematicaBDS 已启用（纯服务端投影，HologramLib 假方块渲染）")

    def on_disable(self) -> None:
        if self._tick_task is not None:
            try:
                self._tick_task.cancel()
            except Exception:
                pass
            self._tick_task = None
        if self._core is not None:
            try:
                self._core.shutdown()
                self._pump_changes()
            except Exception as exc:
                self.logger.warning(f"停用清理失败: {exc}")
            finally:
                self._core.close()
                self._core = None
        self.logger.info("LitematicaBDS 已禁用")

    # ── 保存（世界方块采样）─────────────────────────────────────────────
    def _sample_block(self, player_name, x, y, z, dimension, out, capacity, user) -> int:
        """C 核心在 /litematica save 时逐格调用；返回 1 表示 out 写入了状态串。"""
        try:
            name = player_name.decode("utf-8", "replace") if player_name else ""
            player = self.server.get_player(name)
            if player is None:
                return 0
            block = player.dimension.get_block_at(int(x), int(y), int(z))
            block_type = str(block.type)
            if block_type in ("minecraft:air", "minecraft:cave_air", "minecraft:void_air") or block_type.endswith("_air"):
                return 0

            states = block.data.block_states or {}
            parts = []
            for key, value in sorted(states.items()):
                key = str(key)
                if key.startswith("minecraft:"):
                    key = key[len("minecraft:") :]
                if isinstance(value, bool):
                    value = "true" if value else "false"
                parts.append(f"{key}={value}")
            state = block_type + (f"[{','.join(parts)}]" if parts else "")

            encoded = state.encode("utf-8")
            limit = int(capacity) - 1
            if len(encoded) > limit:
                encoded = encoded[:limit]
            ctypes.memmove(out, encoded, len(encoded))
            out[len(encoded)] = bytes([0])
            return 1
        except Exception:
            return 0

    # ── tick ─────────────────────────────────────────────────────────────
    def _tick(self) -> None:
        if self._core is None:
            return
        for player in self.server.online_players:
            location = player.location
            self._core.update_viewer(
                player.name,
                location.x,
                location.y,
                location.z,
                _dimension_id(player),
            )
        self._core.tick(time.time_ns() // 1_000_000)
        self._pump_changes()

    def _pump_changes(self) -> None:
        if self._core is None or self._holo is None:
            return
        for change in self._core.poll():
            player = change.player.decode("utf-8", "replace")
            if not player:
                continue
            if change.kind == 0:
                self._holo.fakeblock.set(
                    player,
                    int(change.x),
                    int(change.y),
                    int(change.z),
                    change.state.decode("utf-8", "replace"),
                )
            else:
                self._holo.fakeblock.clear(player, int(change.x), int(change.y), int(change.z))

    # ── 木棍菜单 ─────────────────────────────────────────────────────────
    def _run(self, player: Player, command: str) -> None:
        if self._core is None:
            player.send_message("§cLitematicaBDS 尚未初始化")
            return
        location = player.location
        ok, text = self._core.execute(
            player.name,
            location.x,
            location.y,
            location.z,
            _dimension_id(player),
            bool(player.is_op),
            command,
        )
        for line in text.splitlines():
            if line:
                player.send_message(line)

    def quick_place(self, player: Player) -> None:
        self._run(player, "place")

    def _show_schematics(self, player: Player) -> None:
        if self._core is None:
            return
        names = self._core.schematic_names()
        form = ActionForm("§l§bLitematicaBDS §r§7· 选择原理图", "§7点击后在脚下放置。")
        if not names:
            form.add_label("§c原理图目录为空")
        for name in names[:20]:
            form.add_button("§f" + name, on_click=lambda p, n=name: self._run(p, f'place "{n}"'))
        form.add_button("§7返回", on_click=lambda p: self.show_menu(p))
        player.send_form(form)

    def show_menu(self, player: Player) -> None:
        form = ActionForm("§l§bLitematicaBDS", "§7木棍右键菜单；左键快速放置当前投影。")
        form.add_button("§a放置 / 加载投影", on_click=lambda p: self._show_schematics(p))
        form.add_button("§e分片（脚部单层）", on_click=lambda p: self._run(p, "layer slice"))
        form.add_button("§e自动 ±2", on_click=lambda p: self._run(p, "layer auto"))
        form.add_button("§e全部", on_click=lambda p: self._run(p, "layer all"))
        form.add_button("§e下一层", on_click=lambda p: self._run(p, "layer next"))
        form.add_button("§e上一层", on_click=lambda p: self._run(p, "layer prev"))
        form.add_button("§b旋转 90°", on_click=lambda p: self._run(p, "rot next"))
        form.add_button("§b镜像切换", on_click=lambda p: self._run(p, "mirror next"))
        form.add_button("§d材料清单", on_click=lambda p: self._run(p, "materials 10"))
        form.add_button("§d保存当前区域", on_click=lambda p: self._run(p, "save lit_" + str(int(time.time()))))
        form.add_button("§6设置 pos1", on_click=lambda p: self._run(p, "pos1"))
        form.add_button("§6设置 pos2", on_click=lambda p: self._run(p, "pos2"))
        form.add_button("§f投影信息", on_click=lambda p: self._run(p, "info"))
        form.add_button("§7运行状态", on_click=lambda p: self._run(p, "status"))
        form.add_button("§7重载配置", on_click=lambda p: self._run(p, "reload"))
        if player.is_op:
            form.add_button("§c管理员菜单", on_click=lambda p: self.show_admin_menu(p))
        player.send_form(form)

    def show_admin_menu(self, player: Player) -> None:
        form = ActionForm("§l§cLitematicaBDS §r§7· 管理员菜单", "§7仅 OP 可见。")
        form.add_button("§e重载配置与原理图", on_click=lambda p: self._run(p, "reload"))
        form.add_button("§f运行状态", on_click=lambda p: self._run(p, "status"))
        form.add_button("§d保存当前区域", on_click=lambda p: self._run(p, "save lit_" + str(int(time.time()))))
        form.add_button("§c清除我的投影", on_click=lambda p: self._run(p, "clear"))
        form.add_button("§7返回主菜单", on_click=lambda p: self.show_menu(p))
        player.send_form(form)

    # ── 命令 ─────────────────────────────────────────────────────────────
    def on_command(self, sender: CommandSender, command: Command, args: list[str]) -> bool:
        if command.name not in ("litematica", "lit"):
            return False
        if self._core is None:
            sender.send_error_message("§cLitematicaBDS 尚未启用（检查 HologramLib 前置）")
            return True

        if isinstance(sender, Player):
            location = sender.location
            player = sender.name
            x, y, z = location.x, location.y, location.z
            dimension = _dimension_id(sender)
            op = bool(sender.is_op)
        else:
            player = ""
            x = y = z = 0.0
            dimension = 0
            op = True

        ok, text = self._core.execute(player, x, y, z, dimension, op, " ".join(args))
        for line in text.splitlines():
            if line:
                sender.send_message(line)
        if not ok and not text:
            sender.send_error_message("§c命令执行失败")
        return True

    # ── 事件 ─────────────────────────────────────────────────────────────
    def on_player_quit(self, player_name: str) -> None:
        if self._core is not None:
            try:
                self._core.remove_viewer(player_name)
            except Exception:
                pass
