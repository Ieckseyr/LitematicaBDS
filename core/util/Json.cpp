#include "Json.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace litematicabds::util {
namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : mText(text) {}

    Json parse() {
        skipSpace();
        Json value = parseValue(0);
        skipSpace();
        if (mPos != mText.size()) throw std::runtime_error("JSON 末尾存在多余内容");
        return value;
    }

private:
    [[nodiscard]] bool eof() const noexcept { return mPos >= mText.size(); }
    [[nodiscard]] char peek() const {
        if (eof()) throw std::runtime_error("JSON 意外结束");
        return mText[mPos];
    }

    void skipSpace() {
        for (;;) {
            while (!eof() && std::isspace(static_cast<unsigned char>(mText[mPos]))) ++mPos;
            if (mPos + 1 < mText.size() && mText[mPos] == '/' && mText[mPos + 1] == '/') {
                mPos += 2;
                while (!eof() && mText[mPos] != '\n') ++mPos;
                continue;
            }
            if (mPos + 1 < mText.size() && mText[mPos] == '/' && mText[mPos + 1] == '*') {
                mPos += 2;
                while (mPos + 1 < mText.size() && !(mText[mPos] == '*' && mText[mPos + 1] == '/')) ++mPos;
                if (mPos + 1 >= mText.size()) throw std::runtime_error("JSON 块注释未闭合");
                mPos += 2;
                continue;
            }
            break;
        }
    }

    Json parseValue(std::size_t depth) {
        if (depth > 64) throw std::runtime_error("JSON 嵌套过深");
        skipSpace();
        if (eof()) throw std::runtime_error("JSON 缺少值");

        char const c = peek();
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') {
            Json value;
            value.type        = Json::Type::String;
            value.stringValue = parseString();
            return value;
        }
        if (c == 't') {
            expect("true");
            Json value;
            value.type      = Json::Type::Bool;
            value.boolValue = true;
            return value;
        }
        if (c == 'f') {
            expect("false");
            Json value;
            value.type      = Json::Type::Bool;
            value.boolValue = false;
            return value;
        }
        if (c == 'n') {
            expect("null");
            return Json{};
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();
        throw std::runtime_error("JSON 值非法");
    }

    Json parseObject(std::size_t depth) {
        Json value;
        value.type = Json::Type::Object;
        ++mPos; // {
        skipSpace();
        if (!eof() && peek() == '}') {
            ++mPos;
            return value;
        }
        for (;;) {
            skipSpace();
            if (eof() || peek() != '"') throw std::runtime_error("JSON 对象键必须是字符串");
            std::string key = parseString();
            skipSpace();
            if (eof() || mText[mPos] != ':') throw std::runtime_error("JSON 对象缺少 ':'");
            ++mPos;
            Json child = parseValue(depth + 1);
            value.objectValue.emplace_back(std::move(key), std::move(child));
            skipSpace();
            if (eof()) throw std::runtime_error("JSON 对象未闭合");
            if (mText[mPos] == ',') {
                ++mPos;
                skipSpace();
                if (!eof() && peek() == '}') { // 尾随逗号
                    ++mPos;
                    return value;
                }
                continue;
            }
            if (mText[mPos] == '}') {
                ++mPos;
                return value;
            }
            throw std::runtime_error("JSON 对象分隔符非法");
        }
    }

    Json parseArray(std::size_t depth) {
        Json value;
        value.type = Json::Type::Array;
        ++mPos; // [
        skipSpace();
        if (!eof() && peek() == ']') {
            ++mPos;
            return value;
        }
        for (;;) {
            value.arrayValue.push_back(parseValue(depth + 1));
            skipSpace();
            if (eof()) throw std::runtime_error("JSON 数组未闭合");
            if (mText[mPos] == ',') {
                ++mPos;
                skipSpace();
                if (!eof() && peek() == ']') { // 尾随逗号
                    ++mPos;
                    return value;
                }
                continue;
            }
            if (mText[mPos] == ']') {
                ++mPos;
                return value;
            }
            throw std::runtime_error("JSON 数组分隔符非法");
        }
    }

    Json parseNumber() {
        std::size_t const start = mPos;
        if (peek() == '-') ++mPos;
        while (!eof() && std::isdigit(static_cast<unsigned char>(mText[mPos]))) ++mPos;
        if (!eof() && mText[mPos] == '.') {
            ++mPos;
            while (!eof() && std::isdigit(static_cast<unsigned char>(mText[mPos]))) ++mPos;
        }
        if (!eof() && (mText[mPos] == 'e' || mText[mPos] == 'E')) {
            ++mPos;
            if (!eof() && (mText[mPos] == '+' || mText[mPos] == '-')) ++mPos;
            while (!eof() && std::isdigit(static_cast<unsigned char>(mText[mPos]))) ++mPos;
        }
        std::string token(mText.substr(start, mPos - start));
        char*       end = nullptr;
        errno           = 0;
        double const number = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || errno == ERANGE) throw std::runtime_error("JSON 数字非法");
        Json value;
        value.type        = Json::Type::Number;
        value.numberValue = number;
        return value;
    }

    std::string parseString() {
        ++mPos; // "
        std::string out;
        while (!eof()) {
            char const c = mText[mPos++];
            if (c == '"') return out;
            if (c == '\\') {
                if (eof()) throw std::runtime_error("JSON 字符串转义未完成");
                char const esc = mText[mPos++];
                switch (esc) {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    if (mPos + 4 > mText.size()) throw std::runtime_error("JSON \\u 转义不完整");
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        char const h = mText[mPos++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else throw std::runtime_error("JSON \\u 转义非法");
                    }
                    // 只处理 BMP 的基本 UTF-8 编码；代理对按替换字符处理。
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else if (code >= 0xD800 && code <= 0xDFFF) {
                        out.push_back('?');
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: throw std::runtime_error("JSON 未知转义");
                }
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x20) throw std::runtime_error("JSON 字符串含控制字符");
            out.push_back(c);
        }
        throw std::runtime_error("JSON 字符串未闭合");
    }

    void expect(std::string_view word) {
        if (mText.substr(mPos, word.size()) != word) throw std::runtime_error("JSON 字面量非法");
        mPos += word.size();
    }

    std::string_view mText;
    std::size_t      mPos{0};
};

} // namespace

Json const* Json::find(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (auto const& [name, value] : objectValue) {
        if (name == key) return &value;
    }
    return nullptr;
}

bool Json::getBool(std::string_view key, bool fallback) const {
    auto const* value = find(key);
    return value != nullptr && value->type == Type::Bool ? value->boolValue : fallback;
}

int Json::getInt(std::string_view key, int fallback) const {
    auto const* value = find(key);
    if (value == nullptr || value->type != Type::Number) return fallback;
    if (value->numberValue < std::numeric_limits<int>::min()
        || value->numberValue > std::numeric_limits<int>::max()) {
        return fallback;
    }
    return static_cast<int>(std::llround(value->numberValue));
}

double Json::getDouble(std::string_view key, double fallback) const {
    auto const* value = find(key);
    return value != nullptr && value->type == Type::Number ? value->numberValue : fallback;
}

std::string Json::getString(std::string_view key, std::string fallback) const {
    auto const* value = find(key);
    return value != nullptr && value->type == Type::String ? value->stringValue : std::move(fallback);
}

Result<Json> parseJson(std::string_view text) {
    try {
        Parser parser(text);
        return parser.parse();
    } catch (std::exception const& e) {
        return Error(std::string("JSON 解析失败: ") + e.what());
    }
}

} // namespace litematicabds::util
