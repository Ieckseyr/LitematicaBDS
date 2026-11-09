#pragma once

// litematica_core 的 C ABI —— endstone（Python）套壳通过 ctypes 调用。
//
// 边界只传字节与数字：没有引擎对象、没有 C++ 容器。LL 版直接链接 C++ 核心，
// 不经过这个 ABI；两个平台共用同一份 core/ 实现。
//
// 所有函数都在服务器主线程调用（与 HologramLib 的约定一致），内部不加锁。

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#    define LITBDS_CORE_EXPORT __declspec(dllexport)
#else
#    define LITBDS_CORE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LITBDS_CORE_ABI_VERSION 1u

typedef struct litbds_core litbds_core;

// 一次渲染操作。kind: 0 = set（把 state 显示给玩家），1 = clear（还原真实方块）。
typedef struct litbds_change {
    int32_t kind;
    int32_t x;
    int32_t y;
    int32_t z;
    char    state[192];
    char    player[64];
} litbds_change;

LITBDS_CORE_EXPORT uint32_t    litbds_api_version(void);
LITBDS_CORE_EXPORT const char* litbds_version(void);

// data_dir 必填；schematics_dir / exports_dir 为空时使用 data_dir 下的子目录。
LITBDS_CORE_EXPORT litbds_core* litbds_create(
    const char* data_dir,
    const char* schematics_dir,
    const char* exports_dir
);
LITBDS_CORE_EXPORT void litbds_destroy(litbds_core* core);

// 0 = 成功；非 0 = 失败（litbds_last_error 取错误文本）。
LITBDS_CORE_EXPORT int32_t litbds_initialize(litbds_core* core);
LITBDS_CORE_EXPORT void    litbds_reload(litbds_core* core);

// 执行 /litematica 之后的参数串。返回 1 = 命令成功，0 = 命令失败，-1 = 参数错误。
// 输出行用 '\n' 连接写入 out，超长截断。
LITBDS_CORE_EXPORT int32_t litbds_execute(
    litbds_core* core,
    const char*  player,
    double       x,
    double       y,
    double       z,
    int32_t      dimension,
    int32_t      op,
    const char*  command,
    char*        out,
    size_t       out_capacity
);

// 平台方块采样器：返回 1 表示 out 写入了一个状态串；0 = 空气/读不到。
typedef int32_t (*litbds_block_sampler_fn)(
    const char* player,
    int32_t     x,
    int32_t     y,
    int32_t     z,
    int32_t     dimension,
    char*       out,
    size_t      capacity,
    void*       user
);

LITBDS_CORE_EXPORT void litbds_set_sampler(litbds_core* core, litbds_block_sampler_fn sampler, void* user);

LITBDS_CORE_EXPORT int32_t litbds_update_viewer(
    litbds_core* core,
    const char*  player,
    double       x,
    double       y,
    double       z,
    int32_t      dimension
);
LITBDS_CORE_EXPORT int32_t litbds_remove_viewer(litbds_core* core, const char* player);

// 推进一次渲染调度；结果由 litbds_poll 取出。
LITBDS_CORE_EXPORT int32_t litbds_tick(litbds_core* core, uint64_t now_ms);
LITBDS_CORE_EXPORT int32_t litbds_poll(litbds_core* core, litbds_change* out, size_t capacity);

// 停服/禁用：把已发出的假方块还原操作推进 outbox，Python 侧 poll 后发送。
LITBDS_CORE_EXPORT void litbds_shutdown(litbds_core* core);

LITBDS_CORE_EXPORT size_t litbds_schematic_count(litbds_core* core);
LITBDS_CORE_EXPORT int32_t litbds_schematic_name_at(litbds_core* core, size_t index, char* out, size_t capacity);

LITBDS_CORE_EXPORT const char* litbds_last_error(litbds_core* core);

#ifdef __cplusplus
} // extern "C"
#endif
