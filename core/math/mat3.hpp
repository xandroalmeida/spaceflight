#pragma once

// Row-major 3x3 matrix, used for reference frame rotations obtained from SPICE
// (pxform_c).  We never hand-write a rotation that SPICE already provides.

#include "core/math/vec3.hpp"

#include <array>

namespace sf::math {

struct Mat3 {
    // m[row][col]
    std::array<std::array<double, 3>, 3> m{{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};

    static constexpr Mat3 identity() { return Mat3{}; }

    [[nodiscard]] constexpr double at(int r, int c) const { return m[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]; }

    [[nodiscard]] constexpr Vec3 operator*(const Vec3& v) const {
        return Vec3{at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z,
                    at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z,
                    at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z};
    }

    [[nodiscard]] constexpr Mat3 transposed() const {
        Mat3 t{};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                t.m[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = at(c, r);
            }
        }
        return t;
    }

    [[nodiscard]] constexpr Mat3 operator*(const Mat3& o) const {
        Mat3 p{};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                double s = 0.0;
                for (int k = 0; k < 3; ++k) {
                    s += at(r, k) * o.at(k, c);
                }
                p.m[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = s;
            }
        }
        return p;
    }
};

}  // namespace sf::math
