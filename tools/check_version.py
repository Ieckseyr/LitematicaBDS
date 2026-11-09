#!/usr/bin/env python3
"""校验五处版本号一致。

    python tools/check_version.py
"""

from __future__ import annotations

import json
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    versions: dict[str, str] = {}

    m = re.search(r'#define\s+LITEMATICABDS_VERSION\s+"([^"]+)"', read(ROOT / "include" / "litematicabds" / "Version.h"))
    if not m:
        print("Version.h 缺少 LITEMATICABDS_VERSION")
        return 1
    versions["Version.h"] = m.group(1)

    m = re.search(r'set_version\("([^"]+)"\)', read(ROOT / "xmake.lua"))
    if not m:
        print("xmake.lua 缺少 set_version")
        return 1
    versions["xmake.lua"] = m.group(1)

    manifest = json.loads(read(ROOT / "platform" / "levilamina" / "manifest.json"))
    versions["manifest.json"] = manifest["version"]

    with (ROOT / "platform" / "endstone" / "pyproject.toml").open("rb") as f:
        pyproject = tomllib.load(f)
    versions["pyproject.toml"] = pyproject["project"]["version"]

    m = re.search(r'^\s*version\s*=\s*"([^"]+)"', read(ROOT / "platform" / "endstone" / "src" / "litematicabds_endstone" / "plugin.py"), re.M)
    if not m:
        print("plugin.py 缺少 version")
        return 1
    versions["plugin.py"] = m.group(1)

    distinct = set(versions.values())
    for name, version in versions.items():
        print(f"{name:20s} {version}")
    if len(distinct) != 1:
        print("\n版本号不一致！")
        return 1
    print(f"\nOK: {distinct.pop()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
