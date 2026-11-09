#!/usr/bin/env python3
"""把 platform/endstone 打成 endstone 可直接加载的 wheel。

不依赖 hatchling/build：wheel 是固定元数据的 zip，自己写能保证 CI 与本地一致、离线可跑。

产物: dist/endstone_litematicabds-<version>-py3-none-win_amd64.whl
内嵌 litematicabds_endstone/_native/litematica_core.dll。
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import shutil
import sys
import tomllib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENDSTONE_DIR = ROOT / "platform" / "endstone"
SRC_DIR = ENDSTONE_DIR / "src"
PACKAGE = "litematicabds_endstone"
ENTRY_POINT = "litematicabds = litematicabds_endstone:LitematicaBDSPlugin"


def b64_digest(data: bytes) -> str:
    return base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()


def read_metadata() -> dict:
    with (ENDSTONE_DIR / "pyproject.toml").open("rb") as f:
        return tomllib.load(f)


def write_wheel(out_dir: Path) -> Path:
    project = read_metadata()["project"]
    version = project["version"]
    name = project["name"].replace("-", "_")
    dist_info = f"{name}-{version}.dist-info"

    out_dir.mkdir(parents=True, exist_ok=True)
    wheel_path = out_dir / f"{name}-{version}-py3-none-win_amd64.whl"

    files: list[tuple[str, bytes]] = []
    for path in sorted((SRC_DIR / PACKAGE).rglob("*")):
        if not path.is_file():
            continue
        if "__pycache__" in path.parts:
            continue
        arcname = f"{PACKAGE}/{path.relative_to(SRC_DIR / PACKAGE).as_posix()}"
        files.append((arcname, path.read_bytes()))

    if not any(arcname.endswith("litematica_core.dll") for arcname, _ in files):
        raise SystemExit(
            "缺少 litematicabds_endstone/_native/litematica_core.dll —— 先运行 xmake build litematica_core"
        )

    metadata = "\n".join(
        [
            "Metadata-Version: 2.1",
            f"Name: {project['name']}",
            f"Version: {version}",
            f"Summary: {project.get('description', '')}",
            "License: MIT",
            f"Requires-Python: {project.get('requires-python', '>=3.10')}",
            *[f"Requires-Dist: {dep}" for dep in project.get("dependencies", [])],
            "",
            "",
        ]
    ).encode("utf-8")

    wheel_meta = "\n".join(
        [
            "Wheel-Version: 1.0",
            "Generator: litematicabds-pack-endstone",
            "Root-Is-Purelib: false",
            "Tag: py3-none-win_amd64",
            "",
        ]
    ).encode("utf-8")

    files.append((f"{dist_info}/METADATA", metadata))
    files.append((f"{dist_info}/WHEEL", wheel_meta))
    files.append((f"{dist_info}/entry_points.txt", f"[endstone]\n{ENTRY_POINT}\n".encode("utf-8")))

    record_lines = [f"{arcname},sha256={b64_digest(data)},{len(data)}" for arcname, data in files]
    record_lines.append(f"{dist_info}/RECORD,,")
    files.append((f"{dist_info}/RECORD", ("\n".join(record_lines) + "\n").encode("utf-8")))

    with zipfile.ZipFile(wheel_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for arcname, data in files:
            info = zipfile.ZipInfo(arcname, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, data)

    return wheel_path


def deploy(wheel: Path, target: Path) -> None:
    target.mkdir(parents=True, exist_ok=True)
    site = target / ".local" / "Lib" / "site-packages"
    for stale in site.glob(f"{PACKAGE}*"):
        if stale.is_dir():
            shutil.rmtree(stale, ignore_errors=True)
        else:
            stale.unlink(missing_ok=True)
    shutil.copy2(wheel, target / wheel.name)
    print(f"[endstone] 已部署到 {target / wheel.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description="打 LitematicaBDS 的 endstone wheel")
    parser.add_argument("--out", default=str(ROOT / "dist"), help="输出目录（默认 dist/）")
    parser.add_argument("--deploy", default=None, help="额外拷贝到某个 plugins 目录")
    args = parser.parse_args()

    wheel = write_wheel(Path(args.out))
    print(f"[endstone] 产物: {wheel} ({wheel.stat().st_size} bytes)")
    if args.deploy:
        deploy(wheel, Path(args.deploy))
    return 0


if __name__ == "__main__":
    sys.exit(main())
