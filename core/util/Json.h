#pragma once

// 配置用的最小 JSON DOM。故意做小：只支持对象/数组/字符串/数字/布尔/null，
// 但允许 // 与 /* */ 注释和尾随逗号——用户手写 config.json 时最常见的两种"错误"。

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "litematicabds/Result.h"

namespace litematicabds::util {

class Json {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type        type{Type::Null};
    bool        boolValue{false};
    double      numberValue{0.0};
    std::string stringValue;
    std::vector<Json>                        arrayValue;
    std::vector<std::pair<std::string, Json>> objectValue;

    [[nodiscard]] bool isObject() const noexcept { return type == Type::Object; }
    [[nodiscard]] bool isArray() const noexcept { return type == Type::Array; }
    [[nodiscard]] bool isString() const noexcept { return type == Type::String; }
    [[nodiscard]] bool isNumber() const noexcept { return type == Type::Number; }
    [[nodiscard]] bool isBool() const noexcept { return type == Type::Bool; }

    [[nodiscard]] Json const* find(std::string_view key) const;

    [[nodiscard]] bool        getBool(std::string_view key, bool fallback) const;
    [[nodiscard]] int         getInt(std::string_view key, int fallback) const;
    [[nodiscard]] double      getDouble(std::string_view key, double fallback) const;
    [[nodiscard]] std::string getString(std::string_view key, std::string fallback) const;
};

[[nodiscard]] Result<Json> parseJson(std::string_view text);

} // namespace litematicabds::util
