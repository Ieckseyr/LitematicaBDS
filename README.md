# LitematicaBDS

基岩版**纯服务端**投影插件：读 Java 版 `.litematic`，用
[HologramLib](https://github.com/Ieckseyr/HologramLib) 的逐玩家假方块显示，
客户端不需要装 BP/RP。

- C++ 核心，双平台：LeviLamina 原生插件 + Endstone 套壳
- 投影 / 逐层 / 旋转 / 镜像 / 材料清单 / 选区保存 / 选区盒 / 硫磺怪填充
- 前置：HologramLib 26.60.8+（ABI `0x012B00`，支持 `#networkId` 假方块）

## 安装

**LeviLamina**

1. 安装 LeviLamina 26.60 + HologramLib 26.60.8（`mods/HologramLib`）。
2. 把 `LitematicaBDS.dll` + `manifest.json` 放进 `mods/LitematicaBDS/`。
3. 原理图放 `mods/LitematicaBDS/schematics/`，重启服务器。

**Endstone**

```bat
xmake build litematica_core
python tools/pack_endstone.py --deploy <bedrock_server>\plugins
```

安装生成的 wheel，并确保 HologramLib 的 endstone 版已安装。

## 使用

手持**木棍右键**打开菜单；**木棍左键**快速放置当前已选投影；或使用命令（`/lit` 为别名）：

| 命令 | 作用 |
| ---- | ---- |
| `/litematica list` | 列出原理图 |
| `/litematica load <名字>` | 加载 |
| `/litematica place [名字] [x y z]` | 放置，默认脚下 |
| `/litematica clear` | 清除投影 |
| `/litematica layer <all\|auto\|slice\|层号\|next\|prev>` | 逐层；`slice` = 脚部单层 |
| `/litematica rot <0\|90\|180\|270\|next>` | 旋转 |
| `/litematica mirror <none\|x\|z\|xz\|next>` | 镜像 |
| `/litematica pos1` / `pos2` | 选区角点（盒线框 + 尺寸悬浮字） |
| `/litematica save <名字> [x1 y1 z1 x2 y2 z2]` | 保存为 `.litematic` |
| `/litematica materials [数量]` | 材料清单 CSV |
| `/litematica info` / `status` / `reload` | 状态与维护 |

**靠近硫磺怪**：8 格内实心方块变成含着对应方块的硫磺怪（本体默认隐身），外包 DebugShape 框；
手持对应方块**左键**点击，真实放置该方块（生存消耗 1 个，创造不消耗）。
非方块投影（花草/火把/铁轨等）只保留投影和框，不生成硫磺怪；左键直接点投影方块也能填充。

## 配置

`mods/LitematicaBDS/config.json`（允许 `//` 与尾随逗号）：

```json
{
  "render": { "blocksPerTick": 256, "viewDistance": 64, "surfaceOnly": true },
  "projection": { "defaultRotation": 0, "defaultMirrorX": false, "defaultMirrorZ": false },
  "sulfur": { "enabled": true, "radius": 8, "maxDisplays": 64, "visible": false, "faceBlock": true },
  "fill": { "enabled": true, "requireOp": false, "consumeItem": true, "quickFill": false },
  "save": { "maxVolume": 262144 },
  "storage": { "schematicsDir": "schematics", "exportsDir": "exports" },
  "debug": false
}
```

## 构建

```bat
python build.py
:: 或只构建核心与单测
xmake f -y -p windows -a x64 -m release --toolchain=clang-cl --with_native=n
xmake build -y litematicabds_tests
build\windows\x64\release\litematicabds_tests.exe
```

环境变量：`LITBDS_HOLOLIB_DIR`、`LITBDS_DEPLOY_DIR`、`LITBDS_LEVILAMINA_REPO`。

## 已知限制

- 基岩协议层不支持逐客户端半透明方块：投影以真实方块显示，没有 LitematicaBE 的 ghost 观感。
- LeviLamina 端通过 HologramLib 1.37.0 的 `#networkId` 假方块接口保留完整方块状态；
  Endstone 的 `fakeblock` 同样支持完整状态。
- 轻松放置 / 打印机 / Mega 分块未实现；保存功能已接。
- 保存时会把基岩状态反向转成 Java 状态（楼梯/门/活板门/床/按钮/拉杆/铁轨/中继器/比较器/
  朝向方块/台阶/原木/墙/栅栏/玻璃板等）；少数复杂方块仍可能退回默认状态。

MIT。映射数据来自 [LitematicaBE](https://github.com/copper-lamp/LitematicaBE)，
压缩使用 [miniz](https://github.com/richgel999/miniz)。详见 `THIRD_PARTY_NOTICES.md`。
