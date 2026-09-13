#pragma once

// Orientation and angular velocity.
//
// There is no pitch/yaw/roll here, by design (rule 19): those are a display
// projection of this, computed on demand and never stored.

#include "core/math/quaternion.hpp"
#include "core/math/vec3.hpp"

namespace sf::attitude {

struct AttitudeState {
    // Body -> inertial, in the integration frame's axes (ADR-0008).
    math::Quaternion orientation{math::Quaternion::identity()};
    // In the BODY frame, which is where the inertia tensor is constant.
    math::Vec3 angular_velocity{};

    [[nodiscard]] bool is_finite() const {
        return orientation.is_finite() && angular_velocity.is_finite();
    }

    // Body axes expressed in the inertial frame.
    [[nodiscard]] math::Vec3 forward() const { return orientation.rotate(math::Vec3::unit_x()); }
    [[nodiscard]] math::Vec3 left() const { return orientation.rotate(math::Vec3::unit_y()); }
    [[nodiscard]] math::Vec3 up() const { return orientation.rotate(math::Vec3::unit_z()); }
};

}  // namespace sf::attitude
