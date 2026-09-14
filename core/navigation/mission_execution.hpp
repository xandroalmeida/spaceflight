#pragma once

// Where a planned mission has got to, and how the flight of it compares with the
// plan (Milestone 6.2 sections 16 and 17).
//
// ---------------------------------------------------------------------------
// Why this is in the core and not in the cockpit
//
// "The ship is on approach" is a claim about the trajectory: it depends on where
// the destination's sphere of influence is, on where the burns sit, on whether
// the attitude has settled.  Every one of those is physics, and physics that
// lived in GDScript would be a third place trajectories are reasoned about --
// after the core and after the GDExtension planner this milestone deleted.
//
// So the phase is COMPUTED here from the plan and the state, and the user
// interface reads a label.  It decides nothing.
//
// ---------------------------------------------------------------------------
// What this class does NOT do
//
// It does not steer, burn, abort or retarget.  It is an observer: `update` takes
// a state and returns nothing but a classification.  Nothing in here writes to a
// PropagationState, and that is the same rule the attitude controller follows --
// a component that both measures and acts can always hide one inside the other.
//
// The one thing it remembers is the OUTCOME: what the plan predicted against
// what the flight delivered, captured at the moment the orbit becomes an orbit.
// Section 17 calls that difference a central metric of the simulator, and a
// metric nobody records is an opinion.

#include "core/attitude/pointing_controller.hpp"
#include "core/celestial/body_id.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/navigation/maneuver.hpp"
#include "core/navigation/mission_planner.hpp"
#include "core/propagation/propagation_state.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/time/coordinate_time.hpp"
#include "core/units/angle.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace sf::navigation {

// Section 16, verbatim, plus Idle -- because "no mission" is a state the cockpit
// spends most of its time in and calling it PLANNED would be a lie.
enum class MissionPhase {
    Idle,
    Planned,
    WaitingForDeparture,
    Orienting,
    InjectionBurn,
    Coast,

    // Declared and, with the plan shape this milestone ships, never entered.
    //
    // That is not an oversight and it is not dead code: the differential
    // corrector folds its authority into the INJECTION -- the whole point of
    // correcting the departure velocity rather than adding a burn later -- so a
    // flown plan contains two maneuvers and neither is a midcourse.  The phase
    // is entered when a maneuver named "midcourse" is running, so the day the
    // planner emits one the cockpit already knows what to call it.
    MidcourseCorrection,

    Approach,
    CaptureOrienting,
    CaptureBurn,
    OrbitInsertion,
    Complete,
    Aborted,
    Failed
};

[[nodiscard]] std::string_view to_string(MissionPhase phase);

// One row of section 17's table.
struct PredictedVersusActual {
    double predicted{0.0};
    double actual{0.0};
    bool recorded{false};

    [[nodiscard]] double difference() const { return actual - predicted; }
};

struct MissionOutcome {
    bool recorded{false};
    std::string note;

    PredictedVersusActual periapsis_altitude{};   // [m]
    PredictedVersusActual apoapsis_altitude{};    // [m]
    PredictedVersusActual eccentricity{};
    PredictedVersusActual inclination_deg{};
    PredictedVersusActual propellant_used{};      // [kg]
    PredictedVersusActual arrival_tdb_s{};        // [s] since J2000
    PredictedVersusActual capture_delta_v{};      // [m/s]
};

// Thresholds, all of them stated rather than tuned.
struct MissionExecutionConfig {
    // Where a slew counts as finished, and therefore where ORIENTING ends.
    units::Angle pointing_settled{units::Angle::degrees(0.5)};

    // How long before an ignition the ship is considered to be getting ready for
    // it.  Ten minutes: a 180 degree slew of this hull at the qualified
    // bandwidth settles in under three (docs/validation/autopilot-hardening.md),
    // so this is generous on purpose -- the phase is a label for the pilot, not
    // a deadline for the controller.
    time::Duration orienting_lead{time::Duration::minutes(10.0)};

    // Where APPROACH begins: inside this multiple of the destination's Hill
    // radius.  The Hill radius is computed from the two gravitational parameters
    // and the actual separation, not hard-coded -- the Moon's is 61 500 km, and
    // a constant with that value in it would be a Moon-shaped assumption in a
    // class that is otherwise about any destination.
    double approach_hill_fraction{1.0};

    // How long after the capture burn's cutoff the orbit is read.  One full
    // revolution, so that what gets recorded is an ORBIT and not the instant the
    // engine stopped; the planner measures it the same way.
    double orbit_settling_revolutions{1.0};
};

// ---------------------------------------------------------------------------

class MissionExecution {
public:
    explicit MissionExecution(MissionExecutionConfig config = {}) : config_(config) {}

    // The ship, for the one question that cannot be answered without it: how
    // much delta-v the capture burn ACTUALLY delivered, by Tsiolkovsky on the
    // mass it consumed.  Optional -- without it that row of section 17's table
    // is reported as not recorded rather than filled in with the plan's own
    // number dressed up as a measurement.
    void set_vehicle(const spacecraft::Spacecraft* vehicle) { vehicle_ = vehicle; }

    // Arm a plan.  Refuses one that is not Planned: arming a failure is how a
    // ship ends up flying a burn nobody planned.
    void arm(const MissionPlanResult& plan);
    void abort();
    void clear();

    // Classify.  `state` is the ship in the integration frame, `now` the
    // coordinate time it is at, and `pointing_error` the angle the attitude
    // controller currently reports (zero when there is no controller, which
    // correctly skips the ORIENTING phases rather than pretending they passed).
    void update(const ephemeris::EphemerisProvider& provider, time::CoordinateTime now,
                const propagation::PropagationState& state, units::Angle pointing_error);

    [[nodiscard]] MissionPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool armed() const noexcept {
        return phase_ != MissionPhase::Idle && phase_ != MissionPhase::Aborted;
    }
    [[nodiscard]] const MissionMetrics& predicted() const noexcept { return predicted_; }
    [[nodiscard]] const ManeuverPlan& plan() const noexcept { return plan_; }
    [[nodiscard]] const MissionOutcome& outcome() const noexcept { return outcome_; }

    // Seconds until the next scheduled event, negative once it has passed.
    [[nodiscard]] double seconds_to_injection(time::CoordinateTime now) const;
    [[nodiscard]] double seconds_to_capture(time::CoordinateTime now) const;

    // Where the nose has to be for the CURRENT phase, or nullopt when the
    // mission has no opinion (idle, aborted, finished) and the attitude belongs
    // to whoever is flying.
    //
    // This is here rather than in the cockpit for the same reason the phase is:
    // "point retrograde about the destination" is a statement about the
    // trajectory, and the plan is what knows which burn is next and which way it
    // points.  The caller applies it to a controller; it does not decide it.
    //
    // Without this, MissionPhase::ORIENTING is a label nobody acts on -- the
    // phase would report that the ship is getting ready and nothing would be
    // getting ready.
    [[nodiscard]] std::optional<attitude::PointingCommand> pointing_command() const;

    [[nodiscard]] std::string describe() const;

private:
    void record_outcome(const ephemeris::EphemerisProvider& provider, time::CoordinateTime now,
                        const propagation::PropagationState& state);

    MissionExecutionConfig config_{};
    MissionPhase phase_{MissionPhase::Idle};
    MissionMetrics predicted_{};
    ManeuverPlan plan_{};
    celestial::BodyId origin_{celestial::bodies::earth};
    celestial::BodyId destination_{celestial::bodies::moon};
    MissionOutcome outcome_{};

    const spacecraft::Spacecraft* vehicle_{nullptr};

    // Where the elements are read, one revolution after cutoff.
    time::CoordinateTime settle_at_{};
    bool has_settle_time_{false};

    // The mass on either side of the capture burn.
    //
    // "Before" is taken at the last observed instant BEFORE ignition rather than
    // the first one inside the burn, and that is not a detail.  The capture burn
    // lasts about 83 s; a caller that samples the flight more coarsely than that
    // -- the regression test walks a 5-day arc in 4000 steps, or 119 s apart --
    // would otherwise take its "before" reading from most of the way through the
    // burn and report a delta-v 20 m/s short of what the engine delivered.
    // Mass is constant while coasting, so any pre-ignition sample is exact.
    double mass_before_capture_{0.0};        // [kg]
    double mass_after_capture_{0.0};         // [kg]

    // Closest approach to the destination, as FLOWN.  The plan predicts one; the
    // flight has its own, and section 17 asks for the difference.  Tracked as a
    // running minimum rather than read off a single instant, because the arrival
    // is a property of the whole approach.
    double closest_distance_{0.0};           // [m]
    time::CoordinateTime closest_at_{};
    bool have_closest_{false};
};

}  // namespace sf::navigation
