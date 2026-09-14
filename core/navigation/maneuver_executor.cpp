#include "core/navigation/maneuver_executor.hpp"

#include <stdexcept>

namespace sf::navigation {

using math::Vec3;

ManeuverExecutor::ManeuverExecutor(const ephemeris::EphemerisProvider& provider,
                                   const spacecraft::Spacecraft& craft,
                                   const ManeuverPlan& plan,
                                   coordinates::ReferenceFrame frame)
    : provider_(provider), craft_(craft), plan_(plan), frame_(frame) {
    if (!frame_.is_inertial()) {
        throw std::invalid_argument("ManeuverExecutor: integration frame must be inertial, got " +
                                    frame_.to_string());
    }
}

Vec3 ManeuverExecutor::thrust_direction(const propagation::PropagationState& state,
                                        time::CoordinateTime t, const Maneuver& maneuver) const {
    if (maneuver.guidance == GuidanceMode::Inertial) {
        return maneuver.inertial_direction.normalized();
    }
    if (maneuver.guidance == GuidanceMode::Hull) {
        // The nose, in the integration frame.  No reference body and no ideal
        // direction: the engine points where the ship points, and where the ship
        // points is a state the integrator carries.
        return state.attitude.orientation.rotate(Vec3::unit_x()).normalized();
    }

    // Prograde, radial and normal are all defined RELATIVE to a body: "prograde"
    // around the Earth and "prograde" around the Sun are 30 km/s apart.
    const auto reference = provider_.state(maneuver.reference, t, frame_);
    const Vec3 r = state.state.position - reference.state.position;
    const Vec3 v = state.state.velocity - reference.state.velocity;

    switch (maneuver.guidance) {
        case GuidanceMode::Prograde:   return v.normalized();
        case GuidanceMode::Retrograde: return -v.normalized();
        case GuidanceMode::Normal:     return cross(r, v).normalized();
        case GuidanceMode::AntiNormal: return -cross(r, v).normalized();
        case GuidanceMode::RadialOut:  return r.normalized();
        case GuidanceMode::RadialIn:   return -r.normalized();
        case GuidanceMode::Inertial:
        case GuidanceMode::Hull:       break;
    }
    return maneuver.inertial_direction.normalized();
}

void ManeuverExecutor::arm(const Maneuver* maneuver) {
    armed_ = maneuver;
    use_armed_ = true;
}

void ManeuverExecutor::disarm() {
    armed_ = nullptr;
    use_armed_ = false;
}

double ManeuverExecutor::speed_relative_to(const propagation::PropagationState& state,
                                           time::CoordinateTime t,
                                           celestial::BodyId body) const {
    const auto reference = provider_.state(body, t, frame_);
    return (state.state.velocity - reference.state.velocity).norm();
}

gravity::ForceResult ManeuverExecutor::evaluate(const propagation::PropagationState& state,
                                                time::CoordinateTime t) const {
    gravity::ForceResult result{};

    const Maneuver* maneuver = use_armed_ ? armed_ : plan_.active_at(t);
    if (maneuver == nullptr) {
        return result;  // coasting: no thrust, no consumption
    }

    // An empty tank produces no thrust for the same reason a full one does: the
    // thrust IS the consumption (F = eta*q*w).  No special case, no clamp.
    if (!craft_.has_propellant(state.mass)) {
        return result;
    }

    const auto& engine = craft_.engine();
    const double flow = engine.mass_flow_at(maneuver->throttle);
    const double thrust = engine.thrust_at(maneuver->throttle);

    if (!(state.mass > 0.0)) {
        throw std::runtime_error("ManeuverExecutor: non-positive spacecraft mass");
    }

    result.proper_thrust = thrust_direction(state, t, *maneuver) * thrust;
    result.mass_flow_rate = -flow;
    return result;
}

}  // namespace sf::navigation
