#pragma once

// The spacecraft as the simulation knows it, independent of any graphical model.
//
// Milestone 0 holds the minimum: identity, dynamical state and the frame it is
// expressed in.  Tank, engine, attitude and instrumentation join it in later
// milestones (rule section 20), each behind its own document.

#include "core/coordinates/reference_frame.hpp"
#include "core/propagation/propagation_state.hpp"
#include "core/time/coordinate_time.hpp"

#include <string>

namespace sf::spacecraft {

struct SpacecraftState {
    std::string name{"spacecraft"};
    time::CoordinateTime epoch{};
    coordinates::ReferenceFrame frame{coordinates::ReferenceFrame::ssb_j2000()};
    propagation::PropagationState dynamics{};

    // Milestone 1: PropellantTank tank; MainEngine engine;
    // Milestone 3: AttitudeState attitude; Instrumentation instruments;

    [[nodiscard]] const math::Vec3& position() const { return dynamics.state.position; }
    [[nodiscard]] const math::Vec3& velocity() const { return dynamics.state.velocity; }
    [[nodiscard]] double mass() const { return dynamics.mass; }
};

}  // namespace sf::spacecraft
