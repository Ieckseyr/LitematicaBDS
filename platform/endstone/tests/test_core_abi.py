#!/usr/bin/env python3
"""litematica_core.dll 的 C ABI 冒烟测试（不启动 BDS）。

用法: python platform/endstone/tests/test_core_abi.py
"""
from __future__ import annotations

import ctypes
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DLL = ROOT / "platform" / "endstone" / "src" / "litematicabds_endstone" / "_native" / "litematica_core.dll"
FIXTURE = ROOT / "tests" / "fixtures" / "tiny.litematic"


SAMPLER = ctypes.CFUNCTYPE(
    ctypes.c_int32,
    ctypes.c_char_p,
    ctypes.c_int32,
    ctypes.c_int32,
    ctypes.c_int32,
    ctypes.c_int32,
    ctypes.POINTER(ctypes.c_char),
    ctypes.c_size_t,
    ctypes.c_void_p,
)


class Change(ctypes.Structure):
    _fields_ = [
        ("kind", ctypes.c_int32),
        ("x", ctypes.c_int32),
        ("y", ctypes.c_int32),
        ("z", ctypes.c_int32),
        ("state", ctypes.c_char * 192),
        ("player", ctypes.c_char * 64),
    ]


def main() -> int:
    if not DLL.exists():
        print(f"缺少 {DLL}，先运行 xmake build litematica_core")
        return 1

    lib = ctypes.CDLL(str(DLL))
    lib.litbds_api_version.restype = ctypes.c_uint32
    lib.litbds_version.restype = ctypes.c_char_p
    lib.litbds_create.restype = ctypes.c_void_p
    lib.litbds_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
    lib.litbds_initialize.argtypes = [ctypes.c_void_p]
    lib.litbds_execute.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_double,
        ctypes.c_double,
        ctypes.c_double,
        ctypes.c_int32,
        ctypes.c_int32,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_size_t,
    ]
    lib.litbds_update_viewer.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_double,
        ctypes.c_double,
        ctypes.c_double,
        ctypes.c_int32,
    ]
    lib.litbds_set_sampler.argtypes = [ctypes.c_void_p, SAMPLER, ctypes.c_void_p]
    lib.litbds_tick.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.litbds_poll.argtypes = [ctypes.c_void_p, ctypes.POINTER(Change), ctypes.c_size_t]
    lib.litbds_shutdown.argtypes = [ctypes.c_void_p]
    lib.litbds_destroy.argtypes = [ctypes.c_void_p]

    assert lib.litbds_api_version() == 1
    assert lib.litbds_version() == b"0.1.0"

    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        (root / "schematics").mkdir()
        shutil.copy2(FIXTURE, root / "schematics" / "tiny.litematic")

        core = lib.litbds_create(str(root).encode(), None, None)
        assert core, "litbds_create failed"
        try:
            assert lib.litbds_initialize(core) == 0

            out = ctypes.create_string_buffer(8192)
            rc = lib.litbds_execute(
                core, b"Steve", 0.5, 64.0, 0.5, 0, 1, b"place tiny 0 64 0", out, len(out)
            )
            text = out.value.decode("utf-8", "replace")
            assert rc == 1, text
            assert "投影已放置" in text, text

            assert lib.litbds_update_viewer(core, b"Steve", 0.5, 64.0, 0.5, 0) == 0

            seen: dict[tuple[int, int, int], str] = {}
            clears: list[tuple[int, int, int]] = []
            batch = (Change * 256)()
            for i in range(10):
                lib.litbds_tick(core, i)
                while True:
                    count = lib.litbds_poll(core, batch, len(batch))
                    if count <= 0:
                        break
                    for j in range(count):
                        change = batch[j]
                        key = (change.x, change.y, change.z)
                        if change.kind == 0:
                            seen[key] = change.state.decode("utf-8", "replace")
                        else:
                            clears.append(key)

            assert len(seen) == 6, seen
            assert all(state.startswith("minecraft:") for state in seen.values()), seen
            assert any("oak_log" in state for state in seen.values()), seen

            rc = lib.litbds_execute(
                core, b"Steve", 0.5, 64.0, 0.5, 0, 1, b"clear", out, len(out)
            )
            assert rc == 1, out.value
            for i in range(10):
                lib.litbds_tick(core, 100 + i)
                while True:
                    count = lib.litbds_poll(core, batch, len(batch))
                    if count <= 0:
                        break
                    for j in range(count):
                        change = batch[j]
                        if change.kind == 1:
                            clears.append((change.x, change.y, change.z))
            assert clears, "clear 没有产生还原操作"

            # 保存：采样器由平台提供；这里所有方块都返回石头
            def sampler(player, x, y, z, dim, out, cap, user):
                state = b"minecraft:stone"
                ctypes.memmove(out, state, len(state))
                out[len(state)] = bytes([0])
                return 1

            callback = SAMPLER(sampler)
            lib.litbds_set_sampler(core, callback, None)
            rc = lib.litbds_execute(
                core, b"Steve", 0.5, 64.0, 0.5, 0, 1, b"save demo 0 64 0 1 64 1", out, len(out)
            )
            text = out.value.decode("utf-8", "replace")
            assert rc == 1, text
            assert "已保存" in text, text
            saved = root / "schematics" / "demo.litematic"
            assert saved.exists(), saved

            lib.litbds_shutdown(core)
            print("core ABI OK")
        finally:
            lib.litbds_destroy(core)

    return 0


if __name__ == "__main__":
    sys.exit(main())
