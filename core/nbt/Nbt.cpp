#include "litematicabds/Nbt.h"

#include <bit>
#include <cstring>
#include <limits>
#include <utility>

namespace litematicabds::nbt {
namespace {

class Reader {
public:
    Reader(std::span<std::uint8_t const> data, ParseOptions options) : mData(data), mOptions(options) {}

    Tag parseRoot() {
        std::uint8_t const type = readU8();
        if (type == 0) return Tag{};
        auto const name = readString(); // 根名字丢弃
        (void)name;
        auto tag = readPayload(type, 0);
        return tag;
    }

private:
    [[nodiscard]] bool remaining(std::size_t bytes) const noexcept { return mPos + bytes <= mData.size(); }

    void require(std::size_t bytes) const {
        if (!remaining(bytes)) throw NbtError("NBT 数据在偏移 " + std::to_string(mPos) + " 处截断");
    }

    std::uint8_t readU8() {
        require(1);
        return mData[mPos++];
    }

    std::uint16_t readU16() {
        require(2);
        auto const hi = static_cast<std::uint16_t>(mData[mPos]) << 8;
        auto const lo = static_cast<std::uint16_t>(mData[mPos + 1]);
        mPos += 2;
        return static_cast<std::uint16_t>(hi | lo);
    }

    std::int16_t readI16() { return static_cast<std::int16_t>(readU16()); }

    std::int32_t readI32() {
        require(4);
        std::uint32_t v = (static_cast<std::uint32_t>(mData[mPos]) << 24)
                        | (static_cast<std::uint32_t>(mData[mPos + 1]) << 16)
                        | (static_cast<std::uint32_t>(mData[mPos + 2]) << 8)
                        | static_cast<std::uint32_t>(mData[mPos + 3]);
        mPos += 4;
        return static_cast<std::int32_t>(v);
    }

    std::int64_t readI64() {
        require(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<std::uint64_t>(mData[mPos + static_cast<std::size_t>(i)]);
        mPos += 8;
        return static_cast<std::int64_t>(v);
    }

    std::string readString() {
        auto const len = readU16();
        if (len > mOptions.maxString) throw NbtError("NBT 字符串超过上限");
        require(len);
        std::string out(reinterpret_cast<char const*>(mData.data() + mPos), len);
        mPos += len;
        return out;
    }

    Tag readPayload(std::uint8_t type, std::size_t depth) {
        if (depth > mOptions.maxDepth) throw NbtError("NBT 嵌套超过上限");
        if (type > 12) throw NbtError("未知 NBT 标签类型 " + std::to_string(type));

        Tag tag;
        tag.type = static_cast<Type>(type);
        switch (tag.type) {
        case Type::Byte:
            tag.value = static_cast<std::int8_t>(readU8());
            break;
        case Type::Short:
            tag.value = readI16();
            break;
        case Type::Int:
            tag.value = readI32();
            break;
        case Type::Long:
            tag.value = readI64();
            break;
        case Type::Float:
            tag.value = std::bit_cast<float>(static_cast<std::uint32_t>(readI32()));
            break;
        case Type::Double:
            tag.value = std::bit_cast<double>(static_cast<std::uint64_t>(readI64()));
            break;
        case Type::ByteArray: {
            auto const len = readI32();
            if (len < 0 || static_cast<std::uint64_t>(len) > mOptions.maxElements) {
                throw NbtError("NBT ByteArray 长度非法");
            }
            require(static_cast<std::size_t>(len));
            Tag::ByteArray bytes(mData.begin() + static_cast<std::ptrdiff_t>(mPos),
                                 mData.begin() + static_cast<std::ptrdiff_t>(mPos) + len);
            mPos += static_cast<std::size_t>(len);
            tag.value = std::move(bytes);
            break;
        }
        case Type::String:
            tag.value = readString();
            break;
        case Type::List: {
            auto const elementType = readU8();
            if (elementType > 12) throw NbtError("NBT List 元素类型非法");
            auto const len = readI32();
            if (len < 0 || static_cast<std::uint64_t>(len) > mOptions.maxElements) {
                throw NbtError("NBT List 长度非法");
            }
            List list;
            list.elementType = static_cast<Type>(elementType);
            list.items.reserve(static_cast<std::size_t>(len));
            for (std::int32_t i = 0; i < len; ++i) {
                list.items.push_back(readPayload(elementType, depth + 1));
            }
            tag.value = std::move(list);
            break;
        }
        case Type::Compound: {
            Compound compound;
            for (;;) {
                auto const childType = readU8();
                if (childType == 0) {
                    break;
                }
                if (childType > 12) throw NbtError("NBT Compound 子标签类型非法");
                auto const name = readString();
                compound.keys.push_back(name);
                compound.values.push_back(readPayload(childType, depth + 1));
            }
            tag.value = std::move(compound);
            break;
        }
        case Type::IntArray: {
            auto const len = readI32();
            if (len < 0 || static_cast<std::uint64_t>(len) > mOptions.maxElements) {
                throw NbtError("NBT IntArray 长度非法");
            }
            Tag::IntArray values;
            values.reserve(static_cast<std::size_t>(len));
            for (std::int32_t i = 0; i < len; ++i) values.push_back(readI32());
            tag.value = std::move(values);
            break;
        }
        case Type::LongArray: {
            auto const len = readI32();
            if (len < 0 || static_cast<std::uint64_t>(len) > mOptions.maxElements) {
                throw NbtError("NBT LongArray 长度非法");
            }
            Tag::LongArray values;
            values.reserve(static_cast<std::size_t>(len));
            for (std::int32_t i = 0; i < len; ++i) values.push_back(static_cast<std::uint64_t>(readI64()));
            tag.value = std::move(values);
            break;
        }
        case Type::End:
        default:
            throw NbtError("非法 NBT 根标签");
        }
        return tag;
    }

    std::span<std::uint8_t const> mData;
    ParseOptions                 mOptions;
    std::size_t                  mPos{0};
};

class Writer {
public:
    std::vector<std::uint8_t> run(std::string_view rootName, Tag const& root) {
        if (root.type == Type::End) return {};
        writeU8(static_cast<std::uint8_t>(root.type));
        writeString(rootName);
        writePayload(root);
        return std::move(mOut);
    }

private:
    void writeU8(std::uint8_t v) { mOut.push_back(v); }

    void writeI16(std::int16_t v) {
        auto const u = static_cast<std::uint16_t>(v);
        mOut.push_back(static_cast<std::uint8_t>(u >> 8));
        mOut.push_back(static_cast<std::uint8_t>(u));
    }

    void writeI32(std::int32_t v) {
        auto const u = static_cast<std::uint32_t>(v);
        for (int shift = 24; shift >= 0; shift -= 8) mOut.push_back(static_cast<std::uint8_t>(u >> shift));
    }

    void writeI64(std::int64_t v) {
        auto const u = static_cast<std::uint64_t>(v);
        for (int shift = 56; shift >= 0; shift -= 8) mOut.push_back(static_cast<std::uint8_t>(u >> shift));
    }

    void writeString(std::string_view s) {
        if (s.size() > 0xFFFF) throw NbtError("NBT 字符串过长");
        writeI16(static_cast<std::int16_t>(s.size()));
        mOut.insert(mOut.end(), s.begin(), s.end());
    }

    void writePayload(Tag const& tag) {
        switch (tag.type) {
        case Type::Byte:
            writeU8(static_cast<std::uint8_t>(std::get<std::int8_t>(tag.value)));
            break;
        case Type::Short:
            writeI16(std::get<std::int16_t>(tag.value));
            break;
        case Type::Int:
            writeI32(std::get<std::int32_t>(tag.value));
            break;
        case Type::Long:
            writeI64(std::get<std::int64_t>(tag.value));
            break;
        case Type::Float:
            writeI32(static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(std::get<float>(tag.value))));
            break;
        case Type::Double:
            writeI64(static_cast<std::int64_t>(std::bit_cast<std::uint64_t>(std::get<double>(tag.value))));
            break;
        case Type::ByteArray: {
            auto const& bytes = std::get<Tag::ByteArray>(tag.value);
            writeI32(static_cast<std::int32_t>(bytes.size()));
            mOut.insert(mOut.end(), bytes.begin(), bytes.end());
            break;
        }
        case Type::String: {
            auto const& s = std::get<std::string>(tag.value);
            writeString(s);
            break;
        }
        case Type::List: {
            auto const& list = std::get<List>(tag.value);
            writeU8(static_cast<std::uint8_t>(list.elementType));
            writeI32(static_cast<std::int32_t>(list.items.size()));
            for (auto const& item : list.items) {
                if (item.type != list.elementType && !(list.items.empty())) {
                    throw NbtError("NBT List 元素类型不一致");
                }
                writePayload(item);
            }
            break;
        }
        case Type::Compound: {
            auto const& compound = std::get<Compound>(tag.value);
            for (std::size_t i = 0; i < compound.keys.size(); ++i) {
                auto const& child = compound.values[i];
                writeU8(static_cast<std::uint8_t>(child.type));
                writeString(compound.keys[i]);
                writePayload(child);
            }
            writeU8(0);
            break;
        }
        case Type::IntArray: {
            auto const& values = std::get<Tag::IntArray>(tag.value);
            writeI32(static_cast<std::int32_t>(values.size()));
            for (auto v : values) writeI32(v);
            break;
        }
        case Type::LongArray: {
            auto const& values = std::get<Tag::LongArray>(tag.value);
            writeI32(static_cast<std::int32_t>(values.size()));
            for (auto v : values) writeI64(static_cast<std::int64_t>(v));
            break;
        }
        case Type::End:
        default:
            break;
        }
    }

    std::vector<std::uint8_t> mOut;
};

} // namespace

Tag const* Compound::find(std::string_view key) const noexcept {
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) return &values[i];
    }
    return nullptr;
}

Tag* Compound::find(std::string_view key) noexcept {
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) return &values[i];
    }
    return nullptr;
}

bool Compound::contains(std::string_view key) const noexcept { return find(key) != nullptr; }

Compound& Compound::set(std::string key, Tag value) {
    if (auto* existing = find(key); existing != nullptr) {
        *existing = std::move(value);
    } else {
        keys.push_back(std::move(key));
        values.push_back(std::move(value));
    }
    return *this;
}

Compound& Compound::set(std::string key, std::string value) {
    return set(std::move(key), Tag::makeString(std::move(value)));
}
Compound& Compound::set(std::string key, std::int32_t value) { return set(std::move(key), Tag::makeInt(value)); }
Compound& Compound::set(std::string key, std::int64_t value) { return set(std::move(key), Tag::makeLong(value)); }

Tag const& Compound::at(std::string_view key) const {
    auto const* tag = find(key);
    if (tag == nullptr) throw NbtError("NBT 缺少字段: " + std::string(key));
    return *tag;
}

Tag& Compound::at(std::string_view key) {
    auto* tag = find(key);
    if (tag == nullptr) throw NbtError("NBT 缺少字段: " + std::string(key));
    return *tag;
}

bool Tag::isNumber() const noexcept {
    switch (type) {
    case Type::Byte:
    case Type::Short:
    case Type::Int:
    case Type::Long:
    case Type::Float:
    case Type::Double:
        return true;
    default:
        return false;
    }
}

Compound const& Tag::asCompound() const {
    if (type != Type::Compound) throw NbtError("NBT 标签不是 Compound");
    return std::get<Compound>(value);
}

Compound& Tag::asCompound() {
    if (type != Type::Compound) throw NbtError("NBT 标签不是 Compound");
    return std::get<Compound>(value);
}

List const& Tag::asList() const {
    if (type != Type::List) throw NbtError("NBT 标签不是 List");
    return std::get<List>(value);
}

List& Tag::asList() {
    if (type != Type::List) throw NbtError("NBT 标签不是 List");
    return std::get<List>(value);
}

std::string const& Tag::asString() const {
    if (type != Type::String) throw NbtError("NBT 标签不是 String");
    return std::get<std::string>(value);
}

std::string& Tag::asString() {
    if (type != Type::String) throw NbtError("NBT 标签不是 String");
    return std::get<std::string>(value);
}

Tag::LongArray const& Tag::asLongArray() const {
    if (type != Type::LongArray) throw NbtError("NBT 标签不是 LongArray");
    return std::get<LongArray>(value);
}

std::int64_t Tag::asInt64(std::int64_t fallback) const noexcept {
    switch (type) {
    case Type::Byte:   return std::get<std::int8_t>(value);
    case Type::Short:  return std::get<std::int16_t>(value);
    case Type::Int:    return std::get<std::int32_t>(value);
    case Type::Long:   return std::get<std::int64_t>(value);
    case Type::Float:  return static_cast<std::int64_t>(std::get<float>(value));
    case Type::Double: return static_cast<std::int64_t>(std::get<double>(value));
    default:           return fallback;
    }
}

double Tag::asDouble(double fallback) const noexcept {
    switch (type) {
    case Type::Byte:   return static_cast<double>(std::get<std::int8_t>(value));
    case Type::Short:  return static_cast<double>(std::get<std::int16_t>(value));
    case Type::Int:    return static_cast<double>(std::get<std::int32_t>(value));
    case Type::Long:   return static_cast<double>(std::get<std::int64_t>(value));
    case Type::Float:  return static_cast<double>(std::get<float>(value));
    case Type::Double: return std::get<double>(value);
    default:           return fallback;
    }
}

bool Tag::asBool(bool fallback) const noexcept {
    if (type == Type::Byte) return std::get<std::int8_t>(value) != 0;
    if (isNumber()) return asInt64(0) != 0;
    if (type == Type::String) {
        auto const& s = std::get<std::string>(value);
        if (s == "true" || s == "1") return true;
        if (s == "false" || s == "0") return false;
    }
    return fallback;
}

Tag Tag::makeByte(std::int8_t v) {
    Tag t;
    t.type  = Type::Byte;
    t.value = v;
    return t;
}
Tag Tag::makeShort(std::int16_t v) {
    Tag t;
    t.type  = Type::Short;
    t.value = v;
    return t;
}
Tag Tag::makeInt(std::int32_t v) {
    Tag t;
    t.type  = Type::Int;
    t.value = v;
    return t;
}
Tag Tag::makeLong(std::int64_t v) {
    Tag t;
    t.type  = Type::Long;
    t.value = v;
    return t;
}
Tag Tag::makeFloat(float v) {
    Tag t;
    t.type  = Type::Float;
    t.value = v;
    return t;
}
Tag Tag::makeDouble(double v) {
    Tag t;
    t.type  = Type::Double;
    t.value = v;
    return t;
}
Tag Tag::makeString(std::string v) {
    Tag t;
    t.type  = Type::String;
    t.value = std::move(v);
    return t;
}
Tag Tag::makeByteArray(ByteArray v) {
    Tag t;
    t.type  = Type::ByteArray;
    t.value = std::move(v);
    return t;
}
Tag Tag::makeIntArray(IntArray v) {
    Tag t;
    t.type  = Type::IntArray;
    t.value = std::move(v);
    return t;
}
Tag Tag::makeLongArray(LongArray v) {
    Tag t;
    t.type  = Type::LongArray;
    t.value = std::move(v);
    return t;
}
Tag Tag::makeList(Type elementType, std::vector<Tag> items) {
    Tag t;
    t.type = Type::List;
    List list;
    list.elementType = elementType;
    list.items       = std::move(items);
    t.value          = std::move(list);
    return t;
}
Tag Tag::makeCompound(Compound c) {
    Tag t;
    t.type  = Type::Compound;
    t.value = std::move(c);
    return t;
}

Result<Tag> parse(std::span<std::uint8_t const> data, ParseOptions const& options) {
    try {
        Reader reader(data, options);
        return reader.parseRoot();
    } catch (NbtError const& e) {
        return Error(e.what());
    } catch (std::exception const& e) {
        return Error(std::string("NBT 解析异常: ") + e.what());
    }
}

std::vector<std::uint8_t> serialize(std::string_view rootName, Tag const& root) {
    Writer writer;
    return writer.run(rootName, root);
}

} // namespace litematicabds::nbt
