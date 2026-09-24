#pragma once

// Turns a ManeuverPlan into a force.  This is the only place in the project that
// makes the spacecraft accelerate under its own power -- and it does it the same
// way gravity does, by returning an acceleration to the integrator.  Nothing here
// writes to the state (rule section 28).

#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/gravity/force_model.hpp"
#include "core/navigation/maneuver.hpp"
#include "core/propagation/spacecraft_propagator.hpp"
#include "core/spacecraft/spacecraft.hpp"

namespace sf::navigation {

class ManeuverExecutor final : public gravity::ForceModel {
public:
    // All three references must outlive the executor.
    ManeuverExecutor(const ephemeris::EphemerisProvider& provider,
                     const spacecraft::Spacecraft& craft,
                     const ManeuverPlan& plan,
                     coordinates::ReferenceFrame frame = coordinates::ReferenceFrame::ssb_j2000());

    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState& state,
                                                time::CoordinateTime t) const override;

    [[nodiscard]] std::string_view name() const override { return "ManeuverExecutor"; }

    // Unit vector the engine points along for `maneuver` in the given state.
    // Exposed for diagnostics and for the CLI; the integrator never calls it.
    [[nodiscard]] math::Vec3 thrust_direction(const propagation::PropagationState& state,
                                              time::CoordinateTime t,
                                              const Maneuver& maneuver) const;

    // The rest-frame thrust vector a RENDEZVOUS maneuver asks for at this state,
    // already limited to what the engine gives at the maneuver's throttle [N].
    // docs/physics/direct-transfer-guidance.md sections 2 and 3.
    [[nodiscard]] math::Vec3 rendezvous_thrust(const propagation::PropagationState& state,
                                               time::CoordinateTime t, const Maneuver& maneuver) const;
    // The rest-frame thrust that produces exactly `coordinate_acceleration` for
    // a ship moving at `velocity` (coordinate), under `kinematics` [N].
    [[nodiscard]] static math::Vec3 proper_thrust_for(const math::Vec3& coordinate_acceleration,
                                                      const math::Vec3& velocity, double mass,
                                                      propagation::Kinematics kinematics);
    // The coordinate acceleration the law commands, before any conversion.
    [[nodiscard]] math::Vec3 rendezvous_acceleration(const propagation::PropagationState& state,
                                                     time::CoordinateTime t,
                                                     const Maneuver& maneuver) const;

    // Which kinematics the integrator runs, so that a commanded coordinate
    // acceleration becomes the rest-frame thrust that produces exactly it:
    // F = m a under Newton, F = gamma m M^-1 du/dt under special relativity.
    void set_kinematics(propagation::Kinematics kinematics) { kinematics_ = kinematics; }
    [[nodiscard]] propagation::Kinematics kinematics() const noexcept { return kinematics_; }

    // Selects which one-sided limit to evaluate at a switch instant.
    //
    // At t = cutoff the thrust is genuinely ambiguous: the burn is over by
    // convention, but the last step of the burn leg is integrating the engine-on
    // dynamics UP TO that instant and needs the left-hand limit there.  The next
    // leg needs the right-hand limit at the same epoch.  Only the mission runner
    // knows which leg is being integrated, so it arms the executor before each
    // one and the force stays constant across the whole leg -- which is exactly
    // what an adaptive integrator requires.
    //
    // Without arming, evaluate() answers by looking the epoch up in the plan.
    // That is correct as a function of time, but it makes a switch instant a
    // discontinuity INSIDE a step, and the error controller responds by grinding
    // the step size down to the floor.  run_mission() always arms.
    //
    // Arming is not thread safe and is not meant to be: it happens between legs,
    // never during one.
    void arm(const Maneuver* maneuver);
    void disarm();
    [[nodiscard]] const Maneuver* armed() const noexcept { return armed_; }

    [[nodiscard]] const ManeuverPlan& plan() const noexcept { return plan_; }
    [[nodiscard]] const spacecraft::Spacecraft& craft() const noexcept { return craft_; }
    [[nodiscard]] const ephemeris::EphemerisProvider& provider() const noexcept { return provider_; }
    [[nodiscard]] const coordinates::ReferenceFrame& frame() const noexcept { return frame_; }

    // Speed relative to a body, used by the mission runner to measure how much of
    // the engine's delta-v actually reached the orbit.
    [[nodiscard]] double speed_relative_to(const propagation::PropagationState& state,
                                           time::CoordinateTime t,
                                           celestial::BodyId body) const;

private:
    const ephemeris::EphemerisProvider& provider_;
    const spacecraft::Spacecraft& craft_;
    const ManeuverPlan& plan_;
    coordinates::ReferenceFrame frame_;
    const Maneuver* armed_{nullptr};
    bool use_armed_{false};
    propagation::Kinematics kinematics_{propagation::Kinematics::Newtonian};
};

}  // namespace sf::navigation
