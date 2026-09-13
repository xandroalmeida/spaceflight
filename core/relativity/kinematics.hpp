#pragma once

// Special-relativistic kinematics, written so that nothing cancels.
//
// The state variable is u = gamma*v, the proper velocity. Every quantity below
// is derived from it, and every derivation is chosen for the form that keeps its
// digits -- at both ends of the scale, because this project has been bitten at
// both (docs/physics/relativistic-propulsion.md section 7).

#include "core/math/vec3.hpp"
#include "core/units/constants.hpp"

#include <cmath>

namespace sf::relativity {

// gamma = sqrt(1 + |u|^2/c^2). A sum of positive terms: no cancellation at any
// speed, unlike 1/sqrt(1 - beta^2), which loses twelve digits at beta = 0.999999.
[[nodiscard]] inline double lorentz_factor(const math::Vec3& proper_velocity) {
    const double ratio = proper_velocity.norm() / units::c;
    return std::sqrt(1.0 + ratio * ratio);
}

// gamma - 1 = (|u|/c)^2 / (gamma + 1). Never forms the difference, so it stays
// exact as beta -> 0, where subtracting 1 from gamma returns literally zero.
[[nodiscard]] inline double lorentz_factor_minus_one(const math::Vec3& proper_velocity) {
    const double ratio = proper_velocity.norm() / units::c;
    return ratio * ratio / (lorentz_factor(proper_velocity) + 1.0);
}

// v = u/gamma. For ANY finite u this has |v| < c identically -- which is why the
// integrator carries u and why no clamp exists anywhere (rule 13).
[[nodiscard]] inline math::Vec3 coordinate_velocity(const math::Vec3& proper_velocity) {
    return proper_velocity / lorentz_factor(proper_velocity);
}

[[nodiscard]] inline double beta(const math::Vec3& proper_velocity) {
    return coordinate_velocity(proper_velocity).norm() / units::c;
}

// u = gamma*v. This direction of the conversion DOES cancel as |v| -> c: it is
// the price of specifying a state by its coordinate velocity, and the reason
// relativistic scenarios should be written in rapidity instead
// (docs/physics/relativistic-propulsion.md section 7). Returns u = 0 for |v| >= c
// rather than an infinity; callers that care must check first.
[[nodiscard]] inline math::Vec3 proper_velocity(const math::Vec3& coordinate_velocity_in) {
    const double b = coordinate_velocity_in.norm() / units::c;
    if (!(b < 1.0)) {
        return math::Vec3{};
    }
    return coordinate_velocity_in / std::sqrt(1.0 - b * b);
}

// Rapidity phi = artanh(beta). Additive under collinear boosts, which is what
// makes the relativistic rocket equation independent of how the burn was spread
// out in time.
[[nodiscard]] inline double rapidity_from_beta(double b) { return std::atanh(b); }
[[nodiscard]] inline double beta_from_rapidity(double phi) { return std::tanh(phi); }

// u = c sinh(phi) along `direction`. The numerically sane way to state a
// relativistic initial condition.
[[nodiscard]] inline math::Vec3 proper_velocity_from_rapidity(const math::Vec3& direction,
                                                              double rapidity) {
    return direction.normalized() * (units::c * std::sinh(rapidity));
}

[[nodiscard]] inline double rapidity(const math::Vec3& proper_velocity_in) {
    return std::asinh(proper_velocity_in.norm() / units::c);
}

}  // namespace sf::relativity
