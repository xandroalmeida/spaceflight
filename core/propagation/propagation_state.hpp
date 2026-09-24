#pragma once

// The quantities the propagator integrates.
//
// `proper_time` is here from day one even though Milestone 0 is Newtonian and it
// simply tracks coordinate time.  Adding it later would mean changing every
// signature in the integrator; see docs/physics/relativity-roadmap.md section 8.
//
// `state.velocity` is ALWAYS the coordinate velocity v, whatever the kinematics.
// The relativistic modes integrate u = gamma*v, but only inside the propagator:
// state_from_array converts before any force model sees the state, and the
// state handed back is v again (dense_output.cpp). So nothing outside the
// integrator converts -- and dividing by gamma "to get v" divides twice.

#include "core/attitude/attitude_state.hpp"
#include "core/coordinates/state_vector.hpp"
#include "core/time/duration.hpp"

namespace sf::propagation {

struct PropagationState {
    coordinates::StateVector state{};       // position [m], velocity [m/s]
    double mass{1.0};                       // rest mass [kg]
    time::Duration proper_time{};           // elapsed proper time since t0 [s]

    // Orientation and angular velocity (Milestone 3). Integrated alongside the
    // translation because the RCS couples them: a thruster that produces torque
    // also produces thrust (docs/physics/attitude.md section 6).
    attitude::AttitudeState attitude{};

    [[nodiscard]] bool is_finite() const {
        return state.is_finite() && std::isfinite(mass) && proper_time.is_finite() &&
               attitude.is_finite();
    }
};

}  // namespace sf::propagation
