#include "core/navigation/maneuver_executor.hpp"

#include "core/units/constants.hpp"

#include <algorithm>
#include <cmath>
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
    if (maneuver.guidance == GuidanceMode::Rendezvous) {
        const Vec3 thrust = rendezvous_thrust(state, t, maneuver);
        return thrust.norm() > 0.0 ? thrust.normalized() : maneuver.inertial_direction.normalized();
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
        case GuidanceMode::Hull:
        case GuidanceMode::Rendezvous: break;
    }
    return maneuver.inertial_direction.normalized();
}

Vec3 ManeuverExecutor::rendezvous_acceleration(const propagation::PropagationState& state,
                                               time::CoordinateTime t, const Maneuver& maneuver) const {
    // Energy-optimal, fixed-time, with the acceleration brought to ZERO at the
    // arrival: a = 12 ZEM / t_go^2 - 6 ZEV / t_go. The plain law (6 and 2) ends
    // at its peak -- still braking hard at t_go = 0 -- so any cutoff before the
    // singularity leaves that braking undone: 30 s early was 28 km/s at the Moon.
    // With a(t_f) = 0 the last second carries under 1 m/s. The destination's
    // state at the arrival comes from the ephemeris, so a moving target is part
    // of the equation; gravity is left out and absorbed by the feedback
    // (docs/physics/direct-transfer-guidance.md section 2).
    const double t_go = (maneuver.arrival - t).seconds();
    if (!(t_go > 0.0)) {
        return {};
    }
    const auto destination = provider_.state(maneuver.reference, maneuver.arrival, frame_);
    const Vec3 wanted_position = destination.state.position + maneuver.arrival_offset;
    const Vec3 wanted_velocity = destination.state.velocity + maneuver.arrival_velocity;
    const Vec3 zem = wanted_position - (state.state.position + state.state.velocity * t_go);
    const Vec3 zev = wanted_velocity - state.state.velocity;
    return zem * (12.0 / (t_go * t_go)) - zev * (6.0 / t_go);
}

Vec3 ManeuverExecutor::proper_thrust_for(const Vec3& coordinate_acceleration, const Vec3& velocity, double mass,
                                         propagation::Kinematics kinematics) {
    const Vec3& a = coordinate_acceleration;
    if (!propagation::carries_proper_velocity(kinematics) || !(velocity.norm() > 0.0)) {
        return a * mass;
    }
    // u = gamma v, so du/dt = gamma a + gamma^3 (v.a) v / c^2; the integrator
    // applies du/dt = M n F / (gamma m) with M = I + (gamma - 1) b b^T, and
    // M^-1 = I - ((gamma - 1)/gamma) b b^T. Parallel to the motion this is
    // F = gamma^3 m a, across it F = gamma^2 m a.
    const double speed = velocity.norm();
    const double gamma = 1.0 / std::sqrt(1.0 - speed * speed / units::c_squared);
    const Vec3 b = velocity / speed;
    const Vec3 du_dt = a * gamma + velocity * (gamma * gamma * gamma * dot(velocity, a) / units::c_squared);
    const Vec3 inverse_m = du_dt - b * (((gamma - 1.0) / gamma) * dot(b, du_dt));
    return inverse_m * (gamma * mass);
}

Vec3 ManeuverExecutor::rendezvous_thrust(const propagation::PropagationState& state,
                                         time::CoordinateTime t, const Maneuver& maneuver) const {
    Vec3 force = proper_thrust_for(rendezvous_acceleration(state, t, maneuver), state.state.velocity, state.mass,
                                   kinematics_);
    // The engine's ceiling at the maneuver's throttle. A saturated command keeps
    // its direction; the feedback sees the shortfall and recovers it.
    const double ceiling = craft_.engine().thrust_at(maneuver.throttle);
    const double magnitude = force.norm();
    if (magnitude > ceiling && magnitude > 0.0) {
        force = force * (ceiling / magnitude);
    }
    return force;
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
    if (!(state.mass > 0.0)) {
        throw std::runtime_error("ManeuverExecutor: non-positive spacecraft mass");
    }

    if (maneuver->guidance == GuidanceMode::Rendezvous) {
        // Throttled by the law: the consumption follows the thrust actually
        // asked for, F = eta q w, so a burn that needs half the engine burns
        // half the propellant.
        const Vec3 thrust = rendezvous_thrust(state, t, *maneuver);
        const double full = engine.max_thrust();
        result.proper_thrust = thrust;
        result.mass_flow_rate = full > 0.0 ? -engine.mass_flow_at(thrust.norm() / full) : 0.0;
        return result;
    }

    const double flow = engine.mass_flow_at(maneuver->throttle);
    const double thrust = engine.thrust_at(maneuver->throttle);

    result.proper_thrust = thrust_direction(state, t, *maneuver) * thrust;
    result.mass_flow_rate = -flow;
    return result;
}

}  // namespace sf::navigation
