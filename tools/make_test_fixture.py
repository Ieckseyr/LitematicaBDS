#!/usr/bin/env python3
"""生成测试用 .litematic（不依赖任何第三方库，输出可复现）。

用法: python tools/make_test_fixture.py
产物: tests/fixtures/tiny.litematic
"""
from __future__ import annotations

import gzip
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "tests" / "fixtures" / "tiny.litematic"

TAG_END = 0
TAG_INT = 3
TAG_LONG = 4
TAG_STRING = 8
TAG_LIST = 9
TAG_COMPOUND = 10
TAG_LONG_ARRAY = 12

END = bytes([0])


def i32(v: int) -> bytes:
    return struct.pack(">i", v)


def i64(v: int) -> bytes:
    if v >= (1 << 63):
        v -= 1 << 64
    return struct.pack(">q", v)


def string(v: str) -> bytes:
    data = v.encode("utf-8")
    return struct.pack(">H", len(data)) + data


def named(tag_type: int, name: str, payload: bytes) -> bytes:
    return bytes([tag_type]) + string(name) + payload


def compound(name: str, payload: bytes) -> bytes:
    return named(TAG_COMPOUND, name, payload + END)


def string_tag(name: str, value: str) -> bytes:
    return named(TAG_STRING, name, string(value))


def int_tag(name: str, value: int) -> bytes:
    return named(TAG_INT, name, i32(value))


def long_tag(name: str, value: int) -> bytes:
    return named(TAG_LONG, name, i64(value))


def list_tag(name: str, element_type: int, elements: list[bytes]) -> bytes:
    return named(TAG_LIST, name, bytes([element_type]) + i32(len(elements)) + b"".join(elements))


def long_array_tag(name: str, values: list[int]) -> bytes:
    return named(TAG_LONG_ARRAY, name, i32(len(values)) + b"".join(i64(v) for v in values))


def pack_indices(indices: list[int], bits: int) -> list[int]:
    words = [0] * ((len(indices) * bits + 63) // 64)
    for i, value in enumerate(indices):
        bit = i * bits
        word = bit // 64
        offset = bit % 64
        mask = (1 << bits) - 1
        words[word] |= (value & mask) << offset
        if offset + bits > 64 and word + 1 < len(words):
            words[word + 1] |= (value & mask) >> (64 - offset)
    return words


def build_nbt() -> bytes:
    # 3x2x2，索引顺序 y -> z -> x。palette: 0=air 1=stone 2=oak_log[axis=y]
    sx, sy, sz = 3, 2, 2
    cells: list[int] = []
    for y in range(sy):
        for z in range(sz):
            for x in range(sx):
                if (x, y, z) in ((1, 0, 0), (2, 1, 1), (0, 1, 0)):
                    cells.append(2)
                elif (x, y, z) in ((0, 0, 0), (2, 0, 1), (1, 1, 1)):
                    cells.append(1)
                else:
                    cells.append(0)
    words = pack_indices(cells, 2)

    # NBT List 的元素只写 payload（不带类型/名字头），compound payload 以 0 结束。
    palette = list_tag(
        "BlockStatePalette",
        TAG_COMPOUND,
        [
            string_tag("Name", "minecraft:air") + END,
            string_tag("Name", "minecraft:stone")
            + compound("Properties", string_tag("stone_type", "stone"))
            + END,
            string_tag("Name", "minecraft:oak_log")
            + compound("Properties", string_tag("axis", "y"))
            + END,
        ],
    )

    region_payload = (
        compound("Position", int_tag("x", 0) + int_tag("y", 0) + int_tag("z", 0))
        + compound("Size", int_tag("x", sx) + int_tag("y", sy) + int_tag("z", sz))
        + palette
        + long_array_tag("BlockStates", words)
        + list_tag("TileEntities", TAG_END, [])
        + list_tag("Entities", TAG_END, [])
    )
    region_tag = compound("main", region_payload)
    regions_tag = compound("Regions", region_tag)

    metadata_payload = (
        string_tag("Name", "Tiny")
        + string_tag("Author", "fixture")
        + string_tag("Description", "LitematicaBDS test fixture")
        + int_tag("RegionCount", 1)
        + int_tag("TotalBlocks", 6)
        + int_tag("TotalVolume", sx * sy * sz)
        + compound("EnclosingSize", int_tag("x", sx) + int_tag("y", sy) + int_tag("z", sz))
        + long_tag("TimeCreated", 1700000000)
        + long_tag("TimeModified", 1700000000)
    )
    metadata_tag = compound("Metadata", metadata_payload)

    root_payload = (
        int_tag("Version", 6)
        + int_tag("MinecraftDataVersion", 3955)
        + metadata_tag
        + regions_tag
    )
    return bytes([TAG_COMPOUND]) + string("") + root_payload + END


def main() -> None:
    nbt = build_nbt()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(gzip.compress(nbt, 6))
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes, nbt {len(nbt)} bytes)")


if __name__ == "__main__":
    main()
