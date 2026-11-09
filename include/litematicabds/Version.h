#pragma once

// LitematicaBDS 版本与 ABI 常量。
//
// 版本号规则：<主>.<次>.<修订>。主版本变化 = 破坏性改动；次版本 = 新能力；
// 修订 = 修复。core C ABI 另有独立版本号（LITBDS_CORE_API_VERSION），
// 只在 C ABI 结构/函数签名变化时递增。

#define LITEMATICABDS_VERSION       "0.1.0"
#define LITEMATICABDS_VERSION_MAJOR 0
#define LITEMATICABDS_VERSION_MINOR 1
#define LITEMATICABDS_VERSION_PATCH 0

// 核心库 C ABI 版本（endstone 套壳按此握手）。
#define LITBDS_CORE_API_VERSION 1u

// Java→Bedrock 映射数据格式版本（data/java_bedrock_map.json 的 format）。
#define LITBDS_BLOCK_MAP_FORMAT 1u
