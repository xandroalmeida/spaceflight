#pragma once

// Position and velocity, SI, in a frame the caller knows.
//
// StateVector deliberately does NOT carry its frame: it is the inner loop data
// of the integrator.  BodyState (core/ephemeris) is the frame-carrying type used
// at interfaces.

#include "core/math/vec3.hpp"

namespace sf::coordinates {

struct StateVector {
    math::Vec3 position{};  // [m]
    math::Vec3 velocity{};  // [m/s]

    [[nodiscard]] double radius() const { return position.norm(); }
    [[nodiscard]] double speed() const { return velocity.norm(); }
    [[nodiscard]] bool is_finite() const { return position.is_finite() && velocity.is_finite(); }

    constexpr StateVector& operator+=(const StateVector& o) {
        position += o.position;
        velocity += o.velocity;
        return *this;
    }
    constexpr StateVector& operator-=(const StateVector& o) {
        position -= o.position;
        velocity -= o.velocity;
        return *this;
    }

    friend constexpr StateVector operator+(StateVector a, const StateVector& b) { return a += b; }
    friend constexpr StateVector operator-(StateVector a, const StateVector& b) { return a -= b; }
    friend constexpr bool operator==(const StateVector&, const StateVector&) = default;
};

}  // namespace sf::coordinates
