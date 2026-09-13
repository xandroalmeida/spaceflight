#pragma once

// The quantities the propagator integrates.
//
// `proper_time` is here from day one even though Milestone 0 is Newtonian and it
// simply tracks coordinate time.  Adding it later would mean changing every
// signature in the integrator; see docs/physics/relativity-roadmap.md section 8.
//
// Milestone 4 replaces `state.velocity` (coordinate velocity v) with u = gamma*v.
// That is a change of this struct plus one derivative function -- not a rewrite.

#include "core/coordinates/state_vector.hpp"
#include "core/time/duration.hpp"

namespace sf::propagation {

struct PropagationState {
    coordinates::StateVector state{};       // position [m], velocity [m/s]
    double mass{1.0};                       // rest mass [kg]
    time::Duration proper_time{};           // elapsed proper time since t0 [s]

    [[nodiscard]] bool is_finite() const {
        return state.is_finite() && std::isfinite(mass) && proper_time.is_finite();
    }
};

}  // namespace sf::propagation
