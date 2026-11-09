# Changelog

## [0.1.0] - 2026-11-07

首个可运行版本：纯服务端投影闭环。

### Added

- **核心**（`core/`，平台无关）
  - Big-Endian NBT 读写（12 种标签、深度/长度上限）
  - `.litematic` 解析：gzip、多区域 + Position 偏移、调色板、位打包解码
  - `.litematic` 写出：NBT + 位打包 + gzip（单测覆盖）
  - Java→Bedrock 方块映射：870 项生成表 + 朝向/楼梯/门/活板门/床/拉杆/栅栏门/铁轨/
    中继器/比较器/作物/红石等状态转换
  - 投影模型：16³ 分块索引、表面剔除、距离/层过滤、最近优先上限
  - 渲染调度：逐玩家会话、差异重规划、每 tick set/clear 预算、退出还原
  - 配置（允许 `//`、`/* */`、尾随逗号）、原理图仓库、材料统计 CSV
  - 命令解析：list/load/place/clear/layer/rot/mirror/info/materials/reload/status/selfcheck
  - endstone C ABI（`litbds_*`）
- **LeviLamina 插件**
  - `/litematica`（别名 `/lit`）命令注册，含 `placeat`
  - 木棍右键打开投影菜单（LeviLamina Form API，客户端零安装）；木棍左键快速放置当前投影
  - 分层新增 `slice`：只显示玩家脚部坐标所在单层，可手动 `layer <n>` / 菜单翻层
  - 靠近投影方块保留完整方块外观 + DebugShape 框；硫磺立方体只做隐身点击载体
  - 成功填充后移除框与载体；草方块映射修正
  - 管理员配置表单：提交写回 config.json 并触发重规划；自检命令/启动自检移除
  - 硫磺范围默认 8，新增 faceBlock 朝向跟随
  - `pos1`/`pos2` 选区盒线框 + 尺寸悬浮字（RGB 循环色）
  - 保存：基岩→Java 反向状态转换覆盖楼梯/门/活板门/床/按钮/拉杆/铁轨/中继器/比较器/朝向方块/台阶/原木/墙/栅栏/玻璃板等
  - 手持对应方块左键点击硫磺怪 → 真实放置方块（生存消耗 1 个，创造不消耗）
  - 每 tick 喂玩家位置、`PlayerDisconnectEvent` 清理
  - `IGhostSink` → HologramLib `IViewOverride::overrideBlock/clearBlock`
  - 用 BDS 方块注册表校验映射状态，启动自检 6 个样例
  - 内存分配运算符适配（LL 加载必需）
- **endstone 套壳**
  - ctypes 绑定 + Python 插件 + 每 tick `fakeblock.set/clear`
  - wheel 打包（内嵌 `litematica_core.dll`）
  - 与 LeviLamina 共用同一份核心与命令
- **映射自检**：LeviLamina 启动时把 800 个基岩方块名逐个丢给 BDS 注册表，当前 800/800 存在；
  1.21 改名的 6 个方块（dirt_path/terracotta/end_stone_brick_*/small_dripleaf/prismarine_brick_stairs）
  在运行期别名表修正，炼药锅补 `cauldron_liquid`/`fill_level` 转换
- **工程**
  - xmake 构建（`--with_native=n` 可只构建核心/单测）
  - 33 个单测 / 193 项断言；独立 Python 生成 `.litematic` 夹具
  - `tools/gen_block_map.mjs`、`tools/make_test_fixture.py`、`tools/pack_endstone.py`、
    `tools/check_version.py`
  - README、架构、命令文档、AGENTS、CI

### Known limitations

- 基岩版协议层不能逐客户端显示半透明方块，投影以真实方块外观显示；没有 LitematicaBE
  的“半透明 ghost”观感。
- HologramLib 26.60.3 的 C++ `IViewOverride` 只接受方块名；LeviLamina 端目前退回基础名渲染，
  朝向类方块是默认状态。endstone 端 `fakeblock` 支持完整状态。
- 轻松放置 / 投影打印机 / 表单 UI / Mega 分块加载未实现。
- 反向映射（基岩→Java）对“一个基岩名对应多个 Java 名”的方块（如 `planks`）只能
  选第一个候选；保存功能接线时需要按状态/木材类型细化。
