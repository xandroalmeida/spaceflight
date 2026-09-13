#pragma once

// Minimal 3-vector in SI units.  Deliberately a plain aggregate: it is the
// hottest data structure in the integrator and must stay trivially copyable.

#include <cmath>
#include <ostream>

namespace sf::math {

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    static constexpr Vec3 zero() { return Vec3{}; }
    static constexpr Vec3 unit_x() { return Vec3{1.0, 0.0, 0.0}; }
    static constexpr Vec3 unit_y() { return Vec3{0.0, 1.0, 0.0}; }
    static constexpr Vec3 unit_z() { return Vec3{0.0, 0.0, 1.0}; }

    constexpr double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    constexpr Vec3& operator/=(double s) { x /= s; y /= s; z /= s; return *this; }

    [[nodiscard]] constexpr double norm_squared() const { return x * x + y * y + z * z; }
    [[nodiscard]] double norm() const { return std::sqrt(norm_squared()); }

    // Throws nothing; returns the zero vector for a zero-length input.  Callers
    // that care about the degenerate case must check norm() themselves.
    [[nodiscard]] Vec3 normalized() const {
        const double n = norm();
        return n > 0.0 ? Vec3{x / n, y / n, z / n} : Vec3{};
    }

    [[nodiscard]] bool is_finite() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

constexpr Vec3 operator+(Vec3 a, const Vec3& b) { return a += b; }
constexpr Vec3 operator-(Vec3 a, const Vec3& b) { return a -= b; }
constexpr Vec3 operator-(const Vec3& a) { return Vec3{-a.x, -a.y, -a.z}; }
constexpr Vec3 operator*(Vec3 a, double s) { return a *= s; }
constexpr Vec3 operator*(double s, Vec3 a) { return a *= s; }
constexpr Vec3 operator/(Vec3 a, double s) { return a /= s; }

constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

inline double distance(const Vec3& a, const Vec3& b) { return (a - b).norm(); }

// Angle between two vectors, numerically stable near 0 and pi (atan2 form).
inline double angle_between(const Vec3& a, const Vec3& b) {
    return std::atan2(cross(a, b).norm(), dot(a, b));
}

std::ostream& operator<<(std::ostream& os, const Vec3& v);

}  // namespace sf::math
