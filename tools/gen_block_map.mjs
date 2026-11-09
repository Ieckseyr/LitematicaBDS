#!/usr/bin/env node
/**
 * gen_block_map.mjs — 从 LitematicaBE 的 BlockMappingRegistry.js 生成 LitematicaBDS 的
 * Java→Bedrock 方块映射表。
 *
 * 用法:
 *   node tools/gen_block_map.mjs <LitematicaBE 仓库根目录>
 *
 * 产物:
 *   data/java_bedrock_map.json        可读映射数据（诊断/文档/回归用）
 *   core/mapping/BlockMapData.inc     编译期常量表（核心库直接 include）
 *
 * 数据来源: https://github.com/copper-lamp/LitematicaBE (MIT)
 * 生成物同样以 MIT 分发；修改请改生成器而不是 .inc。
 */
import { createRequire } from 'node:module';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const src = process.argv[2];
if (!src) {
    console.error('用法: node tools/gen_block_map.mjs <LitematicaBE 仓库根目录>');
    process.exit(2);
}

const require = createRequire(join(resolve(src), 'noop.cjs'));
const { javaToBedrock } = require(join(resolve(src), 'src/mappings/BlockMappingRegistry.js'));

const entries = Object.entries(javaToBedrock)
    .filter(([, v]) => v && typeof v.b === 'string' && v.b.length > 0)
    .sort(([a], [b]) => a.localeCompare(b));

const json = {
    format: 1,
    source: 'https://github.com/copper-lamp/LitematicaBE (MIT)',
    generatedBy: 'tools/gen_block_map.mjs',
    note: 'b = Bedrock block id without namespace; s = Java state key -> Bedrock state key; f bit0=directional, bit1=multipart, bit2=tileentity, bit3=color, bit4=wood',
    count: entries.length,
    entries: Object.fromEntries(
        entries.map(([java, v]) => [
            java,
            {
                b: v.b,
                s: v.s ?? null,
                f: v.f ?? 0,
                c: v.c ?? 'unknown',
                v: v.v ?? '',
            },
        ])
    ),
};
mkdirSync(join(root, 'data'), { recursive: true });
writeFileSync(join(root, 'data/java_bedrock_map.json'), JSON.stringify(json, null, 2) + '\n');

const esc = (s) => String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
const rows = entries.map(([java, v]) => {
    const stateMap = v.s
        ? Object.entries(v.s)
              .map(([k, val]) => `${k}:${val}`)
              .join(',')
        : '';
    return `    {"${esc(java)}", "${esc(v.b)}", "${esc(stateMap)}", ${v.f ?? 0}, "${esc(v.c ?? 'unknown')}"},`;
});

const out = `// 由 tools/gen_block_map.mjs 生成，勿手改。
// 数据来源: LitematicaBE (https://github.com/copper-lamp/LitematicaBE, MIT)。
// b = Bedrock 方块名（不带 minecraft: 前缀）; s = Java 状态键 -> Bedrock 状态键;
// f = 方向/多部件/方块实体/颜色/木材 标记; c = 分类。
#pragma once

namespace litematicabds::mapping::detail {

struct MapEntry {
    char const* javaName;
    char const* bedrockName;
    char const* stateMap; // "javaKey:beKey,javaKey2:beKey2"
    unsigned char flags;
    char const* category;
};

inline constexpr MapEntry kBlockMap[] = {
${rows.join('\n')}
};

inline constexpr unsigned kBlockMapCount = sizeof(kBlockMap) / sizeof(kBlockMap[0]);

} // namespace litematicabds::mapping::detail
`;

writeFileSync(join(root, 'core/mapping/BlockMapData.inc'), out);
console.log(`[gen_block_map] ${entries.length} entries`);
console.log(`  data/java_bedrock_map.json`);
console.log(`  core/mapping/BlockMapData.inc`);
