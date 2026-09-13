// Quaternion algebra and the conventions of ADR-0008.
//
// Most of the value here is in pinning the CONVENTIONS: scalar first, Hamilton,
// body -> inertial, omega multiplying from the right. Every one of those has a
// popular opposite, and mixing them produces rotations that look almost correct.

#include "core/math/quaternion.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <limits>
#include <sstream>

using sf::math::EulerZYX;
using sf::math::Mat3;
using sf::math::Quaternion;
using sf::math::Vec3;

namespace {
constexpr double kEps = std::numeric_limits<double>::epsilon();
}

TEST(the_algebra_is_hamilton_not_jpl) {
    // i*j = k is the whole difference between the two conventions, and choosing
    // the wrong one transposes every composed rotation.
    const Quaternion i{0.0, 1.0, 0.0, 0.0};
    const Quaternion j{0.0, 0.0, 1.0, 0.0};
    const Quaternion k{0.0, 0.0, 0.0, 1.0};

    const Quaternion ij = i * j;
    CHECK_NEAR_ABS(ij.w() - k.w(), 0.0, 0.0, "ij = k exactly: all terms are 0 or +/-1");
    CHECK_NEAR_ABS(ij.z() - k.z(), 0.0, 0.0, "same");
    CHECK_NEAR_ABS((i * i).w(), -1.0, 0.0, "i^2 = -1");
    CHECK_NEAR_ABS((j * k).x(), 1.0, 0.0, "jk = i");
    CHECK_NEAR_ABS((k * i).y(), 1.0, 0.0, "ki = j");

    // Non-commutative, and by exactly the sign of the cross product.
    CHECK_NEAR_ABS((j * i).z(), -1.0, 0.0, "ji = -k");
}

TEST(rotations_compose_and_invert) {
    const auto a = Quaternion::from_axis_angle(Vec3::unit_z(), sf::units::deg_to_rad(90.0));
    const auto b = Quaternion::from_axis_angle(Vec3::unit_x(), sf::units::deg_to_rad(90.0));

    const Vec3 v{1.0, 0.0, 0.0};
    // 90 degrees about z takes +x to +y.
    const Vec3 rotated = a.rotate(v);
    CHECK_NEAR_ABS((rotated - Vec3{0.0, 1.0, 0.0}).norm(), 0.0, 4.0 * kEps,
                   "sin and cos of pi/2 are exact to a couple of ulp; the rotation is three "
                   "products of those");

    // (a*b) applied at once equals a applied after b.
    const Vec3 together = (a * b).rotate(v);
    const Vec3 stepwise = a.rotate(b.rotate(v));
    CHECK_NEAR_ABS((together - stepwise).norm(), 0.0, 8.0 * kEps,
                   "associativity of the rotation action; the two paths differ only by the order "
                   "of a handful of multiplications");

    CHECK_NEAR_ABS((a.rotate_inverse(a.rotate(v)) - v).norm(), 0.0, 8.0 * kEps,
                   "q* undoes q exactly for a unit quaternion");
    CHECK_NEAR_ABS(((a * a.inverse()).vector()).norm(), 0.0, 8.0 * kEps, "q q^-1 = identity");
}

TEST(rotation_matrices_and_quaternions_are_the_same_rotation) {
    for (const double degrees : {1.0, 37.0, 90.0, 179.0, 181.0, 300.0}) {
        const Vec3 axis = Vec3{0.3, -0.7, 0.65}.normalized();
        const auto q = Quaternion::from_axis_angle(axis, sf::units::deg_to_rad(degrees));
        const Mat3 m = q.to_rotation_matrix();

        const Vec3 v{2.0, -3.0, 0.5};
        CHECK_NEAR_ABS((q.rotate(v) - m * v).norm(), 0.0, 1.0e-14,
                       "the matrix form and the quaternion form are algebraically identical; only "
                       "the order of the products differs");

        // Round trip, including through 180 degrees where the naive branch of
        // the matrix-to-quaternion conversion cancels catastrophically.
        const auto back = Quaternion::from_rotation_matrix(m);
        CHECK_NEAR_ABS(Quaternion::angle_between(q, back), 0.0, 1.0e-12,
                       "Shepperd's branch selection keeps the conversion well conditioned at every "
                       "angle, including 180 degrees, where the w-branch would divide by ~0");
    }
}

TEST(q_and_minus_q_are_the_same_orientation) {
    const auto q = Quaternion::from_axis_angle(Vec3::unit_y(), 1.2);
    const Quaternion negated = -q;

    const Vec3 v{1.0, 2.0, 3.0};
    CHECK_NEAR_ABS((q.rotate(v) - negated.rotate(v)).norm(), 0.0, 1.0e-14,
                   "the rotation is quadratic in q, so the sign cancels identically");
    CHECK_NEAR_ABS(Quaternion::angle_between(q, negated), 0.0, 1.0e-12,
                   "angle_between uses |dot| precisely so that this is zero and not 2*pi");

    CHECK(Quaternion::dot(Quaternion::nearest_to(negated, q), q) > 0.0);
}

TEST(quaternion_kinematics_match_a_finite_rotation) {
    // The convention test that matters: qdot = 1/2 q (x) (0, omega_body), with
    // omega on the RIGHT. Integrating it for a short time must reproduce the
    // finite rotation about the body axis -- and if the order were swapped, the
    // ship would turn the right way about the WRONG axis.
    const auto q0 = Quaternion::from_axis_angle(Vec3{1.0, 1.0, 0.0}, 0.7);
    const Vec3 omega_body{0.0, 0.0, 0.3};  // spin about the body z axis
    const double dt = 1.0e-4;

    // One Euler step of the kinematic equation...
    const Quaternion stepped = (q0 + sf::math::attitude_derivative(q0, omega_body) * dt).normalized();
    // ...against the exact finite rotation about the BODY axis, applied on the right.
    const Quaternion exact = q0 * Quaternion::from_axis_angle(Vec3::unit_z(), 0.3 * dt);

    const double error = Quaternion::angle_between(stepped, exact);
    std::ostringstream os;
    os << "after dt = " << dt << " the Euler step differs from the exact rotation by " << error
       << " rad";
    INFO(os.str());

    CHECK_NEAR_ABS(error, 0.0, 1.0e-9,
                   "a first order step has error O(dt^2 * omega^2) = (1e-4 * 0.3)^2 ~ 1e-9 rad. "
                   "Getting the multiplication order wrong would instead give an error of order "
                   "omega*dt = 3e-5 -- four orders larger, and in the wrong direction");

    // The body axis the ship spins about must be unchanged by its own spin.
    const Vec3 spin_axis_inertial = q0.rotate(Vec3::unit_z());
    CHECK_NEAR_ABS((exact.rotate(Vec3::unit_z()) - spin_axis_inertial).norm(), 0.0, 1.0e-12,
                   "spinning about the body z axis leaves that axis fixed in inertial space");
}

TEST(from_two_vectors_handles_the_antiparallel_case) {
    const Vec3 a{1.0, 0.0, 0.0};

    for (const Vec3 b : {Vec3{0.0, 1.0, 0.0}, Vec3{0.0, 0.0, 1.0}, Vec3{-1.0, 0.0, 0.0},
                         Vec3{0.5, -0.5, 0.707}, Vec3{1.0, 0.0, 0.0}}) {
        const auto q = Quaternion::from_two_vectors(a, b);
        const Vec3 mapped = q.rotate(a);
        CHECK_NEAR_ABS((mapped - b.normalized()).norm(), 0.0, 1.0e-12,
                       "the constructed rotation must actually take a onto b, including the "
                       "antiparallel case where the axis is undefined and any perpendicular one "
                       "is a valid answer");
    }
}

TEST(euler_angles_are_a_display_projection_with_a_known_failure) {
    const EulerZYX angles{sf::units::deg_to_rad(30.0), sf::units::deg_to_rad(20.0),
                          sf::units::deg_to_rad(-45.0)};
    const auto q = Quaternion::from_euler_zyx(angles);
    const EulerZYX back = q.to_euler_zyx();

    CHECK_NEAR_ABS(back.yaw, angles.yaw, 1.0e-12, "round trip away from the poles is exact");
    CHECK_NEAR_ABS(back.pitch, angles.pitch, 1.0e-12, "same");
    CHECK_NEAR_ABS(back.roll, angles.roll, 1.0e-12, "same");

    // At pitch = 90 degrees yaw and roll stop being separable. The function
    // returns a consistent answer rather than a NaN, but the split is arbitrary
    // -- which is exactly why the STATE is never Euler angles (rule 19).
    const auto locked = Quaternion::from_euler_zyx(
        EulerZYX{sf::units::deg_to_rad(40.0), sf::units::deg_to_rad(90.0), sf::units::deg_to_rad(10.0)});
    const EulerZYX degenerate = locked.to_euler_zyx();
    CHECK_NEAR_ABS(std::abs(degenerate.pitch), sf::units::pi / 2.0, 1.0e-7,
                   "pitch itself is still recovered; asin near its argument's limit loses half the "
                   "digits, hence 1e-7 rather than 1e-12");
    CHECK_EQ(degenerate.roll, 0.0);
    CHECK(std::isfinite(degenerate.yaw));
}
