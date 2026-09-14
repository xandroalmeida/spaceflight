#pragma once

// Leaving a body onto a GIVEN hyperbolic excess velocity (Milestone 8 rules
// 33 and 34).
//
// ---------------------------------------------------------------------------
// Why this is not just a speed
//
// The patched-conic recipe everyone quotes is
//
//     v_p = sqrt(v_inf^2 + v_escape^2),    dv = v_p - v_parking
//
// and it is correct about the SPEED and silent about the DIRECTION -- because it
// is written for a departure from periapsis of a hyperbola that is already
// oriented the right way.  A planner searching a parking orbit does not get that
// for free: it is standing at some arbitrary point of the orbit and has to work
// out which way to point, and the answer is not "prograde".
//
// Aiming the burn prograde and trusting the magnitude is the mistake that makes
// an interplanetary plan miss by an entire planetary radius of angle.  The
// outgoing asymptote of a hyperbola is not along the velocity at the departure
// point; it is rotated away from it by the turn the body's own gravity still has
// to apply, and how much depends on the eccentricity, which depends on where on
// the orbit the burn happens.
//
// ---------------------------------------------------------------------------
// What is solved here
//
// Given a position r relative to the body, a desired outgoing asymptote velocity
// v_inf (relative to the same body) and the body's GM, there is exactly a
// one-parameter family of hyperbolas through r whose outgoing asymptote is along
// v_inf -- one per eccentricity -- and exactly ONE of them passes through r at
// the right distance.  Two facts pin it:
//
//     energy      v^2 = v_inf^2 + 2 mu / r            (fixes the speed)
//     geometry    nu_inf - nu = theta                 (fixes the direction)
//
// where theta is the angle between r and v_inf, and nu_inf = arccos(-1/e) is the
// true anomaly of the asymptote.  With a = -mu / v_inf^2 known from the energy,
//
//     r = a (1 - e^2) / (1 + e cos(nu_inf(e) - theta))
//
// is one equation in e alone.  It is solved numerically, because it is not
// invertible in closed form and pretending otherwise would mean approximating
// where an exact answer is one bisection away.
//
// The result is a TWO-BODY answer and it is used as a two-body answer: an
// initial guess handed to the differential corrector, which then inverts the
// full model.  Rule 36 -- patched conics generate candidates; they do not fly
// them.
//
// See docs/architecture/general-mission-planning.md section 5.

#include "core/math/vec3.hpp"

#include <string>

namespace sf::trajectory {

struct DepartureHyperbola {
    // The velocity to be ON at `position`, relative to the body.  This is a
    // state, not a burn: what it costs is |this - whatever the ship has|, and
    // that subtraction belongs to whoever knows the parking orbit.
    math::Vec3 velocity{};

    double speed{0.0};             // [m/s], = |velocity|
    double eccentricity{0.0};      // > 1
    double semi_major_axis{0.0};   // [m], negative
    double periapsis_radius{0.0};  // [m]
    double true_anomaly{0.0};      // [rad] of `position`; negative before periapsis
    double flight_path_angle{0.0}; // [rad] between velocity and the local horizontal

    bool ok{false};
    std::string message;
};

// Never throws for a geometry it cannot serve: an unusable departure point is a
// RESULT with a reason on it, because the search walks hundreds of them and a
// throw per bad one would make the refusal cost more than the answer.
//
// It refuses when:
//   * `v_infinity` is zero -- that is an escape onto a parabola, not a transfer;
//   * `position` is parallel or antiparallel to `v_infinity` -- the orbital plane
//     is then undefined, which is a real statement about that departure point and
//     not a numerical wobble;
//   * the iteration does not bracket a solution.
[[nodiscard]] DepartureHyperbola departure_onto_asymptote(const math::Vec3& position,
                                                          const math::Vec3& v_infinity,
                                                          double gm);

// The speed alone, which is all rule 34's formula gives: sqrt(v_inf^2 + 2 mu/r).
// Exposed because it is the right thing for a SCREEN -- it bounds the cost of a
// departure without solving for its direction -- and wrong as a burn.
[[nodiscard]] double speed_for_asymptote(double radius, double v_infinity, double gm);

}  // namespace sf::trajectory
