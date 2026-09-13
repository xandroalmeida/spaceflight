#include "core/propulsion/main_engine_force.hpp"

#include <algorithm>
#include <cmath>

namespace sf::propulsion {

MainEngineForce::MainEngineForce(const spacecraft::Spacecraft& craft) : craft_(craft) {}

void MainEngineForce::set_throttle(double throttle) {
    throttle_ = std::isfinite(throttle) ? std::clamp(throttle, 0.0, 1.0) : 0.0;
}

double MainEngineForce::current_thrust() const { return craft_.engine().thrust_at(throttle_); }

gravity::ForceResult MainEngineForce::evaluate(const propagation::PropagationState& state,
                                               time::CoordinateTime) const {
    gravity::ForceResult result{};
    if (throttle_ <= 0.0) {
        return result;
    }
    // An empty tank produces no thrust for the same reason a full one does: the
    // thrust IS the consumption. No special case.
    if (!craft_.has_propellant(state.mass) || !(state.mass > 0.0)) {
        return result;
    }

    const math::Vec3 nose = state.attitude.orientation.rotate(math::Vec3::unit_x());
    result.acceleration = nose * (current_thrust() / state.mass);
    result.mass_flow_rate = -craft_.engine().mass_flow_at(throttle_);
    // A main engine aligned with the centre of mass produces no torque. One that
    // is not would, and the term is here so that adding a gimbal later is a
    // change of value, not of structure.
    return result;
}

}  // namespace sf::propulsion
