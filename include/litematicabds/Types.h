#pragma once

// 基础值类型：坐标、包围盒、旋转/镜像、哈希。
// 不依赖任何平台头，LeviLamina 插件与 endstone core DLL 共用。

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace litematicabds {

struct Vec3i {
    int32_t x{0};
    int32_t y{0};
    int32_t z{0};

    constexpr Vec3i() = default;
    constexpr Vec3i(int32_t x_, int32_t y_, int32_t z_) : x(x_), y(y_), z(z_) {}

    friend constexpr bool operator==(Vec3i const& a, Vec3i const& b) noexcept {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
    friend constexpr bool operator!=(Vec3i const& a, Vec3i const& b) noexcept { return !(a == b); }

    friend constexpr Vec3i operator+(Vec3i const& a, Vec3i const& b) noexcept {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    friend constexpr Vec3i operator-(Vec3i const& a, Vec3i const& b) noexcept {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
};

struct Vec3d {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    constexpr Vec3d() = default;
    constexpr Vec3d(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
};

struct Vec3iHash {
    std::size_t operator()(Vec3i const& v) const noexcept {
        // 64 位混合（x/y/z 均在 ±2^21 内的世界坐标足够；越界也不会错，只是碰撞率上升）。
        std::uint64_t h = static_cast<std::uint64_t>(static_cast<std::uint32_t>(v.x)) * 0x9E3779B185EBCA87ull;
        h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(v.y)) * 0xC2B2AE3D27D4EB4Full;
        h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(v.z)) * 0x165667B19E3779F9ull;
        h ^= h >> 29;
        return static_cast<std::size_t>(h);
    }
};

// 闭区间包围盒（min/max 都包含）。空盒满足 max < min。
struct Bounds {
    Vec3i min{};
    Vec3i max{};

    [[nodiscard]] constexpr bool empty() const noexcept {
        return max.x < min.x || max.y < min.y || max.z < min.z;
    }
    [[nodiscard]] constexpr Vec3i size() const noexcept {
        return empty() ? Vec3i{} : Vec3i{max.x - min.x + 1, max.y - min.y + 1, max.z - min.z + 1};
    }
    [[nodiscard]] constexpr std::int64_t volume() const noexcept {
        if (empty()) return 0;
        auto const s = size();
        return static_cast<std::int64_t>(s.x) * s.y * s.z;
    }
    [[nodiscard]] constexpr bool contains(Vec3i p) const noexcept {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
};

enum class Rotation : int {
    None  = 0,
    CW90  = 90,
    CW180 = 180,
    CW270 = 270,
};

enum class Mirror : int {
    None = 0,
    X    = 1, // 沿 X 轴镜像（x -> size.x-1-x）
    Z    = 2, // 沿 Z 轴镜像（z -> size.z-1-z）
    XZ   = 3, // 两轴同时镜像
};

// 旋转/镜像后的尺寸（旋转 90/270 会交换 X/Z）。
[[nodiscard]] inline Vec3i rotatedSize(Vec3i size, Rotation rotation) noexcept {
    if (rotation == Rotation::CW90 || rotation == Rotation::CW270) {
        return {size.z, size.y, size.x};
    }
    return size;
}

// 把局部坐标（0..size-1）变换到投影内部坐标：先镜像，再旋转。
[[nodiscard]] inline Vec3i transformLocal(Vec3i local, Vec3i size, Rotation rotation, Mirror mirror) noexcept {
    int x = local.x;
    int z = local.z;
    if (mirror == Mirror::X || mirror == Mirror::XZ) x = size.x - 1 - x;
    if (mirror == Mirror::Z || mirror == Mirror::XZ) z = size.z - 1 - z;

    switch (rotation) {
    case Rotation::CW90:  return {size.z - 1 - z, local.y, x};
    case Rotation::CW180: return {size.x - 1 - x, local.y, size.z - 1 - z};
    case Rotation::CW270: return {z, local.y, size.x - 1 - x};
    case Rotation::None:
    default:              return {x, local.y, z};
    }
}

// 解析 "0/90/180/270" 或 "none/cw90/cw180/cw270"。
[[nodiscard]] bool parseRotation(std::string_view text, Rotation& out) noexcept;
[[nodiscard]] bool parseMirror(std::string_view text, Mirror& out) noexcept;
[[nodiscard]] char const* rotationName(Rotation rotation) noexcept;
[[nodiscard]] char const* mirrorName(Mirror mirror) noexcept;

} // namespace litematicabds
