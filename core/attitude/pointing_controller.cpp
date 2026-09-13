#include "core/attitude/pointing_controller.hpp"

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

std::optional<Vec3> PointingController::desired_direction(
    const propagation::PropagationState& state, time::CoordinateTime t) const {
    if (!command_.mode.has_value()) {
        return std::nullopt;
    }
    if (*command_.mode == navigation::GuidanceMode::Inertial) {
        return command_.inertial_direction.normalized();
    }

    const auto reference = provider_.state(command_.reference, t, frame_);
    const Vec3 r = state.state.position - reference.state.position;
    const Vec3 v = state.state.velocity - reference.state.velocity;

    switch (*command_.mode) {
        case navigation::GuidanceMode::Prograde:   return v.normalized();
        case navigation::GuidanceMode::Retrograde: return -v.normalized();
        case navigation::GuidanceMode::Normal:     return cross(r, v).normalized();
        case navigation::GuidanceMode::AntiNormal: return -cross(r, v).normalized();
        case navigation::GuidanceMode::RadialOut:  return r.normalized();
        case navigation::GuidanceMode::RadialIn:   return -r.normalized();
        case navigation::GuidanceMode::Inertial:   break;
    }
    return command_.inertial_direction.normalized();
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

Vec3 PointingController::desired_torque(const propagation::PropagationState& state,
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

gravity::ForceResult RcsForce::evaluate(const propagation::PropagationState& state,
                                        time::CoordinateTime t) const {
    gravity::ForceResult result{};

    const Vec3 requested = manual_torque_.norm_squared() > 0.0
                               ? manual_torque_
                               : controller_.desired_torque(state, t);
    if (requested.norm_squared() <= 0.0) {
        return result;
    }

    const RcsOutput output = rcs_.evaluate(requested);

    result.torque = output.torque_body;
    // The force is produced in the body frame and acts in the inertial one. A
    // balanced couple cancels here and this term is zero -- which is the point of
    // laying the thrusters out in couples.
    result.proper_thrust = state.attitude.orientation.rotate(output.force_body);
    result.mass_flow_rate = -output.mass_flow;
    return result;
}

}  // namespace sf::attitude
