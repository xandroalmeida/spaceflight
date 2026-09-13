#pragma once

// A contribution to the spacecraft's equation of motion.
//
// Contract:
//  * const and reentrant -- the integrator calls it up to 7 times per step, out
//    of chronological order, and may one day do so from several threads;
//  * no hidden state carried between calls;
//  * it returns an acceleration, not a "position correction".  Nothing in the
//    force layer is ever allowed to move the spacecraft directly.

#include "core/celestial/body_id.hpp"
#include "core/math/vec3.hpp"
#include "core/propagation/propagation_state.hpp"
#include "core/time/coordinate_time.hpp"

#include <string_view>

namespace sf::gravity {

struct ForceResult {
    math::Vec3 acceleration{};  // [m/s^2], in the integration frame

    // Rate of change of the spacecraft's rest mass [kg/s].  Negative while an
    // engine is burning, zero for every conservative force.  It is part of the
    // force result because thrust and consumption are one physical process:
    // F = eta * q * w with q = -dm/dt (docs/physics/propulsion-model.md section 3).
    // Reporting them separately would allow them to drift apart, which is exactly
    // the failure mode rule section 17 forbids.
    double mass_flow_rate{0.0};

    // Torque about the centre of mass, in the BODY frame [N m]. Body frame
    // because that is where the inertia tensor is constant, so that is where
    // Euler's equations are written (docs/physics/attitude.md section 2).
    math::Vec3 torque{};

    // Set when the evaluation happened inside a body's radius.  Point-mass
    // gravity remains mathematically defined there but stops being physical, so
    // the condition is reported instead of being clamped away.
    bool inside_body{false};
    celestial::BodyId inside_of{};
};

class ForceModel {
public:
    ForceModel() = default;
    virtual ~ForceModel() = default;
    ForceModel(const ForceModel&) = delete;
    ForceModel& operator=(const ForceModel&) = delete;

    [[nodiscard]] virtual ForceResult evaluate(const propagation::PropagationState& spacecraft,
                                               time::CoordinateTime t) const = 0;

    [[nodiscard]] virtual std::string_view name() const = 0;
};

}  // namespace sf::gravity
