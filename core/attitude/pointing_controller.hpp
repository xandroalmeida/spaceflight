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
    // omega_n is the natural frequency of the closed loop [rad/s] and zeta = 1 is
    // critical damping, the fastest approach with no overshoot.
    //
    // ---------------------------------------------------------------------
    // Why 0.20 and not the 0.05 this used to be (Milestone 6.2, sections 8-10)
    //
    // A PD controller is type 0.  Tracking a RAMP -- a target direction that
    // rotates at a constant rate -- leaves a standing error, because the
    // derivative term needs a permanent proportional error to supply the
    // permanent torque:
    //
    //     theta_lag = 2 zeta omega / omega_n
    //
    // At the periapsis of a 100 km lunar orbit the retrograde direction rotates
    // at omega = sqrt(mu/r^3) = 8.886e-4 rad/s.  At omega_n = 0.05 that is
    // 0.03554 rad = 2.036 degrees of standing error against 2.064 measured, 1.4 %
    // -- the formula predicts the PEAK, because omega was evaluated at periapsis
    // where the retrograde direction turns fastest and that is where the peak is
    // -- and the
    // engine spends the whole capture burn pointed two degrees off retrograde.
    // The transverse component of the thrust is LINEAR in that angle, so it
    // dominates the cosine loss, and the captured orbit came out at e = 0.0105
    // against 0.0017 for the same trajectory under ideal guidance.
    //
    // The sweep in docs/validation/autopilot-hardening.md walks six bandwidths
    // over three epochs and confirms the 1/omega_n law to 0.4 %:
    //
    //     omega_n   mean lag   peak lag   captured   e
    //     0.050     1.653 deg  2.065 deg  0 / 3      0.0105
    //     0.075     1.107      1.373      3 / 3      0.0064
    //     0.100     0.829      1.031      3 / 3      0.0044
    //     0.150     0.550      0.683      3 / 3      0.0023
    //     0.200     0.412      0.512      3 / 3      0.0013
    //
    // The specification is peak < 1.0 deg and mean < 0.5 deg, and 0.20 is the
    // SMALLEST swept gain that meets both -- 0.15 misses the mean by 10 %.  It
    // is not the largest available, and that is deliberate: bandwidth is not
    // free (section 10).  Between 0.05 and 0.20 the RCS propellant per mission
    // rises from 0.047 g to 0.177 g and the peak slew rate from 46 to 186
    // mrad/s, both linear in omega_n; the integrator also takes about three
    // times as long, which is stiffness rather than waste.
    //
    // What this is NOT is a fix for the lag.  The lag is removable, by
    // feed-forward of the target rate or by an integral term, and neither is
    // implemented -- both are named in docs/physics/attitude.md section 7.
    // Raising the gain buys the specification with authority; the next milestone
    // should buy it with structure.
    double natural_frequency{0.20};
    double damping_ratio{1.0};
};

// What the actuators can actually deliver (Milestone 6.2 section 11).
//
// The PD law above produces a DEMAND, and the demand is unbounded: the torque it
// asks for goes as the pointing error, and at the start of a 180 degree slew that
// is hundreds of times what twelve thrusters on a 2 m arm can produce.
//
// Without this the demand still gets truncated -- RcsSystem::allocate clamps each
// thruster to [0, 1] -- but it gets truncated PER THRUSTER, which changes the
// direction of the delivered torque as well as its size whenever the axes
// saturate by different amounts.  The ship then turns about an axis nobody asked
// for.  Clamping here instead scales the whole vector, so a saturated command is
// the right rotation delivered slowly rather than the wrong rotation delivered
// fast.
//
// `available` is filled from RcsSystem::max_torque_about, so it is the layout's
// arithmetic and not a number chosen to make a plot look better.
struct ActuatorLimits {
    bool active{false};
    math::Vec3 max_torque{};   // [N m] achievable about each body axis
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
    [[nodiscard]] const PointingGains& gains() const noexcept { return gains_; }

    // Tell the controller what the thrusters can do.  Derived from the layout:
    //     controller.set_actuator_limits(ActuatorLimits::from(rcs));
    void set_actuator_limits(ActuatorLimits limits) { limits_ = limits; }
    [[nodiscard]] const ActuatorLimits& actuator_limits() const noexcept { return limits_; }

    // The demand BEFORE saturation, and the factor saturation applied to it.
    // Separated from desired_torque so that an instrument can say how hard the
    // controller was pushing against the ceiling instead of only seeing the
    // clipped result.
    [[nodiscard]] math::Vec3 unsaturated_torque(const propagation::PropagationState& state,
                                                time::CoordinateTime t) const;
    [[nodiscard]] double saturation_factor(const math::Vec3& demand_body) const;

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
    ActuatorLimits limits_{};
};

// The limits a given RCS layout implies.  Free function rather than a member of
// RcsSystem so that attitude/rcs.hpp stays ignorant of the controller.
[[nodiscard]] ActuatorLimits limits_of(const RcsSystem& rcs);

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
