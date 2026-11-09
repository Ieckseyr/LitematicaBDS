# Third-party notices

LitematicaBDS 以 MIT 分发。下列第三方作品/数据被使用或改编：

## LitematicaBE

- 来源：https://github.com/copper-lamp/LitematicaBE
- 许可：MIT
- 版权：Copyright (c) 2024 LitematicaBE
- 使用范围：Java→Bedrock 方块映射数据（`data/java_bedrock_map.json`、
  `core/mapping/BlockMapData.inc`）与状态转换规则（`core/mapping/BlockMapper.cpp`），
  由 `tools/gen_block_map.mjs` 生成/移植。

## HologramLib

- 来源：https://github.com/Ieckseyr/HologramLib
- 许可：MIT
- 版权：Copyright (c) 2026 伊希娅
- 使用范围：作为运行时前置库；方块状态解析/`Block::tryGetFromRegistry` 校验方式与
  endstone 套壳的打包思路参考其实现。

## miniz

- 来源：https://github.com/richgel999/miniz
- 许可：public domain（Unlicense）
- 使用范围：`third_party/miniz/`，用于 gzip/deflate。

## LeviLamina / Bedrock Dedicated Server

- 本插件链接 LeviLamina（LGPL-3.0）并调用 BDS 导出符号；LeviLamina/BDS 本体不随本仓库分发。
