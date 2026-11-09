# 架构

```
platform/levilamina  LitematicaBDS.dll
  ModEntry / 命令 / 菜单 / tick
  HoloSink        -> HologramLib IViewOverride
  SulfurFill      -> HologramLib sulfur_cube + GhostInteractEvent
  SelectionVisual -> HologramLib box + hologram text
        │
core/  litematicabds_core.lib（平台无关）
  nbt / schematic / mapping / service / config / c_api
        │ C ABI
platform/endstone  litematica_core.dll + Python 套壳
  ctypes / 命令 / tick -> HologramLib fakeblock
```

核心只认识 `IGhostSink::setBlock/clearBlock`；平台负责真正发包。因此核心可在宿主机用
`FakeSink` 单测，两个平台共用同一份解析、映射、投影调度与命令。

## 数据流

1. 加载：gzip → NBT → 区域/palette/位打包 → Java→Bedrock 映射 → 坐标归一化 →
   16³ 分块 + 表面剔除。
2. 每 tick：平台喂 `updateViewer`；核心按视距/层/表面筛选，取最近上限，与已应用集合求差，
   按 `blocksPerTick` / `clearPerTick` 调 sink。
3. 玩家移动超过阈值、切层、换投影时重规划；只发新增、只还原移除。
4. 选区：核心返回 `Bounds`；平台画盒线与悬浮字。
5. 硫磺怪：平台读取已应用的假方块，近处改为 `air` + sulfur 实体；点击后写真实方块，
   调 `markBlockPlaced` 让核心不再画该格。

## 约束

- `core/` 不 include 平台头；渲染只走 sink。
- 所有逻辑在服务器主线程；HologramLib 交互事件先入队再在主线程处理。
- 硫磺怪/填充/菜单/选区视觉属于平台层，不要塞进 core。
- 大投影必须遵守每 tick 预算。

## 路线图

轻松放置、投影打印机、Mega 分块加载。
