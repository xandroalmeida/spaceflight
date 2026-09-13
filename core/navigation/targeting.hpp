#pragma once

// Differential targeting: correct a departure velocity until the FULL model
// arrives where the two-body plan said it would.
//
// Lambert answers a two-body question.  Propagated through N bodies and J2, its
// answer misses -- for an Earth-Moon transfer, by hundreds of thousands of
// kilometres, because a trans-lunar injection is extremely sensitive to the
// departure velocity.  See docs/physics/lambert.md section 5.
//
// The corrector treats the trajectory as a black box: give it a function from
// departure velocity to arrival position, and it inverts that function by Newton
// iteration with a numerically estimated Jacobian.  It does not know or care
// whether the arrival came from an impulsive burn, a finite burn, or a mission
// with ten maneuvers in it.

#include "core/math/vec3.hpp"

#include <functional>
#include <string>

namespace sf::navigation {

struct TargetingConfig {
    double position_tolerance{1.0e4};   // [m] -- when to stop
    // Finite difference step for the Jacobian.  Small, deliberately: a trans-lunar
    // arrival moves by ~1e6 m per m/s of departure velocity, so a 0.1 m/s probe
    // already leaves the linear regime and the resulting Jacobian is a chord
    // across a curve rather than a tangent.  Measured on the lunar intercept:
    // 0.5 m/s stalls at 36 km, 0.1 at 155 km, 0.001 converges to 3.5 km.
    // Numerical noise is not the limit -- the propagator's own error is ~1e-3 m
    // against a signal of ~1e3 m at this step size.
    double velocity_step{1.0e-3};       // [m/s]
    int max_iterations{25};
    double max_correction{5.0e3};       // [m/s] -- per-iteration step limit
    // A full Newton step routinely overshoots here: a trans-lunar arrival is
    // wildly nonlinear in the departure velocity, not least because the Moon's
    // own gravity is part of the map.  The corrector backtracks (1, 1/2, 1/4 ...)
    // until the miss actually decreases.
    int max_backtracks{8};
};

struct TargetingResult {
    math::Vec3 departure_velocity{};    // corrected
    math::Vec3 initial_guess{};
    double miss_distance{0.0};          // [m] achieved
    double initial_miss{0.0};           // [m] before correction
    int iterations{0};
    int evaluations{0};
    bool converged{false};
    std::string message;
};

// `arrival` maps a departure velocity to the arrival position, in whatever frame
// `target` is expressed in.  It must be deterministic: the Jacobian is estimated
// by differencing it.
TargetingResult correct_departure(const std::function<math::Vec3(const math::Vec3&)>& arrival,
                                  const math::Vec3& initial_guess,
                                  const math::Vec3& target,
                                  TargetingConfig config = {});

}  // namespace sf::navigation
