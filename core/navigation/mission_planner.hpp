#pragma once

// The ONE way to ask for a transfer to another body.
//
// ---------------------------------------------------------------------------
// Why this file exists (Milestone 6.2, sections 1-5)
//
// Until this milestone there were two planners.  The campaign that got 365
// departure epochs out of 365 called core/navigation/transfer_planner.hpp; the
// scene, when a pilot pressed J, called a second implementation living inside
// the GDExtension -- its own departure search, its own time-of-flight sweep, its
// own Lambert screen, its own B-plane corrector, its own cost rule.  Five hundred
// lines of astrodynamics behind the renderer.
//
// That is not duplication in the tidiness sense.  It means the thing that was
// MEASURED and the thing that is FLOWN were different programs, so every number
// in docs/validation/ described software the player never ran.  A validation
// campaign against a code path nobody executes is not evidence of anything.
//
// So: one authoritative implementation, and this is its front door.  The search,
// the correctors, the flight and the classification all live in transfer_planner;
// this file is the vocabulary a caller uses to ask, and the vocabulary the answer
// comes back in.  The campaign goes through here.  The GDExtension goes through
// here.  tests/scientific/test_planner_equivalence.cpp exists to make sure they
// keep arriving at the same place.
//
// ---------------------------------------------------------------------------
// What a caller is NOT given
//
// No Lambert solution, no Jacobian, no corrector residual, no candidate list, no
// departure velocity vector.  Those are how the answer was reached, and a caller
// that reads them starts depending on them; the next planner would then have to
// reproduce the previous one's internals to avoid breaking the scene.
//
// `MissionPlanResult::diagnostics` is the single deliberate exception, and it is
// named so that its audience is obvious: the campaign tool records forty
// quantities per epoch (Milestone 6.1 section 3) and a CSV of them is the
// evidence the milestone rests on.  The GDExtension must not touch it.
// ---------------------------------------------------------------------------

#include "core/attitude/pointing_controller.hpp"
#include "core/celestial/body_catalog.hpp"
#include "core/celestial/body_id.hpp"
#include "core/celestial/body_orientation.hpp"
#include "core/coordinates/state_vector.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/navigation/b_plane.hpp"
#include "core/navigation/transfer_planner.hpp"
#include "core/navigation/maneuver.hpp"
#include "core/propagation/spacecraft_propagator.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/time/coordinate_time.hpp"
#include "core/time/duration.hpp"
#include "core/trajectory/lambert.hpp"
#include "core/units/angle.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sf::navigation {

// ---------------------------------------------------------------------------
// The orbit that is being asked for (section 12).
//
// Periapsis and apoapsis, not "an altitude": a capture that comes out
// 96 x 103 km and one that comes out 40 x 900 km both have a mean altitude near
// what was asked for, and only one of them is a lunar orbit.
//
// Inclination and RAAN are OPTIONAL and, as of this milestone, are not targeted.
// They are here because section 12 asks the vocabulary to exist before the
// capability does, and because a request that cannot even express "equatorial"
// cannot later be told to achieve it without changing every caller.  What the
// planner does with them today is REPORT what it achieved against them
// (section 13) -- never silently steer towards them, which would change the
// trajectory the 365/365 campaign qualified without anyone asking.
// ---------------------------------------------------------------------------
struct OrbitTarget {
    double periapsis_altitude{100.0e3};   // [m] above the destination's mean radius
    double apoapsis_altitude{100.0e3};    // [m]

    std::optional<units::Angle> inclination{};
    std::optional<units::Angle> raan{};

    // The band the ACHIEVED orbit has to land in for the mission to count as
    // flown.  Wider than the request on purpose: an orbit is the result of a
    // finite burn through a real gravity field, and demanding that it come out
    // exactly 100 x 100 km is demanding that the physics not happen.
    double minimum_periapsis_altitude{80.0e3};    // [m]
    double maximum_apoapsis_altitude{120.0e3};    // [m]
    double maximum_eccentricity{0.01};
};

// When the ship is allowed to leave, and how finely that window is searched.
struct TimeWindow {
    time::Duration span{time::Duration::hours(2.0)};
    int samples{16};
};

// How long it is allowed to fly.  A LIST and not a range, because the grid is
// what the search walks and pretending it is continuous would hide that.
struct DurationRange {
    std::vector<double> days{3.0, 3.25, 3.5, 3.75, 4.0, 4.25, 4.5, 4.75, 5.0, 5.5};

    // Evenly spaced, inclusive of both ends.  A convenience for callers that
    // think in bounds; it produces the same kind of list.
    [[nodiscard]] static DurationRange linear(double from_days, double to_days, int samples);
};

// ---------------------------------------------------------------------------
// The grid a transfer between two particular bodies has to be searched over
// (Milestone 8 rules 37-39).
//
// Not a constant, and not a switch on a body name: it is derived from the two
// bodies' own orbits, so the same code produces three days for the Moon and
// three hundred for Mars, and would produce something sensible for Titan without
// anyone writing Titan down.
//
// ---------------------------------------------------------------------------
// Why the DEPARTURE window stays short even for Mars
//
// The textbook answer is a synodic period -- 780 days for Earth and Mars -- because
// a chemical rocket can only afford the transfer near an opposition.  This ship
// is not a chemical rocket.  Measured on this codebase, departing 2026-01-01,
// which is a thoroughly unfavourable date:
//
//     time of flight     v_inf departure    v_inf arrival     total
//        60 d              61.9 km/s          55.0 km/s      116.9 km/s
//       150 d              33.6               18.5            52.0
//       260 d              34.0               20.6            54.6
//       400 d              23.3               14.5            37.8
//
// against a budget of 26 900 km/s in IMPULSE mode (config/engines/torch-mk3.json).
// Every one of those is affordable by three orders of magnitude.  There is no
// launch window to wait for, so the search does not look for one -- and rule 40
// is explicit that the ship must not be artificially confined to the
// trajectories a chemical stage could fly.
//
// What the window IS, in both geometries, is one revolution of the PARKING
// ORBIT: where the ship is when it leaves decides which way the departure
// hyperbola can point, and that is a question with a 92-minute period.  The
// field stays configurable so that a scenario with a chemical budget can ask
// for a synodic sweep; it would cost a very long parking coast, which is why it
// is not the default.
// ---------------------------------------------------------------------------
struct SearchSpace {
    TimeWindow departure_window{};
    DurationRange time_of_flight{};
    TransferGeometry geometry{TransferGeometry::Local};
};

// Throws std::invalid_argument when the two bodies have no common primary, which
// is a question that cannot be posed rather than a search that finds nothing.
[[nodiscard]] SearchSpace default_search_space(const ephemeris::EphemerisProvider& provider,
                                               celestial::BodyId origin,
                                               celestial::BodyId destination,
                                               time::CoordinateTime epoch);

// What "best" means, as a choice the caller makes rather than a rule buried in
// the cost function (section 8 of the Milestone 6.1 brief, section 14 of 6.2).
enum class OptimizationObjective {
    // The weights the 365/365 campaign was qualified with.  Departure and
    // insertion delta-v at parity, periapsis error priced, and corrector
    // authority priced at twice delta-v because a candidate that needs a large
    // correction is one whose two-body plan described the full model badly.
    Balanced,

    // Total delta-v, with correction effort barely priced.  Measurably worse at
    // handing the corrector something it can invert; kept because "cheapest" is
    // a question people legitimately ask.
    MinimumTotalDeltaV,

    // Injection only.  Useful when the insertion is somebody else's problem --
    // a flyby, an aerocapture, a stage that is discarded.
    MinimumInjectionDeltaV,

    // Prices each day of flight.  The grid still bounds it; this breaks ties
    // towards getting there sooner.
    ShortestTimeOfFlight
};

[[nodiscard]] std::string_view to_string(OptimizationObjective objective);

// ---------------------------------------------------------------------------
// The ship, as the planner needs to know it.
//
// The hull and the thrusters are here and not in a constant because the
// autopilot's entire subject is the LAG, and the lag is a property of the
// inertia and the available torque.  Anyone comparing an autopilot flight with
// an impulsive one has to be able to see which hull was compared.
// ---------------------------------------------------------------------------
struct SpacecraftCapabilities {
    const spacecraft::Spacecraft* vehicle{nullptr};

    // How the burns are delivered.  FiniteBurn is the production answer: the
    // engine runs, the mass varies, guidance is ideal.  Autopilot puts the
    // attitude controller in the loop.  Impulsive has no engine at all and
    // therefore produces NO maneuvers -- it is a reference trajectory, and a
    // result carrying it cannot be used to arm a ship.
    ExecutionModel execution{ExecutionModel::FiniteBurn};

    double hull_mass{1000.0};                        // [kg]
    math::Vec3 hull_size{8.0, 3.0, 3.0};             // [m]
    double rcs_arm{2.0};                             // [m]
    double rcs_mass_flow{2.0e-5};                    // [kg/s] per thruster
    double rcs_exhaust_fraction_c{0.03};
    attitude::PointingGains pointing{};
};

// How hard to search.  Separated from the mission parameters because these are
// the only fields whose right value is "whatever the campaign was qualified
// with"; changing them changes the evidence, not the mission.
struct SearchEffort {
    int screened_candidates{24};
    int flown_candidates{6};
    int b_plane_passes{4};
    std::size_t approach_bracket_samples{1200};

    // Integrator steps before a run is classified TIMEOUT.
    //
    // Counted in STEPS and not in wall seconds, so that the same case classifies
    // the same way on a fast machine and a slow one.
    //
    // TWO numbers, because the three execution models are not remotely the same
    // amount of arithmetic.  Measured on the lunar-intercept scenario, epoch
    // 2026-01-01, the SAME search:
    //
    //     FINITE_BURN      138 050 steps over 435 propagations
    //     AUTOPILOT     42 393 965 steps over 773 propagations      307x
    //
    // Under AUTOPILOT the state carries a quaternion and an angular velocity, the
    // error controller has to resolve both alongside the orbit, and the attitude
    // loop at the qualified bandwidth is far stiffer than the orbit is.  Only 1.8
    // times the propagations and 307 times the steps: it is the step size that
    // collapses, not the number of flights.
    //
    // 2e6 sits 3.4x above the worst of the 365-epoch FINITE_BURN campaign
    // (591 036 steps).  150e6 gives the same 3.5x margin over the autopilot
    // measurement above -- with the caveat, stated because it matters, that the
    // FINITE figure is the worst of 365 samples and the AUTOPILOT figure is ONE.
    // Tighten it when a 365-epoch autopilot campaign exists to tighten it with.
    //
    // One shared number would have to be the larger, and then a FINITE_BURN run
    // that has genuinely gone wrong would grind seventy-five times longer before
    // anyone was told.  A budget that never fires is not a budget.
    //
    // What this costs when it does fire: a healthy autopilot search is about
    // sixteen minutes, so an exhausted one reports TIMEOUT after roughly an hour.
    std::size_t step_budget{2'000'000};               // Impulsive, FiniteBurn
    std::size_t autopilot_step_budget{150'000'000};   // Autopilot

    // And a third, for an interplanetary search, because none of the reasoning
    // above transfers to one.
    //
    // A lunar flight is four days long and a Mars flight is two hundred; a lunar
    // search flies about four hundred of them and an interplanetary one flies a
    // similar number of arcs that are each fifty times longer. Measured on the
    // first Earth-Mars search that reached a capture: 2 314 362 steps for a
    // SINGLE attempt -- the 2 000 000 budget exhausted before the second
    // candidate was reached, and reported as TIMEOUT, which said nothing about
    // what had actually happened.
    //
    // 50e6 is about twenty attempts at that cost. It is a budget and not a
    // target: the search that ships uses a fraction of it, and the number exists
    // so that a run which has genuinely gone wrong is stopped and SAID to have
    // been stopped, rather than grinding.
    std::size_t interplanetary_step_budget{50'000'000};

    // Whichever applies.
    [[nodiscard]] std::size_t budget_for(ExecutionModel execution) const {
        return execution == ExecutionModel::Autopilot ? autopilot_step_budget : step_budget;
    }
    [[nodiscard]] std::size_t budget_for(ExecutionModel execution,
                                         TransferGeometry geometry) const {
        if (execution == ExecutionModel::Autopilot) {
            return autopilot_step_budget;
        }
        return geometry == TransferGeometry::Interplanetary ? interplanetary_step_budget
                                                            : step_budget;
    }

    // How close the flyby has to be aimed, in metres of PERIAPSIS (the corrector
    // converts it to a tolerance on |B| itself).
    double periapsis_tolerance{2.0e3};

    // Which side of the destination the flyby passes.  No natural default; zero
    // is a choice, recorded as one.
    units::Angle b_plane_angle{units::Angle::radians(0.0)};

    // The floor the post-injection conic's perigee must clear, and whether
    // falling below it refuses the candidate or merely makes it expensive.  A
    // penalty by default, measured: the screen reads the UNCORRECTED conic and
    // the corrector routinely lifts a perigee that started below the surface.
    double minimum_departure_perigee_altitude{120.0e3};   // [m]
    bool refuse_departure_conic_below_floor{false};

    // Where the capture burn sits relative to periapsis.  Zero is centred on it.
    double capture_burn_offset_seconds{0.0};

    // Where the pointing error has to fall below for a slew to count as settled.
    units::Angle settling_threshold{units::Angle::degrees(0.5)};

    // Passed straight through to the search (rules 48 and 120). Null by default,
    // which is a search nobody can stop and that reports nothing -- correct for a
    // campaign tool and wrong for a cockpit.
    std::function<bool()> cancelled{};
    std::function<void(const SearchProgress&)> on_progress{};
};

// ---------------------------------------------------------------------------
// The request (section 4).
// ---------------------------------------------------------------------------
struct MissionRequest {
    celestial::BodyId origin{celestial::bodies::earth};
    celestial::BodyId destination{celestial::bodies::moon};

    OrbitTarget target_orbit{};
    TimeWindow departure_window{};
    DurationRange time_of_flight{};
    OptimizationObjective objective{OptimizationObjective::Balanced};
    SpacecraftCapabilities spacecraft{};
    SearchEffort effort{};

    // Whether the result carries a sampled arc.  Off by default: a 365-epoch
    // campaign does not need 365 dense trajectories, and the scene asks for one.
    bool want_trajectory{false};
    int trajectory_samples{512};

    // Fly ONE named geometry instead of searching for it.  This is how the three
    // execution models are compared: let each run its own search and they differ
    // in departure point, flight time and arrival geometry, and the comparison
    // stops being a comparison.
    TransferConfig::PinnedDeparture pinned{};
};

// ---------------------------------------------------------------------------
// Where the ship is when the question is asked.
//
// `vehicle` is relative to `request.origin`, because that is the only frame in
// which a parking orbit is a description rather than a coincidence.
// ---------------------------------------------------------------------------
struct SimulationState {
    const ephemeris::EphemerisProvider* provider{nullptr};

    // Oblateness needs a body-fixed frame, which is a different question from
    // where the body is.  Separate pointer, and `j2_bodies` is refused when it is
    // null rather than silently dropped.
    const celestial::BodyOrientationProvider* orientation{nullptr};

    celestial::BodyCatalog catalog{};
    std::vector<celestial::BodyId> j2_bodies{};

    coordinates::StateVector vehicle{};   // relative to the request's origin
    time::CoordinateTime epoch{};
    propagation::IntegratorConfig integrator{};
};

// ---------------------------------------------------------------------------
// The answer.
// ---------------------------------------------------------------------------
enum class MissionPlanStatus {
    Planned,
    NoSolution,                // nothing in the searched grid survives
    InsufficientPropellant,    // a transfer exists and the ship cannot pay (section 21)
    ExecutionFailed,           // the trajectory is right and the flight of it was not
    Invalid,                   // the request is not a question (no vehicle, empty grid)

    // Whoever asked stopped asking (rule 120).  Not a failure: nothing was found
    // wanting, the search simply did not finish.
    Cancelled
};

[[nodiscard]] std::string_view to_string(MissionPlanStatus status);

struct FailureReason {
    TransferFailure code{TransferFailure::NoFeasibleTrajectory};
    std::string detail;

    [[nodiscard]] std::string_view name() const { return to_string(code); }
};

// Everything the cockpit must be able to show BEFORE execution (section 15),
// plus what section 13 asks to be reported rather than corrected.
struct MissionMetrics {
    // Which transfer this is.  Carried on the metrics and not only on the
    // request, because a result gets handed around on its own -- to the cockpit,
    // to a CSV, to the execution monitor -- and a set of orbital elements with no
    // statement of what they are about is a set of numbers.
    celestial::BodyId origin{celestial::bodies::earth};
    celestial::BodyId destination{celestial::bodies::moon};

    time::CoordinateTime departure{};
    time::CoordinateTime arrival{};
    time::CoordinateTime capture_ignition{};
    time::CoordinateTime capture_cutoff{};
    double time_of_flight_s{0.0};

    trajectory::TransferDirection branch{trajectory::TransferDirection::Prograde};
    units::Angle transfer_angle{units::Angle::radians(0.0)};

    double injection_delta_v{0.0};   // [m/s]
    double midcourse_delta_v{0.0};   // [m/s] -- the corrector's authority, priced and reported
    double capture_delta_v{0.0};     // [m/s]
    double total_delta_v{0.0};       // [m/s]

    double injection_duration_s{0.0};
    double capture_duration_s{0.0};

    double v_infinity{0.0};                    // [m/s] at the destination
    BPlaneTarget b_plane_target{};             // [m]
    double predicted_flyby_periapsis{0.0};     // [m] from the destination's centre

    // What was ASKED for, carried alongside what was predicted.
    //
    // Both, and not just the prediction: a flight that comes out 4 km from what
    // the planner predicted has gone exactly as planned, and a planner that
    // predicted 300 km when 100 was requested has not -- and those are different
    // failures with different fixes.  Keeping only one of the two numbers makes
    // them indistinguishable downstream.
    double requested_periapsis_altitude{0.0};  // [m]
    double requested_apoapsis_altitude{0.0};   // [m]

    double predicted_periapsis_altitude{0.0};  // [m]
    double predicted_apoapsis_altitude{0.0};   // [m]
    double predicted_eccentricity{0.0};
    units::Angle predicted_inclination{units::Angle::radians(0.0)};
    units::Angle predicted_raan{units::Angle::radians(0.0)};

    double propellant_required{0.0};    // [kg]
    double propellant_remaining{0.0};   // [kg] after the whole mission
    double delta_v_available{0.0};      // [m/s] at departure
    double mass_at_departure{0.0};      // [kg] total, at the injection's ignition

    // Autopilot only; zero for the other two models, where the guidance law IS
    // the thrust direction and there is nothing to lag.
    units::Angle pointing_error_mean{units::Angle::radians(0.0)};
    units::Angle pointing_error_peak{units::Angle::radians(0.0)};
    double rcs_propellant{0.0};      // [kg]
    double rcs_duty_cycle{0.0};      // [0,1]
    double torque_saturation{0.0};   // [0,1]
    double settling_s{0.0};          // [s]
    double angular_rate_peak{0.0};   // [rad/s]
};

// The arc, sampled.  Positions only, and relative to both ends of the transfer,
// because that is what a display needs and a velocity would invite somebody to
// integrate it.
struct TrajectoryPrediction {
    struct Sample {
        time::CoordinateTime time{};
        math::Vec3 from_origin{};        // [m]
        math::Vec3 from_destination{};   // [m]
    };
    std::vector<Sample> samples;

    [[nodiscard]] bool empty() const noexcept { return samples.empty(); }
};

// One of the geometries the search actually flew (section 13).
//
// These are not extra work: the planner attempts several candidates in cost
// order and each attempt already knows the orbit it would arrive in.  Reporting
// them is how "predicted inclination and RAAN" gets answered without pretending
// that the one the planner chose was the only one available.
struct MissionAlternative {
    std::string label;                 // e.g. "prograde/4.25d/coast=0.750h"
    bool feasible{false};
    time::CoordinateTime departure{};
    double time_of_flight_s{0.0};
    trajectory::TransferDirection branch{trajectory::TransferDirection::Prograde};

    // How far into the departure window this geometry left, in seconds.
    //
    // Carried so that a caller can ASK FOR THIS ONE: the three numbers here --
    // coast, time of flight, branch -- are exactly TransferConfig::PinnedDeparture,
    // and pinning them replans this single candidate instead of searching again.
    // Without it the alternatives are a report; with it they are a choice.
    double departure_coast_s{0.0};

    double injection_delta_v{0.0};
    double capture_delta_v{0.0};
    double total_delta_v{0.0};
    double cost{0.0};

    double predicted_periapsis_altitude{0.0};
    double predicted_apoapsis_altitude{0.0};
    double predicted_eccentricity{0.0};
    units::Angle predicted_inclination{units::Angle::radians(0.0)};
    units::Angle predicted_raan{units::Angle::radians(0.0)};

    TransferFailure failure{TransferFailure::None};
};

struct MissionPlanResult {
    MissionPlanStatus status{MissionPlanStatus::NoSolution};

    // The burns, ready to arm.  Empty under ExecutionModel::Impulsive, and empty
    // whenever the status is not Planned: a failed plan must never leave a burn
    // behind for somebody to fly by accident.
    ManeuverPlan maneuvers{};

    TrajectoryPrediction trajectory{};
    MissionMetrics metrics{};
    std::optional<FailureReason> failure{};
    std::vector<MissionAlternative> alternatives{};

    // NOT for the game.  See the note at the top of this file.
    TransferRecord diagnostics{};

    [[nodiscard]] bool ok() const noexcept { return status == MissionPlanStatus::Planned; }
};

// Searches, corrects, flies, classifies, and hands back a plan.
//
// Never throws for a physical refusal: an unflyable geometry, an unaffordable
// burn and an orbit that came out wrong are RESULTS with a reason on them.
// Throws std::invalid_argument only for a request that is not a question.
[[nodiscard]] MissionPlanResult plan_mission(const SimulationState& state,
                                                    const MissionRequest& request);

// The translation, exposed.
//
// Not so that callers can go round the front door -- so that
// tests/scientific/test_planner_equivalence.cpp can prove there is only one
// door.  A test that compares two pipelines by re-deriving the configuration
// itself proves that the test author can add, not that the code agrees.
[[nodiscard]] TransferConfig config_for(const MissionRequest& request);
[[nodiscard]] TransferInputs inputs_for(const SimulationState& state,
                                        const MissionRequest& request);

}  // namespace sf::navigation
