#include "core/attitude/pointing_controller.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sf::attitude {

using math::Quaternion;
using math::Vec3;

PointingController::PointingController(const ephemeris::EphemerisProvider& provider,
                                       const InertiaTensor& inertia,
                                       coordinates::ReferenceFrame frame, PointingGains gains)
    : provider_(provider), inertia_(inertia), frame_(frame), gains_(gains) {
    if (!(gains_.natural_frequency > 0.0) || !(gains_.damping_ratio > 0.0)) {
        throw std::invalid_argument("PointingController: gains must be > 0");
    }
}

std::optional<Vec3> PointingController::direction_for(navigation::GuidanceMode mode,
                                                      celestial::BodyId reference,
                                                      const propagation::PropagationState& state,
                                                      time::CoordinateTime t) const {
    if (mode == navigation::GuidanceMode::Inertial) {
        return command_.inertial_direction.normalized();
    }

    const auto body = provider_.state(reference, t, frame_);
    const Vec3 r = state.state.position - body.state.position;
    const Vec3 v = state.state.velocity - body.state.velocity;

    switch (mode) {
        case navigation::GuidanceMode::Prograde:   return v.normalized();
        case navigation::GuidanceMode::Retrograde: return -v.normalized();
        case navigation::GuidanceMode::Normal:     return cross(r, v).normalized();
        case navigation::GuidanceMode::AntiNormal: return -cross(r, v).normalized();
        case navigation::GuidanceMode::RadialOut:  return r.normalized();
        case navigation::GuidanceMode::RadialIn:   return -r.normalized();
        // "Point where you are pointing" is not a command, it is the absence of
        // one: a controller asked to track the hull's own axis has nothing to do
        // and would report zero error from any attitude.  Answering nullopt sends
        // it down the HOLD path, which is the honest reading.
        case navigation::GuidanceMode::Hull:       return std::nullopt;
        // A rendezvous direction needs the arrival and the ephemeris, which the
        // maneuver carries and a pointing command does not: the caller asks the
        // executor for it and commands it as INERTIAL, every frame.
        case navigation::GuidanceMode::Rendezvous: return std::nullopt;
        case navigation::GuidanceMode::Inertial:   break;
    }
    return command_.inertial_direction.normalized();
}

std::optional<Vec3> PointingController::desired_direction(
    const propagation::PropagationState& state, time::CoordinateTime t) const {
    if (!command_.mode.has_value()) {
        return std::nullopt;
    }
    return direction_for(*command_.mode, command_.reference, state, t);
}

std::optional<Quaternion> PointingController::desired_orientation(
    const propagation::PropagationState& state, time::CoordinateTime t) const {
    const auto direction = desired_direction(state, t);
    if (!direction.has_value()) {
        return std::nullopt;
    }
    // Shortest rotation putting the nose (body +x) on the target direction.
    return Quaternion::from_two_vectors(Vec3::unit_x(), *direction);
}

ActuatorLimits limits_of(const RcsSystem& rcs) {
    ActuatorLimits limits{};
    limits.active = true;
    limits.max_torque = rcs.max_torque();
    return limits;
}

double PointingController::saturation_factor(const Vec3& demand_body) const {
    if (!limits_.active) {
        return 1.0;
    }
    // One factor for the whole vector, set by the axis that runs out first.  Per
    // axis clipping would keep more torque and point it somewhere else; see the
    // note on ActuatorLimits.
    double factor = 1.0;
    const double components[3] = {std::abs(demand_body.x), std::abs(demand_body.y),
                                  std::abs(demand_body.z)};
    const double available[3] = {limits_.max_torque.x, limits_.max_torque.y,
                                 limits_.max_torque.z};
    for (int i = 0; i < 3; ++i) {
        if (components[i] > 0.0) {
            // An axis with no authority at all cannot be scaled into compliance;
            // the honest factor is zero, and the demand is refused rather than
            // delivered about some other axis.
            const double allowed = available[i] > 0.0 ? available[i] / components[i] : 0.0;
            factor = std::min(factor, allowed);
        }
    }
    return factor;
}

Vec3 PointingController::unsaturated_torque(const propagation::PropagationState& state,
                                            time::CoordinateTime t) const {
    const auto target = desired_orientation(state, t);
    if (!target.has_value()) {
        return Vec3{};
    }

    const Quaternion current = state.attitude.orientation.normalized();
    // Rotation from the target to where we actually are, in the BODY frame.
    Quaternion error = target->conjugate() * current;
    // q and -q are the same orientation; without this the ship sometimes slews
    // 300 degrees to reach what was 60 degrees away.
    if (error.w() < 0.0) {
        error = -error;
    }

    // For a small error, 2*vec(q) is the rotation vector. The law is written in
    // terms of a natural frequency and a damping ratio and then multiplied by the
    // inertia, so the closed-loop response is the same about every axis even
    // though the moments are not.
    const double wn = gains_.natural_frequency;
    const double zeta = gains_.damping_ratio;

    const Vec3 proportional = error.vector() * (2.0 * wn * wn);
    const Vec3 derivative = state.attitude.angular_velocity * (2.0 * zeta * wn);
    return inertia_.tensor() * (-(proportional + derivative));
}

Vec3 PointingController::desired_torque(const propagation::PropagationState& state,
                                        time::CoordinateTime t) const {
    const Vec3 demand = unsaturated_torque(state, t);
    if (!limits_.active || demand.norm_squared() <= 0.0) {
        return demand;
    }
    return demand * saturation_factor(demand);
}

double PointingController::pointing_error(const propagation::PropagationState& state,
                                          time::CoordinateTime t) const {
    const auto direction = desired_direction(state, t);
    if (!direction.has_value()) {
        return 0.0;
    }
    const Vec3 nose = state.attitude.orientation.rotate(Vec3::unit_x());
    return angle_between(nose, *direction);
}

RcsForce::RcsForce(const RcsSystem& rcs, const PointingController& controller)
    : rcs_(rcs), controller_(controller) {}

std::vector<double> RcsForce::throttles(const propagation::PropagationState& state,
                                        time::CoordinateTime t) const {
    const Vec3 requested_torque = manual_torque_.norm_squared() > 0.0
                                      ? manual_torque_
                                      : controller_.desired_torque(state, t);

    std::vector<double> open = rcs_.allocate(requested_torque);

    if (manual_force_.norm_squared() > 0.0) {
        // A translation demand on top of a rotation demand. The two allocations
        // are added and the SUM is clamped per thruster, which is what the
        // hardware does: a valve that is already wide open for the slew cannot
        // open further for the push, and the shortfall shows up as the
        // translation being weaker than asked rather than as a torque nobody
        // commanded. Not an optimal allocator, for the same reason allocate()
        // is not one, and named as such in the same place.
        const auto translation = rcs_.allocate_force(manual_force_);
        for (std::size_t i = 0; i < open.size() && i < translation.size(); ++i) {
            open[i] = std::clamp(open[i] + translation[i], 0.0, 1.0);
        }
    }
    return open;
}

gravity::ForceResult RcsForce::evaluate(const propagation::PropagationState& state,
                                        time::CoordinateTime t) const {
    gravity::ForceResult result{};

    const auto open = throttles(state, t);
    bool any = false;
    for (const double throttle : open) {
        if (throttle > 0.0) {
            any = true;
            break;
        }
    }
    if (!any) {
        return result;
    }

    const RcsOutput output = rcs_.evaluate(open);

    result.torque = output.torque_body;
    // The force is produced in the body frame and acts in the inertial one. A
    // balanced couple cancels here and this term is zero -- which is the point of
    // laying the thrusters out in couples.
    result.proper_thrust = state.attitude.orientation.rotate(output.force_body);
    result.mass_flow_rate = -output.mass_flow;
    return result;
}

}  // namespace sf::attitude
