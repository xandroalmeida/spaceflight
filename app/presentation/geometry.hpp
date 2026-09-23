#pragma once

// The small amount of 3-D bookkeeping the presentation needs: a 2-D point for
// the instruments, and a basis/transform pair for placing parts of the ship and
// the cameras.
//
// Double precision throughout, and the conventions are the ones the scene was
// written in (it was written against Godot's, and they were kept rather than
// translated, because every comment about "+y of the mesh" and "the camera looks
// along -Z" in this directory depends on them):
//
//   * a Basis is three COLUMNS -- the local axes expressed in the parent frame;
//   * a camera looks along its local -Z, with +Y up;
//   * `rotated(axis, angle)` rotates in the PARENT frame (R * B),
//     `rotated_local(axis, angle)` in the object's own frame (B * R).

#include "core/math/vec3.hpp"

#include <cmath>

namespace sf::app {

using math::Vec3;

struct Vec2 {
    double x{0.0};
    double y{0.0};

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr bool operator==(const Vec2&) const = default;
    [[nodiscard]] double length() const { return std::sqrt(x * x + y * y); }
    [[nodiscard]] Vec2 normalized() const {
        const double l = length();
        return l > 0.0 ? Vec2{x / l, y / l} : Vec2{};
    }
    [[nodiscard]] double dot(const Vec2& o) const { return x * o.x + y * o.y; }
    [[nodiscard]] double distance_to(const Vec2& o) const { return (*this - o).length(); }
};

struct Rect2 {
    Vec2 position{};
    Vec2 size{};
};

struct Basis {
    Vec3 x{1.0, 0.0, 0.0};
    Vec3 y{0.0, 1.0, 0.0};
    Vec3 z{0.0, 0.0, 1.0};

    static constexpr Basis identity() { return {}; }

    // Rotation of `angle` radians about the unit `axis`.
    static Basis from_axis_angle(const Vec3& axis, double angle);
    // Euler angles in DEGREES, applied Y, then X, then Z (R = Ry * Rx * Rz) --
    // the order the scene's `rotation_degrees` were written in.
    static Basis from_euler_degrees(const Vec3& degrees);
    static constexpr Basis diagonal(const Vec3& s) {
        return {{s.x, 0.0, 0.0}, {0.0, s.y, 0.0}, {0.0, 0.0, s.z}};
    }

    [[nodiscard]] constexpr Vec3 operator*(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
    [[nodiscard]] constexpr Basis operator*(const Basis& o) const { return {*this * o.x, *this * o.y, *this * o.z}; }
    [[nodiscard]] constexpr Basis transposed() const {
        return {{x.x, y.x, z.x}, {x.y, y.y, z.y}, {x.z, y.z, z.z}};
    }
    [[nodiscard]] Basis rotated(const Vec3& axis, double angle) const {
        return from_axis_angle(axis.normalized(), angle) * *this;
    }
    [[nodiscard]] Basis rotated_local(const Vec3& axis, double angle) const {
        return *this * from_axis_angle(axis.normalized(), angle);
    }
    [[nodiscard]] Basis scaled_local(const Vec3& s) const { return *this * diagonal(s); }
    [[nodiscard]] Basis scaled(const Vec3& s) const { return diagonal(s) * *this; }
    [[nodiscard]] Basis orthonormalized() const;
    [[nodiscard]] Basis inverse() const;
    [[nodiscard]] double determinant() const { return dot(x, cross(y, z)); }
};

struct Transform3 {
    Basis basis{};
    Vec3 origin{};

    [[nodiscard]] Vec3 operator*(const Vec3& p) const { return basis * p + origin; }
    [[nodiscard]] Transform3 operator*(const Transform3& o) const {
        return {basis * o.basis, basis * o.origin + origin};
    }
    [[nodiscard]] Transform3 affine_inverse() const {
        const Basis inv = basis.inverse();
        return {inv, inv * (origin * -1.0)};
    }
};

// A basis whose -Z points from `eye` to `target`, with +Y as close to `up` as
// the geometry allows.
[[nodiscard]] Basis looking_at(const Vec3& direction, const Vec3& up);

[[nodiscard]] double wrap_angle(double angle);   // into [-pi, pi)

// Anything with float x, y, z (a RenderVec3), widened.
template <typename T>
[[nodiscard]] constexpr Vec3 widen(const T& v) {
    return Vec3{static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}
[[nodiscard]] constexpr Vec3 widen(const float* v) {
    return Vec3{static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2])};
}

}  // namespace sf::app
