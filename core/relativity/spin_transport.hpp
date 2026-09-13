#pragma once

// How an orientation changes when nothing rotated it.
//
// A frame carried without torque along a curved worldline still turns, because
// the composition of two non-collinear boosts is a boost AND a rotation. In flat
// spacetime that is Thomas precession; in free fall in a weak field it is the
// geodetic (de Sitter) precession that Gravity Probe B measured.
//
// Derivations, the closed forms and the error budget:
// docs/physics/spin-transport.md.

#include "core/math/vec3.hpp"
#include "core/relativity/kinematics.hpp"
#include "core/units/constants.hpp"

namespace sf::relativity {

// Thomas precession, exact in flat spacetime, as an angular velocity in the
// inertial frame per unit COORDINATE time.
//
//                   du/dt  x  u
//     omega_T  =  ----------------
//                   (gamma + 1) c^2
//
// This is the textbook (gamma^2/(gamma+1))(a x v)/c^2 rewritten in the variables
// the propagator actually carries. The rewrite is not cosmetic: every gamma
// cancels, there is no subtraction anywhere, and gamma + 1 >= 2 always, so the
// expression holds its digits from beta = 1e-15 to beta = 1 - 1e-15
// (spin-transport.md section 2.1).
//
// Collinear thrust gives exactly zero -- the cross product vanishes -- rather
// than something small. A ship accelerating in a straight line does not
// precess, and that is a property of the form, not a special case.
[[nodiscard]] inline math::Vec3 thomas_precession(const math::Vec3& proper_velocity,
                                                  const math::Vec3& proper_velocity_rate) {
    const double gamma = lorentz_factor(proper_velocity);
    return cross(proper_velocity_rate, proper_velocity) /
           ((gamma + 1.0) * units::c_squared);
}

// Geodetic (de Sitter) precession to first post-Newtonian order:
//
//     omega_G = (3/2) (v x grad U) / c^2
//
// with U > 0 the summed Newtonian potential, so grad U IS the Newtonian
// acceleration that PointMassGravity already computes. The 3/2 is 1 from the
// spatial curvature of the metric plus 1/2 from the same Thomas effect above,
// driven by gravity instead of thrust. Unlike Thomas it is prograde.
//
// First order in v/c: this is the 1PN term, not an exact expression. At
// beta ~ 1 close to a massive body it is only indicative, and
// spin-transport.md section 5 says so.
[[nodiscard]] inline math::Vec3 geodetic_precession(const math::Vec3& coordinate_velocity_in,
                                                    const math::Vec3& potential_gradient) {
    return cross(coordinate_velocity_in, potential_gradient) * (1.5 / units::c_squared);
}

}  // namespace sf::relativity
