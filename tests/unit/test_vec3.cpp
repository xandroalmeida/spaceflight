// Algebraic identities of the vector type.  No tolerance discussion is needed
// for the exact ones; where floating point makes exactness impossible, the
// tolerance is one machine epsilon scaled by the magnitude of the operands.

#include "core/math/vec3.hpp"
#include "core/units/constants.hpp"
#include "tests/support/test_harness.hpp"

#include <limits>

using sf::math::Vec3;
using sf::math::cross;
using sf::math::dot;

namespace {
constexpr double kEps = std::numeric_limits<double>::epsilon();
}

TEST(vec3_arithmetic_is_exact_for_representable_values) {
    const Vec3 a{1.0, 2.0, 3.0};
    const Vec3 b{0.5, 0.25, 0.125};

    CHECK_EQ(a + b, (Vec3{1.5, 2.25, 3.125}));
    CHECK_EQ(a - b, (Vec3{0.5, 1.75, 2.875}));
    CHECK_EQ(a * 2.0, (Vec3{2.0, 4.0, 6.0}));
    CHECK_EQ(2.0 * a, a * 2.0);
    CHECK_EQ(-a, (Vec3{-1.0, -2.0, -3.0}));
}

TEST(vec3_dot_and_cross_obey_their_identities) {
    const Vec3 a{1.0, -2.0, 3.5};
    const Vec3 b{-4.0, 0.5, 2.0};
    const Vec3 c{0.25, 7.0, -1.5};

    // a x b is orthogonal to both operands.  The residual is a sum of products
    // of the inputs, so the tolerance scales with |a||b|.
    const Vec3 axb = cross(a, b);
    const double scale = a.norm() * b.norm();
    CHECK_NEAR_ABS(dot(axb, a), 0.0, 8.0 * kEps * scale * a.norm(),
                   "orthogonality residual is bounded by the rounding of the products, "
                   "~eps*|a|^2*|b|; 8 ulp of margin");
    CHECK_NEAR_ABS(dot(axb, b), 0.0, 8.0 * kEps * scale * b.norm(),
                   "same bound with |b| in place of |a|");

    CHECK_EQ(cross(a, b), -cross(b, a));

    // Jacobi identity: a x (b x c) + b x (c x a) + c x (a x b) = 0
    const Vec3 jacobi = cross(a, cross(b, c)) + cross(b, cross(c, a)) + cross(c, cross(a, b));
    const double jacobi_scale = a.norm() * b.norm() * c.norm();
    CHECK_NEAR_ABS(jacobi.norm(), 0.0, 16.0 * kEps * jacobi_scale,
                   "each term is a triple product; residual bounded by ~eps*|a||b||c|, 16 ulp margin");

    // Lagrange: |a x b|^2 + (a.b)^2 = |a|^2 |b|^2
    const double lhs = axb.norm_squared() + dot(a, b) * dot(a, b);
    const double rhs = a.norm_squared() * b.norm_squared();
    CHECK_NEAR_REL(lhs, rhs, 16.0 * kEps,
                   "Lagrange identity; both sides are sums of 6 products, 16 ulp of margin");
}

TEST(vec3_norm_and_normalization) {
    const Vec3 v{3.0, 4.0, 12.0};
    CHECK_NEAR_ABS(v.norm(), 13.0, 0.0,
                   "3-4-12-13 is an exact Pythagorean quadruple: sqrt of an exactly "
                   "representable integer, no rounding possible");

    const Vec3 u = v.normalized();
    CHECK_NEAR_REL(u.norm(), 1.0, 4.0 * kEps,
                   "one division and one sqrt, each correctly rounded: 4 ulp");

    // Degenerate input must not produce NaN: it returns zero, and the caller is
    // expected to have checked.  Silent NaN propagation is the failure mode we
    // are guarding against.
    CHECK_EQ(Vec3{}.normalized(), Vec3{});
    CHECK(Vec3{}.normalized().is_finite());
}

TEST(vec3_angle_between_is_stable_near_zero_and_pi) {
    const Vec3 a{1.0, 0.0, 0.0};

    // A cosine-only formula loses half its digits here; the atan2 form does not.
    const Vec3 nearly_parallel{1.0, 1.0e-8, 0.0};
    CHECK_NEAR_REL(angle_between(a, nearly_parallel), 1.0e-8, 1.0e-12,
                   "exact angle is atan(1e-8) = 1e-8 - 1e-24/3; the atan2 formulation keeps "
                   "full relative precision, so 1e-12 relative is a loose bound");

    const Vec3 nearly_opposite{-1.0, 1.0e-8, 0.0};
    CHECK_NEAR_REL(angle_between(a, nearly_opposite), sf::units::pi - 1.0e-8, 1.0e-12,
                   "same argument mirrored about pi");

    CHECK_NEAR_ABS(angle_between(a, Vec3{0.0, 1.0, 0.0}), sf::units::pi / 2.0, 4.0 * kEps,
                   "orthogonal unit vectors: atan2(1,0) is exact up to the representation of pi/2");
}

TEST(vec3_indexing_matches_members) {
    Vec3 v{1.0, 2.0, 3.0};
    CHECK_EQ(v[0], v.x);
    CHECK_EQ(v[1], v.y);
    CHECK_EQ(v[2], v.z);
    v[1] = 42.0;
    CHECK_EQ(v.y, 42.0);
}
