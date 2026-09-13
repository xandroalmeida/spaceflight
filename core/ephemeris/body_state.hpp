#pragma once

#include "core/celestial/body_id.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/coordinates/state_vector.hpp"
#include "core/time/coordinate_time.hpp"

namespace sf::ephemeris {

// A state vector together with everything needed to interpret it.
// Units are SI (m, m/s) -- the kilometre-based SPICE convention stops at the
// provider boundary.
struct BodyState {
    celestial::BodyId body{};
    time::CoordinateTime epoch{};
    coordinates::ReferenceFrame frame{};
    coordinates::StateVector state{};

    [[nodiscard]] const math::Vec3& position() const { return state.position; }
    [[nodiscard]] const math::Vec3& velocity() const { return state.velocity; }
};

}  // namespace sf::ephemeris
