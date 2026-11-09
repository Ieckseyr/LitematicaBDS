# 方块映射

`.litematic` 的 palette 是 Java 名/状态；基岩版名字与状态键不同，加载时由
`BlockMapper` 转成基岩状态串。

## 数据

`data/java_bedrock_map.json` / `core/mapping/BlockMapData.inc` 由
`tools/gen_block_map.mjs` 从 [LitematicaBE](https://github.com/copper-lamp/LitematicaBE)（MIT）
生成，870 条：

- `b`：基岩方块名
- `s`：Java 状态键 → 基岩状态键（仅 24 条，其余同名）
- `f`：方向/多部件/方块实体/颜色/木材标记

重新生成：

```bat
node tools/gen_block_map.mjs <LitematicaBE 仓库根目录>
```

## 转换

1. 查表；未知方块保留名字、丢弃属性。
2. 按 `s` 重命名状态键，再跑特殊转换（门/楼梯/台阶/活板门/床/拉杆/铁轨/红石/作物/
   炼药锅/蜡烛…）。
3. 方向兜底（南瓜/活塞/按钮/火把/通用 cardinal）。
4. 去掉 `waterlogged/persistent/distance/snowy` 等基岩无对应语义的键。

LeviLamina 端加载后用 `Block::tryGetFromRegistry` 校验；失败降级到基础方块名，再失败则
标记不可渲染。

## 保存（反向）

保存时 `BlockMapper::bedrockToJava` 会：

- 按 `wood_type` / `color` / `stone_type` 等选择正确的 Java 名字（现代扁平名字直接命中）；
- 反向转换楼梯、台阶、门、活板门、床、按钮、拉杆、栅栏门、铁轨、中继器、比较器、
  火把、朝向方块、活塞/观察者/漏斗/熔炉、箱子/木桶、原木轴、树叶、雪层、蛋糕、
  堆肥桶、液体、蜡烛、海泡菜、海龟蛋、甜浆果、红石线、告示牌、栅栏/墙/玻璃板；
- 丢弃基岩专有状态键，补齐 Java 必需键。

仍是近似的：复杂红石/多部件方块、双层台阶的精确形状、部分模组方块。最坏是默认状态或被跳过，不会崩溃。
