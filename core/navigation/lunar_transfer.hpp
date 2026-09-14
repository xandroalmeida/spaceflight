#pragma once

// A transfer from a parking orbit about one body to a closed orbit about
// another: searched, flown, measured and CLASSIFIED.
//
// Nothing in this file is lunar.  The centre and the target are arguments and
// the arithmetic is the same for Mars or for Titan.  It is named for the Moon
// because the Moon is the only case that has been qualified against a campaign
// (docs/validation/lunar-navigation-hardening.md), and a generic name would
// claim a generality nobody has measured.
//
// ---------------------------------------------------------------------------
// Why this exists next to trajectory_planner.hpp and targeting.hpp
//
// Those two are primitives: Lambert's problem, a differential corrector, the
// rocket equation.  They were enough to fly ONE trans-lunar injection and they
// were not enough to fly a hundred.  The Milestone 6 campaign got 41 captures
// out of 100 departure epochs, and every single failure had the same shape --
// the solver reported that it had converged.
//
// It had.  It had converged on a trajectory that passes through the Earth.
//
// The B-plane corrector's probe flights run with `stop_inside_body` off, on
// purpose: a corrector needs a smooth map and a trajectory that clips the target
// is a useful intermediate iterate.  But the map it was inverting had no
// knowledge that the departure conic it was shaping dived below the Earth's
// surface six minutes after ignition, so it happily drove the aim point to
// 0.04 km of the requested lunar periapsis on an arc the ship could not fly.
// Fifty-two of the hundred cases ended with
//
//     propagation stopped: trajectory entered a body -- trajectory entered Earth
//
// after a B-plane stage that reported `converged`.  The correlation with the
// outcome was perfect: 52 entered the Earth, 0 of those 52 were captured, and
// none of the 41 successes entered anything.
//
// The root cause is not the corrector.  It is that the previous pipeline had no
// SEARCH: the departure point was wherever the parking orbit happened to be at
// the epoch, and the time of flight was a constant 4.5 days.  With both fixed,
// the transfer angle is whatever the calendar says, and above about 215 degrees
// the Lambert branch that connects those two points needs a departure velocity
// 4.6 to 13 km/s away from the parking orbit's, pointed in a direction whose
// resulting conic re-enters the Earth.  There is no correcting that.  There is
// only leaving at a different time, or flying for a different length of time --
// which is what a mission does, and what this file does.
// ---------------------------------------------------------------------------
//
// The pipeline, and it is deliberately four stages and not one
// (section 5 of the Milestone 6.1 brief):
//
//     analytic candidate        Lambert, two-body, no propagation
//            |
//     planned trajectory        the full model, impulsive burns
//            |
//     flown trajectory          the full model, finite burns
//            |
//     autopilot execution       finite burns under closed-loop pointing
//
// Each stage is a separate ExecutionModel and each produces its own record, so
// that an error of PLANNING and an error of EXECUTION can never be confused for
// one another.

#include "core/attitude/pointing_controller.hpp"
#include "core/celestial/body_catalog.hpp"
#include "core/celestial/body_orientation.hpp"
#include "core/celestial/body_id.hpp"
#include "core/coordinates/state_vector.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/math/vec3.hpp"
#include "core/navigation/b_plane.hpp"
#include "core/navigation/targeting.hpp"
#include "core/propagation/spacecraft_propagator.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/time/coordinate_time.hpp"
#include "core/time/duration.hpp"
#include "core/trajectory/lambert.hpp"
#include "core/units/angle.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sf::navigation {

// ---------------------------------------------------------------------------
// The taxonomy (section 4 of the brief).
//
// Nothing may fail as "solver failed".  Every refusal, every divergence and
// every unflyable geometry gets a name, and the campaign counts them.
//
// The order matters in one respect only: the pipeline reports the FIRST reason
// that applies, walking from the cheapest test to the most expensive, so a case
// that has no Lambert solution is never also reported as a capture failure.
// ---------------------------------------------------------------------------
enum class TransferFailure {
    None,

    // -- search ------------------------------------------------------------
    NoLambertSolution,          // the iteration does not reach the requested tof
    BadLambertBranch,           // a solution exists and is not flyable (see below)
    NoFeasibleTrajectory,       // the whole grid was searched and nothing survived

    // -- departure ---------------------------------------------------------
    DepartureCorrectorDiverged,   // the Jacobian went singular or non-finite
    DepartureCorrectorStagnated,  // no step along the Newton direction helps

    // -- arrival -----------------------------------------------------------
    InvalidBPlane,              // the approach is not hyperbolic about the target
    BPlaneCorrectorDiverged,
    LunarImpact,                // the flown arc entered the target
    PeriapsisTooHigh,
    PeriapsisTooLow,

    // -- capture -----------------------------------------------------------
    CaptureBurnTooEarly,
    CaptureBurnTooLate,
    InsufficientCaptureDeltaV,  // the ship cannot pay for the burn
    PostBurnHyperbolic,         // the burn happened and did not close the orbit

    // -- the run itself ----------------------------------------------------
    NumericalFailure,           // NaN, minimum step, invariant violation
    Timeout,                    // the step budget was exhausted

    // -- added to the brief's minimum, because they dominated the campaign ---

    // The post-injection conic about the CENTRAL body passes below its surface.
    // This is the mechanism behind 46 of the 59 Milestone 6 failures and it has
    // to have its own name: calling it BadLambertBranch would be true and would
    // hide that the branch is fine and the DEPARTURE GEOMETRY is not.
    DepartureConicHitsCentralBody,

    // The transfer is flyable and the ship cannot pay for it.
    InsufficientDepartureDeltaV,

    // Captured, but not into the orbit that was asked for (section 13).
    TargetOrbitNotAchieved
};

[[nodiscard]] std::string_view to_string(TransferFailure reason);

// ---------------------------------------------------------------------------
// Planning versus execution (sections 5 and 11).
// ---------------------------------------------------------------------------
enum class ExecutionModel {
    // Both burns applied as instantaneous changes of velocity, in the full
    // force model.  This is the trajectory with NO control error in it, and it
    // is the reference every other model is measured against: if a case fails
    // here, the planner is wrong; if it fails only downstream, the planner is
    // right and the engine is the problem.
    Impulsive,

    // Both burns as the engine would run them: finite duration, mass varying,
    // thrust along a fixed inertial direction for the injection and along the
    // instantaneous retrograde for the insertion.
    FiniteBurn,

    // The same finite burns, but pointed by the attitude controller rather than
    // by an ideal guidance law, so the pointing lag is in the loop.
    Autopilot
};

[[nodiscard]] std::string_view to_string(ExecutionModel model);

// ---------------------------------------------------------------------------
// What "captured" means (section 13).
//
// `specific_energy < 0` is not success.  It is satisfied by an orbit with a
// periapsis inside the Moon and an apoapsis past the Earth, and the Milestone 6
// campaign's 41 "captures" were only saved from that by luck.  The mission asks
// for an ORBIT, so the orbit is specified.
// ---------------------------------------------------------------------------
struct TargetOrbit {
    double mean_altitude{100.0e3};        // [m] above the target's mean radius
    double max_eccentricity{0.01};
    double min_periapsis_altitude{80.0e3};  // [m]
    double max_apoapsis_altitude{120.0e3};  // [m]

    // Every condition, evaluated against a measured orbit.  Returns None when
    // the orbit qualifies and the first reason it does not otherwise.
    [[nodiscard]] TransferFailure check(double periapsis_altitude, double apoapsis_altitude,
                                        double eccentricity) const;
};

// ---------------------------------------------------------------------------
// The cost function (section 8).
//
// Explicit, weighted, and NOT "whichever is cheapest".  Selecting on departure
// delta-v alone is measurably the wrong rule: it reaches for the longest time of
// flight in the grid, because a longer transfer approaches the minimum-energy
// ellipse -- and a longer transfer is also far more sensitive to the departure
// velocity, which is the quantity the corrector then has to invert.  Measured on
// this codebase before the weights existed: the cheapest candidate saved 150 m/s
// that a ship with kilometres per second of budget does not need, and left the
// corrector stalled at 53 000 km.
//
// The hard constraints are not in here.  A candidate that violates one is not
// expensive, it is refused (section 8: "hard constraints").
// ---------------------------------------------------------------------------
struct TransferCost {
    double departure_delta_v{1.0};        // per m/s
    double insertion_delta_v{1.0};        // per m/s
    double periapsis_error{0.05};         // per m of |r_p - wanted|
    double correction_magnitude{2.0};     // per m/s of corrector authority used

    // How much it costs, per metre, that the two-body departure conic's perigee
    // falls below the floor.
    //
    // A PENALTY and not a refusal, and that distinction was measured rather than
    // chosen.  The first version refused such candidates outright, which is what
    // section 8 seems to ask for -- and in the Milestone 6 configuration, where
    // the departure point and the time of flight are both fixed and only four
    // geometries exist, it refused 84 epochs out of 100 as
    // NO_FEASIBLE_TRAJECTORY.  With the same screen demoted to a penalty, 66 of
    // those 100 succeed.
    //
    // The reason is that the screen reads the UNCORRECTED conic.  The corrector
    // then moves the departure velocity by 152 to 3910 m/s (median 445 over 365
    // epochs), and a correction that size routinely lifts a perigee that started
    // below the surface.  A screen that refuses on the two-body conic is
    // therefore refusing flyable transfers.
    //
    // The hard constraint still exists and is still enforced -- on the FLOWN
    // arc, where `stop_inside_body` reports it and the case is classified
    // DEPARTURE_CONIC_HITS_CENTRAL_BODY.  That is where "no collision" belongs,
    // because that is where a collision is a fact rather than an extrapolation.
    // At 0.01 per metre a 1000 km deficit costs 10 000, which outranks every
    // flyable candidate and still lets an unflyable one be attempted when there
    // is nothing else.
    double departure_conic_deficit{0.01};

    // The corrector's own sensitivity, priced.  A candidate that needs a large
    // correction is one whose two-body plan was a poor description of the full
    // model, and that is a property worth avoiding even when it converges.
    [[nodiscard]] double evaluate(double departure_dv, double insertion_dv,
                                  double periapsis_error_m, double correction_dv,
                                  double conic_deficit_m = 0.0) const;
};

// ---------------------------------------------------------------------------
// The search (sections 6 and 7).
// ---------------------------------------------------------------------------
struct LunarTransferConfig {
    // -- when to leave -----------------------------------------------------
    //
    // One revolution of the parking orbit, sampled.  Most points in a parking
    // orbit are a bad place to leave from, and which ones are good is decided by
    // where the target WILL BE, not by anything about the orbit.
    time::Duration departure_window{time::Duration::hours(2.0)};
    int departure_samples{16};

    // -- how long to fly ---------------------------------------------------
    //
    // The time of flight is not a constant (section 7).  It is the strongest
    // knob in the whole search: the Moon moves 13 degrees a day, so two days of
    // flight time moves the arrival geometry by 26 degrees while a two-hour
    // departure window moves it by almost nothing.
    std::vector<double> time_of_flight_days{3.0,  3.25, 3.5,  3.75, 4.0,
                                            4.25, 4.5,  4.75, 5.0,  5.5};

    // -- which branch ------------------------------------------------------
    //
    // Short way and long way, which for a zero-revolution Lambert are exactly
    // the prograde and retrograde solutions: the pair covers transfer angles
    // theta and 2*pi - theta.  Both are tried and the chosen one is recorded,
    // because "the first solution the solver returned" is not a reason.
    bool try_prograde{true};
    bool try_retrograde{true};

    // Lambert is degenerate at a transfer angle of exactly pi -- every plane
    // containing both points is a solution -- and ill-conditioned near it.  The
    // refusal band is narrow on purpose: it refuses what is undefined and not
    // what is merely awkward.
    units::Angle minimum_transfer_angle{units::Angle::degrees(5.0)};
    units::Angle degenerate_half_width{units::Angle::degrees(2.0)};

    // -- hard constraints (section 8) --------------------------------------
    //
    // The one that matters most.  The post-injection conic about the central
    // body must clear its surface, or the ship flies into the planet it just
    // left.  Checked ANALYTICALLY, on the two-body conic, before any propagation
    // -- which is both cheap and the only place the constraint is exact.
    double minimum_departure_perigee_altitude{120.0e3};   // [m]

    // Whether falling below that floor REFUSES the candidate or merely makes it
    // expensive.  False -- a penalty -- because the floor is read off the
    // uncorrected two-body conic and the corrector routinely lifts a perigee that
    // started below it; see TransferCost::departure_conic_deficit.  Set it true
    // to reproduce the stricter reading, which the ablation in
    // docs/validation/lunar-navigation-hardening.md section 5 measures.
    bool refuse_departure_conic_below_floor{false};

    // Where the flyby is aimed, and how close it has to land.
    double flyby_altitude{100.0e3};       // [m] above the target's mean radius
    double periapsis_tolerance{2.0e3};    // [m]
    units::Angle b_plane_angle{units::Angle::radians(0.0)};

    // Allowed band for the ACHIEVED periapsis, wider than the tolerance: outside
    // it the case is PeriapsisTooHigh or PeriapsisTooLow rather than a near miss.
    double minimum_periapsis_altitude{20.0e3};    // [m]
    double maximum_periapsis_altitude{400.0e3};   // [m]

    TargetOrbit target_orbit{};
    TransferCost cost{};

    // How finely the arc is scanned to BRACKET the closest approach, before the
    // golden section refines it.  This is the campaign's dominant cost: the
    // corrector evaluates the approach a few hundred times per epoch and each
    // evaluation is one ephemeris call per sample.  400 samples over a five-day
    // arc is 1080 s apart, which is far finer than the basin the minimum sits in
    // -- the flyby's distance-versus-time curve is unimodal over a whole day --
    // and the refinement that follows does not care how coarse the bracket was.
    // Measured: 2000 samples cost 30 s per epoch, 400 cost 7 s, and the answers
    // agree to the last digit the golden section resolves.
    std::size_t approach_bracket_samples{1200};

    // -- the correctors ----------------------------------------------------
    //
    // The two stages have DIFFERENT jobs and therefore different tolerances, and
    // the defaults below say which is which rather than sharing a number.
    //
    // Stage 1 has to put the spacecraft somewhere a B-plane can be read from.
    // That is all.  Asking it for ten kilometres -- the tolerance the CLI used --
    // asks it for something that is not even well posed: its target is the
    // position at the NOMINAL ARRIVAL EPOCH, and closest approach does not happen
    // then, so the residual it is driving to zero has a floor of a few thousand
    // kilometres that no number of iterations removes.  Measured: 25 iterations,
    // 192 propagations, stalled at 4560 km, every single time.  The work was not
    // wasted on a hard problem; it was wasted on the wrong problem.
    //
    // Stage 2 does the precision, and its tolerance is not set here at all: it is
    // derived per pass from `periapsis_tolerance` through dr_p/db, because what
    // the mission cares about is the periapsis and what the corrector can aim at
    // is |B|.
    TargetingConfig departure_targeting{[] {
        TargetingConfig config{};
        config.position_tolerance = 1.0e7;   // [m] -- "near the Moon", not "at it"
        config.max_iterations = 12;
        return config;
    }()};
    TargetingConfig b_plane_targeting{};
    int b_plane_passes{4};

    // How many candidates survive the two-body sort, and how many of those are
    // then corrected and flown all the way to a captured orbit.
    //
    // The one flight per screened candidate is not a second selection rule, and
    // it took a campaign to get that right.  The first version ranked candidates
    // by how close that flight came to the target and attempted the best two --
    // and under the IMPULSIVE model, where nothing punishes a huge burn, that
    // chose departures costing 18 km/s because they happened to fly nearest.  The
    // measured median departure was 10 952 m/s against 3 167 for the same epochs
    // under FINITE_BURN.  Flying nearest is not a reason to prefer a trajectory;
    // it is a PREDICTION of how much correction the trajectory will need, and the
    // cost function already prices correction.  So the miss is converted into the
    // delta-v it implies and added to the cost, and there is one ranking.
    int screened_candidates{24};
    int flown_candidates{4};

    // How far the arrival moves per m/s of departure velocity, for a trans-lunar
    // transfer.  Measured on this codebase and recorded in targeting.hpp: about
    // 1e6 m per m/s, which is why a corrector here needs a 1e-3 m/s difference
    // step.  It is the conversion that turns "this candidate flew 40 000 km wide"
    // into "this candidate will cost about 40 m/s to correct".
    double arrival_lever_arm{1.0e6};   // [m per m/s]

    // -- the budget (section: TIMEOUT must be a classification) -------------
    //
    // Counted in integrator STEPS and not in wall seconds, so that the same run
    // classifies the same way on a fast machine and a slow one.  The Milestone 6
    // campaign's six "timeouts" were all one mechanism -- a probe trajectory
    // passing 600 m from the Moon's centre, where the point-mass acceleration is
    // 10^11 m/s^2 and the error controller grinds the step to the floor -- and
    // reporting that as "took more than 30 seconds" said nothing about it.
    std::size_t step_budget{2'000'000};

    ExecutionModel execution{ExecutionModel::FiniteBurn};

    // ---- pinning the search (sections 5 and 11) --------------------------
    //
    // The three execution models are supposed to differ in ONE thing: how the
    // burns are delivered.  Let each of them run its own search and they differ
    // in everything -- a different departure point, a different time of flight, a
    // different arrival v_infinity -- and the comparison stops being a comparison.
    // Measured on 2026-01-05: IMPULSIVE chose a geometry needing 816 m/s of
    // capture and FINITE_BURN one needing 874, and the 7% between them says
    // nothing at all about finite burns.
    //
    // With a departure pinned, every model corrects and flies the SAME geometry,
    // each in its own model, and what is left between them is execution.
    struct PinnedDeparture {
        bool active{false};
        double coast_s{0.0};
        double time_of_flight_days{0.0};
        trajectory::TransferDirection direction{trajectory::TransferDirection::Prograde};
    };
    PinnedDeparture pinned{};

    // The ship the autopilot flies, and the numbers are the scene's
    // (godot/gdextension/src/simulation_node.cpp) rather than new ones: a 1000 kg
    // box 8 x 3 x 3 m with twelve RCS thrusters in six couples on a 2 m arm.
    // They are here as configuration and not as constants because the autopilot's
    // whole subject is the LAG, and the lag is a property of the inertia and the
    // available torque -- so anyone comparing IMPULSIVE with AUTOPILOT has to be
    // able to see what hull they compared.
    double autopilot_hull_mass{1000.0};                    // [kg]
    math::Vec3 autopilot_hull_size{8.0, 3.0, 3.0};         // [m]
    double autopilot_rcs_arm{2.0};                         // [m]
    double autopilot_rcs_mass_flow{2.0e-5};                // [kg/s] per thruster
    double autopilot_rcs_exhaust_fraction_c{0.03};
    attitude::PointingGains autopilot_gains{};

    // Where the capture burn sits relative to periapsis (section 12).  Zero is
    // centred on it; the sweep is what the campaign tool varies.
    double capture_burn_offset_seconds{0.0};

    // Recorded arcs cost nothing in force evaluations (ADR-0006) but they do
    // cost memory, and a 365-epoch campaign does not need them.
    bool keep_trajectory{false};
};

// ---------------------------------------------------------------------------
// Everything the brief's section 3 asks to be recorded, for every epoch.
// ---------------------------------------------------------------------------
struct TransferRecord {
    // -- identity ----------------------------------------------------------
    time::CoordinateTime requested_epoch{};
    time::CoordinateTime departure_epoch{};
    time::CoordinateTime arrival_epoch{};
    double time_of_flight_s{0.0};
    double departure_coast_s{0.0};        // how far into the window the ship left

    // -- states ------------------------------------------------------------
    coordinates::StateVector spacecraft_initial_state{};   // relative to the centre
    coordinates::StateVector center_state{};               // centre, in the integration frame
    coordinates::StateVector target_departure_state{};     // target at departure
    coordinates::StateVector target_arrival_state{};       // target at arrival

    // -- Lambert -----------------------------------------------------------
    std::string lambert_solution_id;      // e.g. "prograde/4.25d/coast=0.750h"
    double initial_flown_miss{0.0};       // [m] before any correction
    trajectory::TransferDirection lambert_direction{trajectory::TransferDirection::Prograde};
    double transfer_angle_rad{0.0};
    math::Vec3 lambert_departure_velocity{};
    math::Vec3 lambert_arrival_velocity{};
    double lambert_delta_v{0.0};

    // The analytic screen that Milestone 6 did not have.
    double departure_perigee_radius{0.0};   // [m] of the post-injection conic
    double departure_conic_eccentricity{0.0};

    // -- departure ---------------------------------------------------------
    double departure_delta_v{0.0};        // [m/s] as corrected and flown
    math::Vec3 departure_velocity{};
    int position_corrector_iterations{0};
    int position_corrector_evaluations{0};
    double position_corrector_residual{0.0};   // [m]
    bool position_corrector_converged{false};
    std::string position_corrector_message;

    // -- B-plane -----------------------------------------------------------
    BPlaneTarget bplane_target{};
    double bplane_actual_t{0.0};
    double bplane_actual_r{0.0};
    double bplane_error{0.0};             // [m]
    int bplane_passes{0};
    int bplane_iterations{0};
    bool bplane_converged{false};
    std::string bplane_message;
    double correction_magnitude{0.0};     // [m/s] total corrector authority used

    // -- arrival -----------------------------------------------------------
    time::CoordinateTime closest_approach_epoch{};
    math::Vec3 target_relative_position{};
    math::Vec3 target_relative_velocity{};
    double v_infinity{0.0};
    double target_periapsis{0.0};         // [m] what the flyby was aimed at
    double predicted_periapsis{0.0};      // [m] from the B-plane, before the burn
    double actual_periapsis{0.0};         // [m] measured on the flown arc
    double periapsis_error{0.0};          // [m] actual - target, signed
    double periapsis_velocity{0.0};       // [m/s]

    // -- capture -----------------------------------------------------------
    double required_capture_delta_v{0.0};
    double available_capture_delta_v{0.0};
    time::CoordinateTime burn_start{};
    double burn_duration_s{0.0};
    time::CoordinateTime burn_end{};
    double burn_offset_from_periapsis_s{0.0};
    double burn_midpoint_radius{0.0};     // [m]  section 12
    double burn_midpoint_speed{0.0};      // [m/s]

    // AUTOPILOT only: how far the nose was from the guidance law's answer while
    // the capture burn ran, sampled at every accepted integrator step inside the
    // burn.  Zero for IMPULSIVE and FINITE_BURN, where the guidance law IS the
    // thrust direction and there is nothing to lag.
    //
    // Mean and peak, because they answer different questions: the mean sets the
    // cosine loss on the delta-v, the peak sets how far off the plane the thrust
    // pushed at its worst.
    double capture_pointing_error_mean_rad{0.0};
    double capture_pointing_error_peak_rad{0.0};

    double energy_before_burn{0.0};       // [J/kg] specific, about the target
    double post_burn_specific_energy{0.0};
    double post_burn_eccentricity{0.0};
    double post_burn_periapsis_altitude{0.0};
    double post_burn_apoapsis_altitude{0.0};
    double post_burn_inclination_rad{0.0};

    // -- outcome -----------------------------------------------------------
    bool success{false};
    TransferFailure failure{TransferFailure::NoFeasibleTrajectory};
    std::string detail;                   // free text, never the classification

    ExecutionModel execution{ExecutionModel::FiniteBurn};

    // ---- pinning the search (sections 5 and 11) --------------------------
    //
    // The three execution models are supposed to differ in ONE thing: how the
    // burns are delivered.  Let each of them run its own search and they differ
    // in everything -- a different departure point, a different time of flight, a
    // different arrival v_infinity -- and the comparison stops being a comparison.
    // Measured on 2026-01-05: IMPULSIVE chose a geometry needing 816 m/s of
    // capture and FINITE_BURN one needing 874, and the 7% between them says
    // nothing at all about finite burns.
    //
    // With a departure pinned, every model corrects and flies the SAME geometry,
    // each in its own model, and what is left between them is execution.
    struct PinnedDeparture {
        bool active{false};
        double coast_s{0.0};
        double time_of_flight_days{0.0};
        trajectory::TransferDirection direction{trajectory::TransferDirection::Prograde};
    };
    PinnedDeparture pinned{};

    // The ship the autopilot flies, and the numbers are the scene's
    // (godot/gdextension/src/simulation_node.cpp) rather than new ones: a 1000 kg
    // box 8 x 3 x 3 m with twelve RCS thrusters in six couples on a 2 m arm.
    // They are here as configuration and not as constants because the autopilot's
    // whole subject is the LAG, and the lag is a property of the inertia and the
    // available torque -- so anyone comparing IMPULSIVE with AUTOPILOT has to be
    // able to see what hull they compared.
    double autopilot_hull_mass{1000.0};                    // [kg]
    math::Vec3 autopilot_hull_size{8.0, 3.0, 3.0};         // [m]
    double autopilot_rcs_arm{2.0};                         // [m]
    double autopilot_rcs_mass_flow{2.0e-5};                // [kg/s] per thruster
    double autopilot_rcs_exhaust_fraction_c{0.03};
    attitude::PointingGains autopilot_gains{};
    double cost{0.0};
    double propellant_used{0.0};
    double propellant_left{0.0};

    // -- the search that produced it ---------------------------------------
    int candidates_considered{0};
    int candidates_feasible{0};
    int candidates_flown{0};
    int rejected_no_lambert{0};
    int rejected_transfer_angle{0};
    int rejected_departure_conic{0};
    int rejected_delta_v{0};

    std::size_t propagations{0};      // how many trajectories the search flew
    std::size_t integrator_steps{0};
    double wall_time_seconds{0.0};

    [[nodiscard]] std::string describe() const;

    // The CSV header and row of docs/validation/lunar-navigation-campaign-v2.csv.
    // Here and not in the tool, so that the two can never drift apart.
    [[nodiscard]] static std::string csv_header();
    [[nodiscard]] std::string csv_row() const;
};

// ---------------------------------------------------------------------------
struct TransferInputs {
    const ephemeris::EphemerisProvider* provider{nullptr};
    const spacecraft::Spacecraft* craft{nullptr};
    celestial::BodyCatalog catalog{};

    // Oblateness needs a body-fixed frame, which is a different question from
    // where the body is, and a different interface.  The SPICE provider answers
    // both, but a test provider need not, so the two are separate pointers and
    // `j2_bodies` is refused when this is null rather than silently ignored.
    const celestial::BodyOrientationProvider* orientation{nullptr};
    std::vector<celestial::BodyId> j2_bodies{};

    celestial::BodyId center{celestial::bodies::earth};
    celestial::BodyId target{celestial::bodies::moon};

    // The parking orbit, relative to `center`, at `epoch`.
    coordinates::StateVector parking{};
    time::CoordinateTime epoch{};

    propagation::IntegratorConfig integrator{};
};

// Searches, chooses, flies and classifies.  Never throws for a physical
// refusal: an unflyable geometry is a RESULT with a reason on it.  Throws
// std::invalid_argument only for inputs that are not a question -- a null
// provider, an empty time-of-flight grid.
[[nodiscard]] TransferRecord plan_and_fly(const TransferInputs& inputs,
                                          const LunarTransferConfig& config);

// ---------------------------------------------------------------------------
// The departure x time-of-flight map (section 7).
//
// One row per grid cell, classified.  Cheap: everything up to `Flyby` is
// answered by two-body arithmetic, and only the cells that survive the hard
// constraints are propagated.
// ---------------------------------------------------------------------------
enum class GridClass {
    NoSolution,                 // Lambert does not reach this time of flight
    DegenerateGeometry,         // transfer angle in the refusal band
    DepartureConicHitsBody,     // the post-injection conic re-enters the centre
    TooExpensive,               // the ship cannot pay
    Feasible                    // survives every hard constraint
};

[[nodiscard]] std::string_view to_string(GridClass value);

struct GridCell {
    double departure_coast_s{0.0};
    double time_of_flight_days{0.0};
    trajectory::TransferDirection direction{trajectory::TransferDirection::Prograde};
    GridClass classification{GridClass::NoSolution};
    double transfer_angle_deg{0.0};
    double lambert_delta_v{0.0};
    double departure_perigee_altitude{0.0};   // [m], negative means below the surface
    double v_infinity_estimate{0.0};
};

// The whole grid, classified, with no propagation at all.  This is the map the
// brief asks for, and its point is that it explains the campaign: the cells that
// Milestone 6 flew are the ones the calendar handed it, and most of them are
// DepartureConicHitsBody.
[[nodiscard]] std::vector<GridCell> map_transfer_grid(const TransferInputs& inputs,
                                                      const LunarTransferConfig& config);

}  // namespace sf::navigation
