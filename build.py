#!/usr/bin/env python3
"""一条命令构建 LitematicaBDS。

    python build.py                  # 核心 + 单测 + LL 插件 + endstone wheel
    python build.py --only core      # 只构建核心与单测
    python build.py --only endstone  # 只构建 core DLL + wheel
    python build.py --deploy-endstone <plugins>

可选环境变量见 README（LITBDS_HOLOLIB_DIR / LITBDS_DEPLOY_DIR / LITBDS_LEVILAMINA_REPO）。
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def run(cmd: list[str]) -> int:
    print(f"\n[build] {' '.join(cmd)}")
    return subprocess.call(cmd, cwd=str(ROOT))


def have(tool: str) -> bool:
    return shutil.which(tool) is not None


def main() -> int:
    parser = argparse.ArgumentParser(description="构建 LitematicaBDS")
    parser.add_argument("--mode", default="release", choices=["release", "debug"])
    parser.add_argument("--only", default="all", choices=["all", "core", "levilamina", "endstone"])
    parser.add_argument("--deploy-endstone", default=None, help="endstone 的 plugins 目录")
    parser.add_argument("--skip-tests", action="store_true")
    args = parser.parse_args()

    if not have("xmake"):
        print("[build] 找不到 xmake：https://xmake.io", file=sys.stderr)
        return 1

    want_native = args.only in ("all", "levilamina")
    want_core = args.only in ("all", "core", "endstone")
    want_endstone = args.only in ("all", "endstone")

    rc = run(
        [
            "xmake",
            "f",
            "-y",
            "-p",
            "windows",
            "-a",
            "x64",
            "-m",
            args.mode,
            "--toolchain=clang-cl",
            f"--with_native={'y' if want_native else 'n'}",
        ]
    )
    if rc != 0:
        return rc

    targets: list[str] = []
    if want_core:
        targets.append("litematica_core")
        if args.only in ("all", "core"):
            targets.append("litematicabds_tests")
    if want_native:
        targets.append("LitematicaBDS")
    rc = run(["xmake", "build", "-y", *targets])
    if rc != 0:
        return rc

    if want_core and not args.skip_tests:
        exe = ROOT / "build" / "windows" / "x64" / args.mode / "litematicabds_tests.exe"
        rc = run([str(exe)])
        if rc != 0:
            return rc

    if want_endstone:
        pack = [sys.executable, str(ROOT / "tools" / "pack_endstone.py")]
        if args.deploy_endstone:
            pack += ["--deploy", args.deploy_endstone]
        rc = run(pack)
        if rc != 0:
            return rc

    print("\n[build] 完成")
    if want_native:
        print("  LeviLamina: LitematicaBDS.dll（已按 LITBDS_DEPLOY_DIR 部署）")
    if want_endstone:
        print("  endstone: dist/endstone_litematicabds-*.whl")
    return 0


if __name__ == "__main__":
    sys.exit(main())
