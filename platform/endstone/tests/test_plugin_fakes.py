#!/usr/bin/env python3
"""在假 endstone 上跑通 LitematicaBDS 套壳（真实 core DLL + 假平台）。

用法: python platform/endstone/tests/test_plugin_fakes.py
"""
from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "platform" / "endstone" / "tests" / "fakes"))
sys.path.insert(0, str(ROOT / "platform" / "endstone" / "src"))

from endstone import ConsoleSender, Player, _Server  # noqa: E402
from endstone.command import Command  # noqa: E402
from litematicabds_endstone import LitematicaBDSPlugin  # noqa: E402


class FakeFakeBlock:
    def __init__(self) -> None:
        self.sets: list[tuple] = []
        self.clears: list[tuple] = []

    def set(self, player: str, x: int, y: int, z: int, state: str) -> bool:
        self.sets.append((player, x, y, z, state))
        return True

    def clear(self, player: str, x: int, y: int, z: int) -> bool:
        self.clears.append((player, x, y, z))
        return True


class FakeHologramLib:
    def __init__(self) -> None:
        self.fakeblock = FakeFakeBlock()


def main() -> int:
    fixture = ROOT / "tests" / "fixtures" / "tiny.litematic"
    with tempfile.TemporaryDirectory() as temp:
        data = Path(temp) / "LitematicaBDS"
        (data / "schematics").mkdir(parents=True)
        shutil.copy2(fixture, data / "schematics" / "tiny.litematic")

        holo = FakeHologramLib()
        player = Player("Steve")
        server = _Server({"HologramLib": holo}, [player])

        plugin = LitematicaBDSPlugin()
        plugin._server = server
        plugin._data_folder = data
        plugin.on_enable()
        assert plugin._core is not None, "核心未初始化"

        assert plugin.on_command(player, Command("litematica"), ["load", "tiny"])
        text = "\n".join(player.messages)
        assert "已加载" in text, text

        assert plugin.on_command(player, Command("lit"), ["place", "tiny"])
        text = "\n".join(player.messages)
        assert "投影已放置" in text, text

        for _ in range(10):
            plugin._tick()
        assert len(holo.fakeblock.sets) == 6, holo.fakeblock.sets
        assert all(item[0] == "Steve" for item in holo.fakeblock.sets)
        assert any("oak_log" in item[4] for item in holo.fakeblock.sets)

        assert plugin.on_command(player, Command("litematica"), ["clear"])
        for _ in range(10):
            plugin._tick()
        assert holo.fakeblock.clears, "clear 没有产生还原"

        console = ConsoleSender()
        assert plugin.on_command(console, Command("litematica"), ["status"])
        assert any("LitematicaBDS" in line for line in console.messages)

        # 选区保存：pos1/pos2 + save，采样器走 Endstone 的 block.data.block_states
        player.location.x, player.location.y, player.location.z = 0.5, 64.0, 0.5
        assert plugin.on_command(player, Command("litematica"), ["pos1"])
        player.location.x, player.location.z = 1.5, 1.5
        assert plugin.on_command(player, Command("litematica"), ["pos2"])
        assert plugin.on_command(player, Command("litematica"), ["save", "fake_save"])
        assert (data / "schematics" / "fake_save.litematic").exists()

        plugin.on_player_quit("Steve")
        plugin.on_disable()
        print("endstone shell OK")

    return 0


if __name__ == "__main__":
    sys.exit(main())
