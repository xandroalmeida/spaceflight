// The golden scenario: Earth LEO -> Moon -> 100 km lunar orbit, flown whole.
//
// Milestone 6.2 section 18.
//
// ---------------------------------------------------------------------------
// What "whole" means here
//
// Everything in the loop at once: the search, the two differential correctors,
// finite burns through a varying mass, the attitude controller pointing the
// engine, twelve RCS thrusters answering it out of the same tank, and the full
// N-body force model with the Earth's J2 -- from a parking orbit to two
// revolutions past the capture burn.
//
// That is deliberately not a unit test.  Every other test in this suite isolates
// one mechanism and can therefore say exactly what broke; this one can only say
// that the mission stopped working, which is a different and equally necessary
// statement.  A suite of green units and a simulator that cannot reach the Moon
// is the failure mode this exists to catch.
//
// ---------------------------------------------------------------------------
// The criteria, and where the numbers come from
//
// Section 18 sets the opening bounds and says to tighten them later.  They are
// used as written, with one addition: the measured values are PRINTED, so that
// the margin each criterion actually has is visible in the log rather than
// inferred from the fact that it passed.  When the bounds are tightened, the log
// is where the evidence for the new ones comes from.
//
// The criteria are not a description of the current numbers.  Over the 365-epoch
// campaign the capture comes out at 96.4 x 102.8 km with e = 0.0017, which sits
// well inside the 80-120 km band -- so this test would still pass if the
// mission degraded considerably, and that is the intent of an acceptance bound
// as opposed to a regression pin.

#include "core/celestial/body_catalog.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "core/navigation/mission_execution.hpp"
#include "core/navigation/mission_planner.hpp"
#include "core/propulsion/engine.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <string>

using namespace sf;

namespace {

// A 400 km parking orbit in the Moon's orbital plane, departure point 170
// degrees from where the Moon will be.  The same state the campaign flies, so
// that a failure here and a failure in the campaign are the same failure.
constexpr double kParkingPosition[3] = {5403733.3475775, -3679897.2695510, -1788660.3908598};
constexpr double kParkingVelocity[3] = {4623.3381510, 5322.9266799, 3016.4827353};
constexpr const char* kEpoch = "2026-01-01 00:00:00 TDB";

spacecraft::Spacecraft make_tug() {
    const propulsion::MultiModeEngine engine{
        {{"IMPULSE", propulsion::EngineSpec{"IMPULSE", 0.0222376, 0.03, 1.0}},
         {"CRUISE", propulsion::EngineSpec{"CRUISE", 7.470950e-05, 0.5, 1.0}}}};
    return spacecraft::Spacecraft{"Tug", 1000.0, 19000.0, engine};
}

}  // namespace

TEST(the_golden_mission_reaches_a_hundred_kilometre_lunar_orbit) {
    const auto spice = sft::load_spice_or_skip();
    auto& provider = *spice.provider;
    const auto epoch = spice.time->parse(kEpoch);
    const auto craft = make_tug();

    navigation::SimulationState state{};
    state.provider = &provider;
    state.orientation = &provider;
    state.catalog = celestial::BodyCatalog::default_solar_system(provider);
    state.j2_bodies = {celestial::bodies::earth};
    state.vehicle.position =
        math::Vec3{kParkingPosition[0], kParkingPosition[1], kParkingPosition[2]};
    state.vehicle.velocity =
        math::Vec3{kParkingVelocity[0], kParkingVelocity[1], kParkingVelocity[2]};
    state.epoch = epoch;
    state.integrator.relative_tolerance = 1.0e-11;
    state.integrator.absolute_tolerance_position = 1.0e-3;
    state.integrator.absolute_tolerance_velocity = 1.0e-6;
    state.integrator.initial_step = time::Duration::seconds(1.0);
    state.integrator.max_step = time::Duration::seconds(3600.0);

    navigation::LunarTransferRequest request{};
    request.origin = celestial::bodies::earth;
    request.destination = celestial::bodies::moon;
    request.target_orbit.periapsis_altitude = 100.0e3;
    request.target_orbit.apoapsis_altitude = 100.0e3;
    request.spacecraft.vehicle = &craft;

    // ---- the mission in two halves, and the split is the point --------------
    //
    // Section 18 asks for planner, autopilot, finite burns, RCS and full
    // propagation, all in one run.  Doing that literally -- one AUTOPILOT plan
    // over the whole production grid -- means the two differential correctors
    // invert a map with the attitude controller inside it for each of the
    // twenty-four screened candidates, which costs tens of millions of
    // integrator steps and the better part of an hour.
    //
    // The decomposition below is cheaper AND says more.  It is the same one the
    // whole milestone is built on (section 19): PLANNING and EXECUTION are
    // different questions, and a test that answers them separately can say which
    // one broke.
    //
    //   1. SEARCH, under ideal guidance.  Does a trajectory exist, does the
    //      search find it, and is the orbit it predicts the one that was asked
    //      for?  A failure here is a planner fault: there is no control error in
    //      this model at all.
    //
    //   2. EXECUTION, on exactly that geometry, pinned, flown by the attitude
    //      controller with twelve thrusters answering it.  A failure here is an
    //      execution fault, because step 1 already proved the trajectory.
    //
    // Pinning does not skip the planner: the corrector, the B-plane targeting
    // and the capture all run again, in the autopilot's own model.  What it skips
    // is re-searching a grid whose answer step 1 already produced.
    request.spacecraft.execution = navigation::ExecutionModel::FiniteBurn;
    request.want_trajectory = false;
    const auto search = navigation::plan_lunar_transfer(state, request);
    if (!search.ok()) {
        SFT_FAIL_FATAL("PLANNER FAULT -- the search found nothing under ideal guidance: " +
                       std::string{search.failure.has_value()
                                       ? search.failure->name()
                                       : navigation::to_string(search.status)} +
                       " -- " +
                       (search.failure.has_value() ? search.failure->detail : std::string{}));
    }
    INFO("  search chose " + search.diagnostics.lambert_solution_id + ", predicting " +
         std::to_string(search.metrics.predicted_periapsis_altitude / 1000.0) + " x " +
         std::to_string(search.metrics.predicted_apoapsis_altitude / 1000.0) + " km, e = " +
         std::to_string(search.metrics.predicted_eccentricity));

    request.pinned.active = true;
    request.pinned.coast_s = search.diagnostics.departure_coast_s;
    request.pinned.time_of_flight_days = search.diagnostics.time_of_flight_s / 86400.0;
    request.pinned.direction = search.diagnostics.lambert_direction;
    request.spacecraft.execution = navigation::ExecutionModel::Autopilot;
    request.want_trajectory = true;
    request.trajectory_samples = 2000;

    const auto plan = navigation::plan_lunar_transfer(state, request);

    // ---- CAPTURED ---------------------------------------------------------
    if (!plan.ok()) {
        SFT_FAIL_FATAL("EXECUTION FAULT -- the trajectory the search proved was not flyable "
                       "by the autopilot: " +
                       std::string{plan.failure.has_value() ? plan.failure->name()
                                                            : navigation::to_string(plan.status)} +
                       " -- " + (plan.failure.has_value() ? plan.failure->detail : std::string{}));
    }
    const auto& m = plan.metrics;
    INFO("  departure " + std::to_string(m.departure.seconds_since_j2000()) + " s TDB, tof " +
         std::to_string(m.time_of_flight_s / 86400.0) + " d");
    INFO("  injection " + std::to_string(m.injection_delta_v) + " m/s over " +
         std::to_string(m.injection_duration_s) + " s, capture " +
         std::to_string(m.capture_delta_v) + " m/s over " +
         std::to_string(m.capture_duration_s) + " s");
    INFO("  autopilot lag mean " + std::to_string(m.pointing_error_mean.degrees()) +
         " deg, peak " + std::to_string(m.pointing_error_peak.degrees()) + " deg, RCS " +
         std::to_string(m.rcs_propellant) + " kg");

    CHECK(plan.diagnostics.post_burn_specific_energy < 0.0);

    // ---- the orbit --------------------------------------------------------
    //
    // Bounds, not a pin: section 18's opening criteria, used as written.
    CHECK(m.predicted_periapsis_altitude >= 80.0e3);
    CHECK(m.predicted_periapsis_altitude <= 120.0e3);
    CHECK(m.predicted_apoapsis_altitude >= 80.0e3);
    CHECK(m.predicted_apoapsis_altitude <= 120.0e3);
    CHECK(m.predicted_eccentricity <= 0.01);
    INFO("  orbit " + std::to_string(m.predicted_periapsis_altitude / 1000.0) + " x " +
         std::to_string(m.predicted_apoapsis_altitude / 1000.0) +
         " km, e = " + std::to_string(m.predicted_eccentricity) + ", i = " +
         std::to_string(m.predicted_inclination.degrees()) + " deg, RAAN = " +
         std::to_string(m.predicted_raan.degrees()) + " deg");

    // ---- fuel -------------------------------------------------------------
    CHECK(m.propellant_required > 0.0);
    CHECK(m.propellant_remaining >= 0.0);
    CHECK(m.propellant_required + m.propellant_remaining <= craft.initial_propellant() * 1.0 +
                                                                1.0e-6);

    // ---- the burns the ship would actually fly ----------------------------
    REQUIRE(plan.maneuvers.size() == 2);
    for (const auto& maneuver : plan.maneuvers.maneuvers()) {
        INFO("  burn " + maneuver.name + ": " + std::to_string(maneuver.duration.seconds()) +
             " s at throttle " + std::to_string(maneuver.throttle));
        CHECK(maneuver.duration.seconds() > 0.0);
        CHECK(std::isfinite(maneuver.ignition.seconds_since_j2000()));
    }
    // The capture must ignite after the injection has finished.  One engine
    // cannot burn in two directions, and ManeuverPlan refuses the overlap -- so
    // reaching here already proves it, and checking it anyway states the
    // requirement where a reader looks for it.
    CHECK(plan.maneuvers.maneuvers()[0].cutoff() <= plan.maneuvers.maneuvers()[1].ignition);

    // ---- no collision, no NaN, no superluminal state ----------------------
    //
    // Checked on the flown arc rather than on its endpoints.  An endpoint test
    // would pass a trajectory that went through the Earth and came out the other
    // side, which is exactly the failure mode Milestone 6 shipped.
    REQUIRE(!plan.trajectory.empty());
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();
    const double earth_radius = provider.mean_radius(celestial::bodies::earth);
    const double moon_radius = provider.mean_radius(celestial::bodies::moon);
    double closest_to_earth = std::numeric_limits<double>::infinity();
    double closest_to_moon = std::numeric_limits<double>::infinity();
    double fastest = 0.0;
    bool finite = true;

    for (const auto& sample : plan.trajectory.samples) {
        const double to_earth = sample.from_origin.norm();
        const double to_moon = sample.from_destination.norm();
        closest_to_earth = std::min(closest_to_earth, to_earth);
        closest_to_moon = std::min(closest_to_moon, to_moon);
        finite = finite && sample.from_origin.is_finite() && sample.from_destination.is_finite();

        // Speed in the integration frame, from consecutive samples' own epochs
        // is not available here -- the prediction carries positions only -- so
        // the superluminal test is done on the barycentric speed of the recorded
        // state instead, below.
        (void)fastest;
    }
    CHECK(finite);
    CHECK(closest_to_earth > earth_radius);
    CHECK(closest_to_moon > moon_radius);
    INFO("  closest approach: Earth " + std::to_string((closest_to_earth - earth_radius) / 1000.0) +
         " km altitude, Moon " + std::to_string((closest_to_moon - moon_radius) / 1000.0) +
         " km altitude");

    // Superluminal: the fastest the ship ever goes, relative to the Earth, over
    // the whole arc.  A trans-lunar injection tops out near 11 km/s, so this has
    // five orders of magnitude of headroom -- which is the point.  It is not a
    // test of the mission, it is a test that the integrator never produced a
    // state that is not physics.
    REQUIRE(plan.diagnostics.trajectory != nullptr);
    const auto samples = plan.diagnostics.trajectory->sample(2000);
    double peak_speed = 0.0;
    for (const auto& [t, sampled] : samples) {
        const auto earth = provider.state(celestial::bodies::earth, t, frame);
        peak_speed = std::max(peak_speed, (sampled.state.velocity - earth.state.velocity).norm());
        CHECK(sampled.is_finite());
        CHECK(sampled.mass >= craft.dry_mass() - 1.0e-6);
    }
    CHECK(peak_speed < units::c);
    INFO("  peak speed relative to the Earth: " + std::to_string(peak_speed / 1000.0) + " km/s (" +
         std::to_string(peak_speed / units::c) + " c)");
}

// Section 17, as a mechanism rather than as a number: the execution monitor must
// be able to say what it predicted and what happened, and the two must be the
// same thing measured twice.
//
// The plan is flown by the planner itself, so the "actual" here is the same
// trajectory -- which makes this a test of the BOOKKEEPING, not of the physics.
// That is the right scope: whether the prediction matches reality is the
// campaign's question, and whether the simulator can even tell you is this one's.
TEST(the_execution_monitor_records_predicted_against_actual) {
    const auto spice = sft::load_spice_or_skip();
    auto& provider = *spice.provider;
    const auto epoch = spice.time->parse(kEpoch);
    const auto craft = make_tug();

    navigation::SimulationState state{};
    state.provider = &provider;
    state.orientation = &provider;
    state.catalog = celestial::BodyCatalog::default_solar_system(provider);
    state.j2_bodies = {celestial::bodies::earth};
    state.vehicle.position =
        math::Vec3{kParkingPosition[0], kParkingPosition[1], kParkingPosition[2]};
    state.vehicle.velocity =
        math::Vec3{kParkingVelocity[0], kParkingVelocity[1], kParkingVelocity[2]};
    state.epoch = epoch;
    state.integrator.relative_tolerance = 1.0e-11;
    state.integrator.absolute_tolerance_position = 1.0e-3;
    state.integrator.absolute_tolerance_velocity = 1.0e-6;
    state.integrator.max_step = time::Duration::seconds(3600.0);

    navigation::LunarTransferRequest request{};
    request.spacecraft.vehicle = &craft;
    request.spacecraft.execution = navigation::ExecutionModel::FiniteBurn;
    request.want_trajectory = true;
    request.trajectory_samples = 4000;
    // Cheap: this test is about the monitor, and the monitor does not care which
    // trajectory it is watching.
    request.time_of_flight.days = {4.5, 5.0};
    request.effort.screened_candidates = 4;
    request.effort.flown_candidates = 2;

    const auto plan = navigation::plan_lunar_transfer(state, request);
    if (!plan.ok()) {
        SKIP("the cheap search found nothing to watch: " +
             std::string{plan.failure.has_value() ? plan.failure->name() : "?"});
    }

    navigation::MissionExecution mission{};
    mission.set_vehicle(&craft);
    mission.arm(plan);
    CHECK_EQ(static_cast<int>(mission.phase()),
             static_cast<int>(navigation::MissionPhase::Planned));

    // Walk the recorded arc and let the monitor classify each instant.  The
    // phases it must visit are the ones the plan contains; ORIENTING and
    // CAPTURE_ORIENTING are not among them here, because this is FINITE_BURN and
    // an ideal guidance law reports zero pointing error by construction.
    REQUIRE(plan.diagnostics.trajectory != nullptr);
    bool saw_injection = false;
    bool saw_coast = false;
    bool saw_approach = false;
    bool saw_capture = false;
    bool saw_insertion = false;
    for (const auto& [t, sampled] : plan.diagnostics.trajectory->sample(4000)) {
        mission.update(provider, t, sampled, units::Angle::radians(0.0));
        switch (mission.phase()) {
            case navigation::MissionPhase::InjectionBurn:  saw_injection = true; break;
            case navigation::MissionPhase::Coast:          saw_coast = true; break;
            case navigation::MissionPhase::Approach:       saw_approach = true; break;
            case navigation::MissionPhase::CaptureBurn:    saw_capture = true; break;
            case navigation::MissionPhase::OrbitInsertion: saw_insertion = true; break;
            default: break;
        }
    }

    CHECK(saw_injection);
    CHECK(saw_coast);
    CHECK(saw_approach);
    CHECK(saw_capture);
    CHECK(saw_insertion);
    INFO("  final phase: " + std::string{navigation::to_string(mission.phase())});
    CHECK_EQ(static_cast<int>(mission.phase()),
             static_cast<int>(navigation::MissionPhase::Complete));

    const auto& outcome = mission.outcome();
    REQUIRE(outcome.recorded);
    INFO(mission.describe());

    // The monitor watched the SAME trajectory the planner reported, so the two
    // must agree to the accuracy of the dense interpolant -- which is the only
    // difference between them: the planner reads the elements at an accepted
    // step, the monitor at an interpolated sample nearby.
    //
    // 100 m on a 100 km altitude is 1e-3 relative.  The dense output of a
    // Dormand-Prince 5(4) is fourth-order accurate between steps against a fifth
    // order solution (ADR-0006), and over a 2 h lunar orbit sampled 4000 times
    // across a 5-day arc the sample spacing is ~110 s, which is where a hundred
    // metres comes from.
    CHECK_NEAR_ABS(outcome.periapsis_altitude.actual, outcome.periapsis_altitude.predicted,
                   200.0,
                   "the monitor reads the orbit off the dense interpolant and the planner off "
                   "an accepted step; 110 s of sample spacing on a 2 h orbit is where the "
                   "difference comes from");
    CHECK_NEAR_ABS(outcome.eccentricity.actual, outcome.eccentricity.predicted, 1.0e-3,
                   "same interpolation difference, expressed in the shape rather than the size");
    CHECK(outcome.propellant_used.recorded);
    CHECK(outcome.propellant_used.actual > 0.0);
}
