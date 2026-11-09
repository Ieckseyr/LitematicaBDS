#include "litematicabds/Types.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace litematicabds {
namespace {

std::string lowerTrim(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front()))) out.erase(out.begin());
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
    return out;
}

} // namespace

bool parseRotation(std::string_view text, Rotation& out) noexcept {
    auto const t = lowerTrim(text);
    if (t.empty() || t == "0" || t == "none" || t == "north") {
        out = Rotation::None;
        return true;
    }
    if (t == "90" || t == "cw90" || t == "clockwise90") {
        out = Rotation::CW90;
        return true;
    }
    if (t == "180" || t == "cw180" || t == "clockwise180") {
        out = Rotation::CW180;
        return true;
    }
    if (t == "270" || t == "cw270" || t == "clockwise270") {
        out = Rotation::CW270;
        return true;
    }
    return false;
}

bool parseMirror(std::string_view text, Mirror& out) noexcept {
    auto const t = lowerTrim(text);
    if (t.empty() || t == "none" || t == "0" || t == "off") {
        out = Mirror::None;
        return true;
    }
    if (t == "x" || t == "mirror_x" || t == "1") {
        out = Mirror::X;
        return true;
    }
    if (t == "z" || t == "mirror_z" || t == "2") {
        out = Mirror::Z;
        return true;
    }
    if (t == "xz" || t == "both" || t == "3") {
        out = Mirror::XZ;
        return true;
    }
    return false;
}

char const* rotationName(Rotation rotation) noexcept {
    switch (rotation) {
    case Rotation::CW90:  return "cw90";
    case Rotation::CW180: return "cw180";
    case Rotation::CW270: return "cw270";
    case Rotation::None:
    default:              return "none";
    }
}

char const* mirrorName(Mirror mirror) noexcept {
    switch (mirror) {
    case Mirror::X: return "x";
    case Mirror::Z: return "z";
    case Mirror::XZ: return "xz";
    case Mirror::None:
    default:        return "none";
    }
}

} // namespace litematicabds
