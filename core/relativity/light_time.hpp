#pragma once

// Where a body APPEARS, which is where it was when the light left.
//
// Solves  |x_obs(t) - x_body(t_r)| = c (t - t_r)  for the retarded epoch t_r.
// Fixed-point iteration converges at the rate v_body/c ~ 1e-4 per step, so three
// or four passes reach machine precision.
//
// Deliberately a SEPARATE call from EphemerisProvider::state(): geometric and
// apparent are different questions, and the dynamics wants the first
// (ADR-0003). Mixing them is the kind of error that hides until someone measures.

#include "core/ephemeris/ephemeris_provider.hpp"

namespace sf::relativity {

struct ApparentPosition {
    // Body position at the retarded epoch, MINUS the observer position at t.
    // This is the vector to look along.
    math::Vec3 relative_position{};
    math::Vec3 relative_velocity{};   // body velocity at t_r, minus nothing
    math::Vec3 body_position{};       // absolute, at t_r
    double light_time{0.0};           // [s]
    time::CoordinateTime retarded_epoch{};
    int iterations{0};
    bool converged{false};
};

// `frame` must have a FIXED origin over the light time -- in practice SSB/J2000.
// A body-centred frame would compare the target at t_r against an origin that
// also moved, which is a different and wrong vector.
ApparentPosition apparent_position(const ephemeris::EphemerisProvider& provider,
                                   celestial::BodyId target,
                                   const math::Vec3& observer_position,
                                   time::CoordinateTime t,
                                   coordinates::ReferenceFrame frame =
                                       coordinates::ReferenceFrame::ssb_j2000(),
                                   double tolerance_seconds = 1.0e-9,
                                   int max_iterations = 12);

// Direction to look, in the SHIP's frame: light time then aberration, in that
// order, which is the order the photons experience them.
math::Vec3 apparent_direction(const ephemeris::EphemerisProvider& provider,
                              celestial::BodyId target,
                              const math::Vec3& observer_position,
                              const math::Vec3& observer_velocity,
                              time::CoordinateTime t,
                              coordinates::ReferenceFrame frame =
                                  coordinates::ReferenceFrame::ssb_j2000());

}  // namespace sf::relativity
