# LitematicaBDS for Endstone

这是 LitematicaBDS 的 endstone 套壳：与 LeviLamina 原生插件共用同一份 C++ 核心
（`core/` → `litematica_core.dll`），Python 侧只负责平台接线。

## 前置

- endstone 0.11+
- `HologramLib` endstone 版（提供逐玩家假方块 `fakeblock` 域）

## 构建与安装

```bat
xmake build litematica_core
python tools/pack_endstone.py --deploy <bedrock_server>\plugins
```

产物：`dist/endstone_litematicabds-<版本>-py3-none-win_amd64.whl`，内嵌
`litematicabds_endstone/_native/litematica_core.dll`。

## 用法

与 LeviLamina 版一致：

```
/litematica list
/litematica load <名字>
/litematica place [名字]
/litematica layer <all|auto|层号>
/litematica materials
/litematica clear
/litematica pos1
/litematica pos2
/litematica save <名字>
```

原理图放在 `plugins/LitematicaBDS/schematics/`，材料清单导出到 `plugins/LitematicaBDS/exports/`。
`pos1`/`pos2` 选区后 `save` 会把世界建筑写成 `.litematic`（读取 `Block.data.block_states`）。
