#pragma once

// Quaternion PD attitude control, and the ForceModel that turns its torque
// request into actual RCS thrust.
//
// The controller ASKS for torque; the RCS provides what it can. A slew the
// thrusters cannot achieve comes out slow, never instantaneous -- nothing here
// writes to the orientation (rule 28).
// See docs/physics/attitude.md section 7.

#include "core/attitude/inertia.hpp"
#include "core/attitude/rcs.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/gravity/force_model.hpp"
#include "core/navigation/maneuver.hpp"

#include <optional>
#include <string>

namespace sf::attitude {

struct PointingGains {
    // Second-order response of the slew: these are physical, not magic numbers.
    // omega_n is the natural frequency of the closed loop [rad/s] -- 0.05 gives a
    // settling time of order a minute -- and zeta = 1 is critical damping, the
    // fastest approach with no overshoot.
    double natural_frequency{0.05};
    double damping_ratio{1.0};
};

// Where the nose should point. Reuses the guidance modes of the maneuver
// planner so that "prograde" has exactly one definition in the program.
struct PointingCommand {
    std::optional<navigation::GuidanceMode> mode;  // nullopt = hold attitude
    celestial::BodyId reference{celestial::bodies::earth};
    math::Vec3 inertial_direction{1.0, 0.0, 0.0};  // used by GuidanceMode::Inertial
};

class PointingController {
public:
    PointingController(const ephemeris::EphemerisProvider& provider,
                       const InertiaTensor& inertia,
                       coordinates::ReferenceFrame frame =
                           coordinates::ReferenceFrame::ssb_j2000(),
                       PointingGains gains = {});

    void set_command(PointingCommand command) { command_ = command; }
    [[nodiscard]] const PointingCommand& command() const noexcept { return command_; }
    void set_gains(PointingGains gains) { gains_ = gains; }

    // Unit vector the nose should point along, in the integration frame.
    [[nodiscard]] std::optional<math::Vec3> desired_direction(
        const propagation::PropagationState& state, time::CoordinateTime t) const;

    // Orientation that puts the body +x axis on the desired direction. The
    // shortest such rotation: roll is left free, because nothing here has an
    // opinion about it yet.
    [[nodiscard]] std::optional<math::Quaternion> desired_orientation(
        const propagation::PropagationState& state, time::CoordinateTime t) const;

    // Body-frame torque request. Zero when there is no command.
    [[nodiscard]] math::Vec3 desired_torque(const propagation::PropagationState& state,
                                            time::CoordinateTime t) const;

    // Angle still to be turned through [rad], for the cockpit.
    [[nodiscard]] double pointing_error(const propagation::PropagationState& state,
                                        time::CoordinateTime t) const;

private:
    const ephemeris::EphemerisProvider& provider_;
    const InertiaTensor& inertia_;
    coordinates::ReferenceFrame frame_;
    PointingGains gains_;
    PointingCommand command_{};
};

// The ForceModel that fires the thrusters the controller asks for.
class RcsForce final : public gravity::ForceModel {
public:
    RcsForce(const RcsSystem& rcs, const PointingController& controller);

    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState& state,
                                                time::CoordinateTime t) const override;
    [[nodiscard]] std::string_view name() const override { return "RcsForce"; }

    // Manual override, in the body frame. Set to zero to hand control back to
    // the pointing controller.
    void set_manual_torque(const math::Vec3& torque_body) { manual_torque_ = torque_body; }
    [[nodiscard]] const math::Vec3& manual_torque() const noexcept { return manual_torque_; }

private:
    const RcsSystem& rcs_;
    const PointingController& controller_;
    math::Vec3 manual_torque_{};
};

}  // namespace sf::attitude
