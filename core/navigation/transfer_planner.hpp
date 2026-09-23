#pragma once

// A transfer from a parking orbit about one body to a closed orbit about
// another: searched, flown, measured and CLASSIFIED.
//
// Nothing in this file is lunar.  The centre and the target are arguments and
// the arithmetic is the same for Mars or for Titan.
//
// Until Milestone 8 it was NAMED for the Moon, because the Moon was the only
// case a campaign had qualified and a generic name would have claimed a
// generality nobody had measured.  What the rename records is that the claim is
// now being made and tested: the same pipeline plans Earth->Moon and
// Earth->Mars, and the ONE thing that differs between them is where the
// candidate geometry comes from -- see TransferGeometry below.
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
#include "core/celestial/solar_system.hpp"
#include "core/coordinates/state_vector.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/math/vec3.hpp"
#include "core/navigation/b_plane.hpp"
#include "core/navigation/maneuver.hpp"
#include "core/navigation/targeting.hpp"
#include "core/propagation/dense_output.hpp"
#include "core/propagation/spacecraft_propagator.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/time/coordinate_time.hpp"
#include "core/time/duration.hpp"
#include "core/trajectory/departure_hyperbola.hpp"
#include "core/trajectory/lambert.hpp"
#include "core/units/angle.hpp"

#include <functional>
#include <optional>
#include <memory>
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
    TargetImpact,                // the flown arc entered the target
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
    TargetOrbitNotAchieved,

    // Whoever started the search asked for it to stop (rule 120).  A RESULT and
    // not an error: nothing went wrong, and reporting it as NO_FEASIBLE_TRAJECTORY
    // would say the grid was searched and found wanting when it was not searched.
    Cancelled,

    // -- added by Milestone 8 -----------------------------------------------

    // The two bodies have no common primary in the directory, so there is no
    // two-body problem to generate candidates from.  Distinct from
    // NO_FEASIBLE_TRAJECTORY: nothing was searched, because the question could
    // not be posed (rule 104).
    NoTransferGeometry,

    // An interplanetary departure needs a hyperbola through the ship's actual
    // point in the parking orbit whose asymptote is the one the transfer wants,
    // and at some points in the orbit that hyperbola does not exist -- the ship
    // is standing along the direction it has to leave in.  A property of the
    // departure POINT, not of the transfer, and the search simply moves on.
    DepartureGeometryUnavailable
};

[[nodiscard]] std::string_view to_string(TransferFailure reason);

// ---------------------------------------------------------------------------
// Which two-body problem generates the candidates (Milestone 8 rules 32-36).
//
// This is the ONE place the two kinds of mission differ, and it is a statement
// about the HIERARCHY of the two bodies rather than about their names -- it is
// read off core/celestial/solar_system.hpp, so adding Titan adds no branch here
// (rules 78, 79).
//
// Everything downstream is shared: the same differential corrector, the same
// B-plane, the same capture burn, the same classification.  That is the claim
// Milestone 8 exists to make, and test_planner_equivalence.cpp exists to keep
// honest.
// ---------------------------------------------------------------------------
enum class TransferGeometry {
    // The destination orbits the ORIGIN.  Earth to the Moon: the whole transfer
    // happens inside the origin's gravity well, so Lambert is solved about the
    // origin, from the parking orbit straight to where the destination will be.
    //
    // This is the geometry the 365/365 campaign qualified, and its arithmetic is
    // untouched by Milestone 8.
    Local,

    // Origin and destination orbit a COMMON PRIMARY that is neither of them.
    // Earth to Mars: over a hundred days the Sun dominates by four orders of
    // magnitude, and a Lambert solved about the Earth would describe a
    // trajectory that does not exist.
    //
    // So Lambert is solved about the primary, between the two BODIES, and its
    // departure velocity is a heliocentric velocity rather than a burn.  What
    // turns it into a burn is core/trajectory/departure_hyperbola.hpp: the
    // excess velocity the transfer needs, converted into the state the ship has
    // to be in at its actual point in the parking orbit.
    Interplanetary
};

[[nodiscard]] std::string_view to_string(TransferGeometry geometry);

// Which of the two applies, from the body hierarchy.  Nullopt when the pair has
// no common primary in the directory, which is a refusal and not a default.
[[nodiscard]] std::optional<TransferGeometry> geometry_for(celestial::BodyId origin,
                                                           celestial::BodyId destination);

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
// What a candidate is priced ON.  A struct and not seven positional doubles
// because section 14 of the Milestone 6.2 brief asks for the cost function to be
// EXTENSIBLE -- insertion delta-v, fuel, periapsis, inclination, orbital plane --
// and a call with seven bare numbers in it is where the wrong one gets passed.
struct TransferCostTerms {
    double departure_delta_v{0.0};      // [m/s]
    double insertion_delta_v{0.0};      // [m/s]
    double periapsis_error_m{0.0};      // [m], signed; the weight takes |.|
    double correction_delta_v{0.0};     // [m/s] of corrector authority
    double conic_deficit_m{0.0};        // [m] the departure conic falls below the floor
    double time_of_flight_days{0.0};    // [d]
    double inclination_error_rad{0.0};  // [rad] |achieved - requested|, 0 when unrequested
};

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

    // -- the terms section 14 asks the architecture to be ready for -----------
    //
    // ZERO, and deliberately so.  The 365/365 campaign was qualified with a cost
    // function that does not contain them, and turning one on changes which
    // trajectory the planner picks -- which is a new campaign, not a tweak.  They
    // exist so that adding the capability later is a weight and not a rewrite,
    // and so that the shape of the eventual answer is visible now.
    //
    // `inclination_error` in particular: section 13 is explicit that the lunar
    // inclinations the campaign produces (0.1 to 30.2 degrees) are NOT to be
    // quietly corrected, because inclination is not yet part of the
    // specification.  Reporting it is this milestone's job; steering to it is
    // not.
    double time_of_flight{0.0};           // per day of flight
    double inclination_error{0.0};        // per radian of |i - requested|

    // The corrector's own sensitivity, priced.  A candidate that needs a large
    // correction is one whose two-body plan was a poor description of the full
    // model, and that is a property worth avoiding even when it converges.
    [[nodiscard]] double evaluate(const TransferCostTerms& terms) const;
};

// Where the search has got to, for a user interface that must not freeze
// (rule 48).  Counts, not trajectories: a progress report that carried a plan
// would be a second place plans come from.
struct SearchProgress {
    std::string stage;              // "screening", "ranking", "flying"
    int candidates_considered{0};   // geometries the two-body screen looked at
    int candidates_screened{0};     // ... that survived it
    int candidates_flown{0};
    int candidates_succeeded{0};
    double best_total_delta_v{0.0}; // [m/s]; 0 until something succeeds
    std::size_t integrator_steps{0};
};

// ---------------------------------------------------------------------------
// The search (sections 6 and 7).
// ---------------------------------------------------------------------------
struct TransferRecord;

struct TransferConfig {
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

    // The floor the INTERMEDIATE capture ellipse's periapsis has to clear, when
    // the capture needs two burns (see CaptureSequence in the .cpp).
    //
    // Between the two burns the ship is on an ellipse whose periapsis is lower
    // than the orbit that was asked for, because the first burn is long and the
    // engine is lit on the way in. It then coasts out to apoapsis, and this is
    // the line below which that coast is a landing.
    //
    // 50 km, and it is a SAFETY MARGIN rather than a physical effect: there is no
    // atmosphere in this simulator and rule 56 forbids aerobraking, so nothing in
    // the dynamics would stop a 10 km pass. What stops it is this number, stated
    // here where it can be argued with.
    double minimum_capture_periapsis_altitude{50.0e3};   // [m]

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

    // How many times the flyby AIM may be moved to anticipate the drop a long
    // capture burn puts into the periapsis (see the note above
    // TransferSession::attempt in the .cpp).
    //
    // Three, and the first pass is the only one a lunar transfer ever uses: the
    // loop exits the moment the orbit lands in the requested band, and an 83 s
    // burn puts it there. Each extra pass is a full departure correction, so
    // this is the most expensive number in the file when it is actually needed.
    int aim_passes{3};

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

    // Where the pointing error has to fall below for the slew to count as
    // finished, for TransferRecord::capture_settling_s.  Not a success criterion
    // -- the specification of docs/validation/autopilot-hardening.md is -- just
    // the line the stopwatch is read against.
    units::Angle settling_threshold{units::Angle::degrees(0.5)};

    // Recorded arcs cost nothing in force evaluations (ADR-0006) but they do
    // cost memory, and a 365-epoch campaign does not need them.
    bool keep_trajectory{false};

    // ---- progress and cancellation (rules 48, 51, 120) ---------------------
    //
    // Both are CALLBACKS and not state, because the planner must not know
    // whether it is running on a worker thread, in a campaign loop or in a test.
    // Whoever started the search decides what stopping means.
    //
    // `cancelled` is asked between candidates and between aim passes -- never in
    // the middle of one. A search that abandons a flight half way through leaves
    // the step counters describing work that produced nothing, and the point of
    // those counters is that they describe the search.
    std::function<bool()> cancelled{};
    std::function<void(const SearchProgress&)> on_progress{};

    // Called once per candidate that is actually flown, with that candidate's
    // own record, before the best of them is chosen.
    //
    // This is how section 13's "report the alternatives" is answered without a
    // second search: the attempts already happen, and each one already knows the
    // inclination and RAAN it would arrive in.  Throwing them away and then
    // recomputing them for a list would be inventing work.
    std::function<void(const TransferRecord&)> on_attempt{};
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

    // -- the capture corrector (Milestone 8) --------------------------------
    //
    // All zero, and `converged` false, whenever it did not have to run -- which
    // is every lunar capture. A non-zero iteration count is the signal that the
    // impulsive plan was not an impulse and the burn had to be solved for.
    // The second capture burn (rule 11's CIRCULARIZATION).  Zero whenever the
    // sequence is one burn long, which is every lunar transfer.
    double circularisation_delta_v{0.0};      // [m/s]
    time::CoordinateTime circularisation_start{};
    double circularisation_duration_s{0.0};

    // The periapsis of the ellipse between the two burns. Zero when the capture
    // was one burn, which is every lunar transfer.
    double intermediate_periapsis_altitude{0.0};   // [m]

    // How many aim passes this attempt used, and where the flyby was finally
    // aimed. One and `flyby_altitude` for every lunar transfer.
    int aim_passes{0};
    double aim_altitude{0.0};   // [m]

    int capture_corrector_iterations{0};
    int capture_corrector_evaluations{0};
    bool capture_corrector_converged{false};
    std::string capture_corrector_message;
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

    // AUTOPILOT only, and everything section 10 warns about optimising away.
    //
    // A gain chosen on eccentricity alone will happily buy it with propellant and
    // with an actuator that is hard against its stop for the whole burn, and
    // neither shows up in an orbit element.  These are the other columns of the
    // sweep in docs/validation/autopilot-hardening.md.
    double capture_rcs_propellant{0.0};      // [kg] burnt by the thrusters in the burn
    double capture_rcs_duty_cycle{0.0};      // [0,1] mean throttle over all thrusters
    double capture_torque_saturation{0.0};   // [0,1] fraction of the burn hard against the stop
    // The LAST instant the pointing error was above `settling_threshold`,
    // measured from the guidance command changing at the injection cutoff --
    // where the nose is still on the injection direction and the target has just
    // become retrograde about the destination.
    //
    // For a controller that settles this is the settling time.  For one that
    // does not it comes out equal to the whole coast, and that reading is the
    // useful one rather than a defect in the metric: at omega_n = 0.05 the
    // steady-state lag is 1.65 degrees, so a 0.5 degree threshold is never
    // crossed downwards and "settling time = 402 610 s" is the honest way to say
    // the controller never got inside specification at all.  A metric that
    // returned zero there, or gave up and reported nothing, would hide it.
    double capture_settling_s{0.0};          // [s]
    // Peak |omega| during the slew and the burn, which is what a crewed hull
    // would feel (section 10).
    double capture_angular_rate_peak{0.0};   // [rad/s]

    double energy_before_burn{0.0};       // [J/kg] specific, about the target
    double post_burn_specific_energy{0.0};
    double post_burn_eccentricity{0.0};
    double post_burn_periapsis_altitude{0.0};
    double post_burn_apoapsis_altitude{0.0};
    double post_burn_inclination_rad{0.0};
    // Section 13: reported, never silently corrected.  The campaign's final
    // orbits run from 0.1 to 30.2 degrees of inclination and that is not a bug --
    // inclination is not part of the specification yet.  What WOULD be a bug is
    // the planner knowing the number and not saying it.
    double post_burn_raan_rad{0.0};

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

    // -- what the scene has to fly (Milestone 6.2 sections 1 and 5) ---------
    //
    // The plan the winning flight ACTUALLY flew, not a reconstruction of it.
    // Before this existed the only way for the game to fly the validated
    // trajectory was to re-derive the burns from the record, and a
    // re-derivation is a second implementation of exactly the kind this
    // milestone exists to delete.
    //
    // Empty for ExecutionModel::Impulsive, where there is no engine and
    // therefore no maneuver: an impulsive result is a reference trajectory, not
    // something a ship can be armed with.
    ManeuverPlan flight_plan{};
    double mass_at_departure{0.0};   // [kg] total, at the injection's ignition
    double mass_at_capture{0.0};     // [kg] total, at the capture burn's ignition

    // The flown arc, when TransferConfig::keep_trajectory asked for it.
    // shared_ptr because a TransferRecord is copied freely (the campaign holds a
    // vector of them) and a dense arc over five days is megabytes.
    std::shared_ptr<const propagation::Trajectory> trajectory{};

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

    // The ship's mass at `epoch`, or 0 for a full tank (the craft's initial
    // mass).  Not a detail: the burns are flown for a planned DURATION, so a
    // ship lighter than the plan assumes over-performs every one of them, and a
    // lunar transfer carries no midcourse to absorb it.  0.015 kg short of full
    // -- one second of the main engine -- moves a 100 km capture to 79 km.
    double mass{0.0};

    propagation::IntegratorConfig integrator{};
};

// Searches, chooses, flies and classifies.  Never throws for a physical
// refusal: an unflyable geometry is a RESULT with a reason on it.  Throws
// std::invalid_argument only for inputs that are not a question -- a null
// provider, an empty time-of-flight grid.
[[nodiscard]] TransferRecord plan_and_fly(const TransferInputs& inputs,
                                          const TransferConfig& config);

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
                                                      const TransferConfig& config);

}  // namespace sf::navigation
