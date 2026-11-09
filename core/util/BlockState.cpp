#include "litematicabds/BlockState.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <string>
#include <vector>

namespace litematicabds {
namespace {

std::string_view trimView(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

std::string trimCopy(std::string_view text) { return std::string(trimView(text)); }

bool hasAirSuffix(std::string_view name) noexcept {
    return name.size() >= 4 && name.substr(name.size() - 4) == "_air";
}

} // namespace

bool BlockState::isAir() const noexcept {
    if (name == "minecraft:air" || name == "minecraft:cave_air" || name == "minecraft:void_air") return true;
    return hasAirSuffix(name);
}

std::string BlockState::toString() const {
    if (properties.empty()) return name;
    return name + "[" + properties + "]";
}

std::optional<BlockState> parseBlockState(std::string_view text) {
    auto const trimmed = trimView(text);
    if (trimmed.empty()) return std::nullopt;

    BlockState state;
    auto const open = trimmed.find('[');
    if (open == std::string_view::npos) {
        state.name = std::string(trimmed);
        return state;
    }

    state.name = trimCopy(trimmed.substr(0, open));
    if (state.name.empty()) return std::nullopt;

    auto const close = trimmed.rfind(']');
    if (close == std::string_view::npos || close < open) return std::nullopt;
    auto const body = trimmed.substr(open + 1, close - open - 1);
    state.properties = canonicalProperties(body);
    return state;
}

std::string canonicalProperties(std::string_view properties) {
    std::map<std::string, std::string, std::less<>> values;
    std::size_t                              start = 0;
    while (start <= properties.size()) {
        auto const comma = properties.find(',', start);
        auto const end   = comma == std::string_view::npos ? properties.size() : comma;
        auto const part  = trimView(properties.substr(start, end - start));
        if (!part.empty()) {
            auto const eq = part.find('=');
            if (eq != std::string_view::npos && eq > 0) {
                auto const key = trimCopy(part.substr(0, eq));
                auto const val = trimCopy(part.substr(eq + 1));
                if (!key.empty()) values[key] = val;
            }
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }

    std::string out;
    for (auto const& [key, value] : values) {
        if (!out.empty()) out.push_back(',');
        out += key;
        out.push_back('=');
        out += value;
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> splitProperties(std::string_view properties) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t                                      start = 0;
    while (start <= properties.size()) {
        auto const comma = properties.find(',', start);
        auto const end   = comma == std::string_view::npos ? properties.size() : comma;
        auto const part  = trimView(properties.substr(start, end - start));
        if (!part.empty()) {
            auto const eq = part.find('=');
            if (eq != std::string_view::npos && eq > 0) {
                out.emplace_back(trimCopy(part.substr(0, eq)), trimCopy(part.substr(eq + 1)));
            }
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

std::string_view stripNamespace(std::string_view name) noexcept {
    constexpr std::string_view kPrefix = "minecraft:";
    if (name.size() > kPrefix.size() && name.substr(0, kPrefix.size()) == kPrefix) {
        return name.substr(kPrefix.size());
    }
    return name;
}

std::string withNamespace(std::string_view name) {
    if (name.find(':') != std::string_view::npos) return std::string(name);
    return "minecraft:" + std::string(name);
}

bool stateValueEquals(std::string_view a, std::string_view b) noexcept {
    if (a == b) return true;

    auto boolOf = [](std::string_view v) -> int {
        if (v == "true" || v == "1") return 1;
        if (v == "false" || v == "0") return 0;
        return -1;
    };
    bool const aIsWord = a == "true" || a == "false";
    bool const bIsWord = b == "true" || b == "false";
    if (aIsWord || bIsWord) {
        int const ba = boolOf(a);
        int const bb = boolOf(b);
        return ba >= 0 && bb >= 0 && ba == bb;
    }

    // 数字比较（"01" == "1"，"1.0" == "1"）。
    auto parseDouble = [](std::string_view v, double& out) -> bool {
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.remove_prefix(1);
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.remove_suffix(1);
        if (v.empty()) return false;
        char const* begin = v.data();
        char const* end   = v.data() + v.size();
        auto        res   = std::from_chars(begin, end, out);
        return res.ec == std::errc{} && res.ptr == end;
    };
    double da = 0;
    double db = 0;
    if (parseDouble(a, da) && parseDouble(b, db)) return da == db;

    return false;
}

} // namespace litematicabds
