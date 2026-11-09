# AGENTS.md

## 结构

| 路径 | 作用 |
| ---- | ---- |
| `include/litematicabds/` | 核心公开头，不得 include 平台头 |
| `core/` | NBT / litematic / 映射 / 服务 / 配置 / C ABI |
| `platform/levilamina/` | 插件、命令、菜单、HologramLib sink、硫磺怪/填充、选区视觉 |
| `platform/endstone/` | ctypes 套壳 + wheel |
| `tests/` | 宿主机单测与独立生成的 `.litematic` 夹具 |
| `tools/` | 映射生成、夹具生成、wheel、版本校验 |

## 构建与测试

```bat
python build.py
:: 只构建核心
xmake f -y -p windows -a x64 -m release --toolchain=clang-cl --with_native=n
xmake build -y litematicabds_tests && build\windows\x64\release\litematicabds_tests.exe
```

环境变量：`LITBDS_HOLOLIB_DIR`、`LITBDS_DEPLOY_DIR`、`LITBDS_LEVILAMINA_REPO`。

- 核心改动跑 `litematicabds_tests`（33 用例 / 193 断言）。
- C ABI 改动同步 `native.py` 字段并跑 `test_core_abi.py`。
- wheel 改动跑 `pack_endstone.py`，确认包含 `litematica_core.dll`。
- 夹具用 `tools/make_test_fixture.py` 生成，不手改二进制。

## 约定

- 核心不依赖平台；渲染只走 `IGhostSink`；保存只走 `BlockSampler`。
- 硫磺怪、填充、菜单、选区视觉属于平台层，核心只提供 `markBlockPlaced` / `selection`。
- 新渲染路径必须遵守每 tick 预算。
- 映射是生成物：改映射改生成器，再生成 `.inc` / `.json`。
- C ABI 字段只在尾部追加，改布局递增版本并同步 ctypes。
- 提交信息用 conventional commit + 中文说明；不要在没有明确要求时推送。
- 版本号：`Version.h` / `xmake.lua` / `manifest.json` / `pyproject.toml` / `plugin.py` 五处一致，
  `python tools/check_version.py` 校验。
