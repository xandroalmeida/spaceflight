#pragma once

// Row-major 3x3 matrix, used for reference frame rotations obtained from SPICE
// (pxform_c).  We never hand-write a rotation that SPICE already provides.

#include "core/math/vec3.hpp"

#include <array>
#include <cmath>

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
    [[nodiscard]] constexpr double determinant() const {
        return at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
               at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
               at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
    }

    // Solves M x = b by Cramer's rule.  Three unknowns is small enough that the
    // determinant form is both the clearest and the fastest; `ok` reports a
    // singular (or near-singular) matrix rather than returning infinities, which
    // is what a differential corrector needs in order to give up honestly.
    struct Solution {
        Vec3 x{};
        bool ok{false};
    };

    [[nodiscard]] Solution solve(const Vec3& b) const {
        const double det = determinant();
        if (!std::isfinite(det) || det == 0.0) {
            return {};
        }

        Mat3 mx = *this;
        Mat3 my = *this;
        Mat3 mz = *this;
        for (int row = 0; row < 3; ++row) {
            mx.m[static_cast<std::size_t>(row)][0] = b[row];
            my.m[static_cast<std::size_t>(row)][1] = b[row];
            mz.m[static_cast<std::size_t>(row)][2] = b[row];
        }

        Solution out{};
        out.x = Vec3{mx.determinant() / det, my.determinant() / det, mz.determinant() / det};
        out.ok = out.x.is_finite();
        return out;
    }

    // Builds a matrix from three column vectors -- the natural shape of a
    // numerically estimated Jacobian.
    static constexpr Mat3 from_columns(const Vec3& c0, const Vec3& c1, const Vec3& c2) {
        Mat3 out{};
        out.m = {{{c0.x, c1.x, c2.x}, {c0.y, c1.y, c2.y}, {c0.z, c1.z, c2.z}}};
        return out;
    }
};

}  // namespace sf::math
