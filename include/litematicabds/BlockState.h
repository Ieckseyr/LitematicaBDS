#pragma once

// 方块状态：名字 + 规范化后的属性串。
//
// 属性串格式固定为 "k=v,k2=v2"，按键名升序排列，值原样保留（数字/布尔/字符串都在字符串层）。
// 这样可以做字符串去重与稳定输出，不丢类型信息；写进 HologramLib 的 BlockView.type 时
// 直接拼成 "minecraft:oak_stairs[upside_down_bit=true]"。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace litematicabds {

struct BlockState {
    std::string name;       // 例如 minecraft:oak_stairs
    std::string properties; // 规范化的 "half=bottom,upside_down_bit=false"

    [[nodiscard]] bool isAir() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return name.empty(); }

    // "minecraft:oak_stairs[half=bottom]"；没有属性时省略方括号。
    [[nodiscard]] std::string toString() const;

    friend bool operator==(BlockState const& a, BlockState const& b) noexcept {
        return a.name == b.name && a.properties == b.properties;
    }
};

// 解析 "minecraft:stone" / "minecraft:oak_stairs[a=b,c=d]"。失败返回 nullopt。
[[nodiscard]] std::optional<BlockState> parseBlockState(std::string_view text);

// 把 "z=1,a=x" 规范化成 "a=x,z=1"；重复键保留最后一个。
[[nodiscard]] std::string canonicalProperties(std::string_view properties);

// 属性串 -> 键值对（已排序）。
[[nodiscard]] std::vector<std::pair<std::string, std::string>> splitProperties(std::string_view properties);

// 去掉 "minecraft:" 前缀（Bedrock 侧部分 API 用短名）。
[[nodiscard]] std::string_view stripNamespace(std::string_view name) noexcept;

// 补上 "minecraft:" 前缀。
[[nodiscard]] std::string withNamespace(std::string_view name);

// 判断两个属性值是否等价（"true" == "1"，数字字符串忽略前导零）。
[[nodiscard]] bool stateValueEquals(std::string_view a, std::string_view b) noexcept;

} // namespace litematicabds
