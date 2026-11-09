#pragma once

// 最小但完整的 NBT（Big Endian，Java 版格式）读写。
//
// 只服务于 .litematic：需要 TAG_Compound / TAG_List / TAG_Long_Array / TAG_String 等
// 全部 12 种标签（真实文件里都出现过）。解析带深度/元素/字符串上限，避免恶意文件
// 把服务端内存打爆。序列化用于保存原理图。
//
// 设计取舍：
//  · Compound 用「键数组 + 值数组」而不是 map<string, Tag>，因为 Tag 在定义
//    Compound 时尚未完整；vector<Tag> 允许不完整类型（C++17），且查找顺序稳定。
//  · LongArray 存 uint64_t：litematic 的位打包只关心低位，无符号掩码更直观。

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "litematicabds/Result.h"

namespace litematicabds::nbt {

enum class Type : std::uint8_t {
    End       = 0,
    Byte      = 1,
    Short     = 2,
    Int       = 3,
    Long      = 4,
    Float     = 5,
    Double    = 6,
    ByteArray = 7,
    String    = 8,
    List      = 9,
    Compound  = 10,
    IntArray  = 11,
    LongArray = 12,
};

class Tag;

struct List {
    Type             elementType{Type::End};
    std::vector<Tag> items;
};

struct Compound {
    std::vector<std::string> keys;
    std::vector<Tag>         values;

    [[nodiscard]] Tag const* find(std::string_view key) const noexcept;
    [[nodiscard]] Tag*       find(std::string_view key) noexcept;
    [[nodiscard]] bool       contains(std::string_view key) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return keys.size(); }

    Compound& set(std::string key, Tag value);
    Compound& set(std::string key, std::string value);
    Compound& set(std::string key, std::int32_t value);
    Compound& set(std::string key, std::int64_t value);

    [[nodiscard]] Tag const& at(std::string_view key) const;
    [[nodiscard]] Tag&       at(std::string_view key);
};

class NbtError : public std::runtime_error {
public:
    explicit NbtError(std::string const& message) : std::runtime_error(message) {}
};

class Tag {
public:
    using ByteArray = std::vector<std::uint8_t>;
    using IntArray  = std::vector<std::int32_t>;
    using LongArray = std::vector<std::uint64_t>;
    using Value = std::variant<
        std::monostate,
        std::int8_t,
        std::int16_t,
        std::int32_t,
        std::int64_t,
        float,
        double,
        ByteArray,
        std::string,
        List,
        Compound,
        IntArray,
        LongArray>;

    Type  type{Type::End};
    Value value{};

    Tag() = default;

    [[nodiscard]] bool isEnd() const noexcept { return type == Type::End; }
    [[nodiscard]] bool isNumber() const noexcept;
    [[nodiscard]] bool isCompound() const noexcept { return type == Type::Compound; }
    [[nodiscard]] bool isList() const noexcept { return type == Type::List; }
    [[nodiscard]] bool isString() const noexcept { return type == Type::String; }

    [[nodiscard]] Compound const& asCompound() const;
    [[nodiscard]] Compound&       asCompound();
    [[nodiscard]] List const&     asList() const;
    [[nodiscard]] List&           asList();
    [[nodiscard]] std::string const& asString() const;
    [[nodiscard]] std::string&       asString();
    [[nodiscard]] LongArray const&   asLongArray() const;

    [[nodiscard]] std::int64_t asInt64(std::int64_t fallback = 0) const noexcept;
    [[nodiscard]] double       asDouble(double fallback = 0.0) const noexcept;
    [[nodiscard]] bool         asBool(bool fallback = false) const noexcept;

    static Tag makeByte(std::int8_t v);
    static Tag makeShort(std::int16_t v);
    static Tag makeInt(std::int32_t v);
    static Tag makeLong(std::int64_t v);
    static Tag makeFloat(float v);
    static Tag makeDouble(double v);
    static Tag makeString(std::string v);
    static Tag makeByteArray(ByteArray v);
    static Tag makeIntArray(IntArray v);
    static Tag makeLongArray(LongArray v);
    static Tag makeList(Type elementType, std::vector<Tag> items = {});
    static Tag makeCompound(Compound c = {});
};

struct ParseOptions {
    std::size_t maxDepth{96};
    std::size_t maxElements{50'000'000}; // List/数组元素总数上限
    std::size_t maxString{16u * 1024u * 1024u};
};

// 解析一个「有名字的根标签」，返回根标签的值（根名字丢弃）。
// 数据必须是未压缩的 NBT 字节（.litematic 先 gunzip）。
[[nodiscard]] Result<Tag> parse(std::span<std::uint8_t const> data, ParseOptions const& options = {});

// 序列化：rootName 通常是空串（litematic 根标签无名字）。
[[nodiscard]] std::vector<std::uint8_t> serialize(std::string_view rootName, Tag const& root);

} // namespace litematicabds::nbt
