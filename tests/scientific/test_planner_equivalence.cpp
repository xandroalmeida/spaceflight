// One planner, two callers, the same answer -- Milestone 6.2 section 6.
//
// ---------------------------------------------------------------------------
// What this test is for
//
// Milestone 6.1 measured a 365-epoch campaign against
// core/navigation/transfer_planner.hpp and got 365 captures.  The game, when a
// pilot pressed J, ran a completely different program: its own departure scan,
// its own Lambert screen, its own two-stage corrector, its own cost rule, all of
// it inside the GDExtension.  The campaign therefore certified software nobody
// played.
//
// Milestone 6.2 deleted the second planner.  This test exists so that it stays
// deleted, and it checks three different things because the failure can arrive
// three different ways.
//
//   1. THE QUESTION.  The bridge's translation, field by field, against the
//      request the campaign tool builds.  This is the cheap, exhaustive one: it
//      notices a drift in a tolerance or a weight immediately, and it notices it
//      even for a configuration nobody flew today.
//
//   2. THE ANSWER.  The same state, epoch, ship and destination flown end to
//      end through both callers, with the eight quantities section 6 names
//      compared at zero tolerance.
//
//   3. THE SHAPE.  The bridge's source, read, and required to contain no
//      astrodynamics at all.
//
// The bridge compiles into a test binary because its header has no Godot in it,
// which was a deliberate choice and is exactly what makes any of this checkable:
// this is the code the engine calls, not a re-implementation of it that happens
// to agree.
//
// ---------------------------------------------------------------------------
// Why the tolerances are ZERO
//
// These are not two methods that ought to agree.  They are one function called
// twice with arguments that must be equal, so the results must be bit-identical
// -- the planner is deterministic (no clock, no random, no threading inside one
// call).  A tolerance here would be a place for a real divergence to hide: if
// the bridge ever starts adjusting a tolerance, a weight, or a grid "slightly",
// an epsilon would absorb it and the test would keep passing while the two paths
// drifted apart again.

#include "core/celestial/body_catalog.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "core/navigation/mission_planner.hpp"
#include "core/propulsion/engine.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "godot/gdextension/src/mission_planner.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/source_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <algorithm>
#include <string>

using namespace sf;

namespace {

// The Milestone 6.1 campaign scenario, in code: a 400 km parking orbit in the
// Moon's orbital plane with the departure point 170 degrees from where the Moon
// will be.  Copied from tests/scenarios/lunar-intercept.json rather than loaded,
// so that this test does not depend on a tool's file parser to say something
// about a planner.
constexpr double kParkingPosition[3] = {5403733.3475775, -3679897.2695510, -1788660.3908598};
constexpr double kParkingVelocity[3] = {4623.3381510, 5322.9266799, 3016.4827353};
constexpr const char* kEpoch = "2026-01-01 00:00:00 TDB";

spacecraft::Spacecraft make_tug() {
    const propulsion::MultiModeEngine engine{
        {{"IMPULSE", propulsion::EngineSpec{"IMPULSE", 0.0222376, 0.03, 1.0}},
         {"CRUISE", propulsion::EngineSpec{"CRUISE", 7.470950e-05, 0.5, 1.0}}}};
    return spacecraft::Spacecraft{"Tug", 1000.0, 19000.0, engine};
}

propagation::IntegratorConfig make_integrator() {
    propagation::IntegratorConfig config{};
    config.relative_tolerance = 1.0e-11;
    config.absolute_tolerance_position = 1.0e-3;
    config.absolute_tolerance_velocity = 1.0e-6;
    config.initial_step = time::Duration::seconds(1.0);
    config.max_step = time::Duration::seconds(3600.0);
    return config;
}

// The request the CAMPAIGN builds, written the way tools/lunar-campaign builds
// one: origin, destination, the orbit that is wanted, the departure window, the
// ship.  Everything else is left at the type's defaults, which is the whole
// point -- those defaults are the qualified configuration and both callers get
// them by not overriding them.
navigation::MissionRequest campaign_request(const spacecraft::Spacecraft& craft,
                                                  navigation::ExecutionModel execution) {
    navigation::MissionRequest request{};
    request.origin = celestial::bodies::earth;
    request.destination = celestial::bodies::moon;
    request.target_orbit.periapsis_altitude = 100.0e3;
    request.target_orbit.apoapsis_altitude = 100.0e3;
    request.target_orbit.minimum_periapsis_altitude = 80.0e3;
    request.target_orbit.maximum_apoapsis_altitude = 120.0e3;
    request.departure_window.span = time::Duration::hours(2.0);
    request.departure_window.samples = 16;
    request.spacecraft.vehicle = &craft;
    request.spacecraft.execution = execution;
    return request;
}

// The same mission, expressed the way the SCENE expresses it.  Note what it does
// not say: no time of flight, no Lambert branch, no aim point, no tolerance, no
// cost weight.  The cockpit has no vocabulary for any of those.
spaceflight_godot::SceneTransferRequest scene_request(
    const ephemeris::SpiceEphemerisProvider& provider, const celestial::BodyCatalog& catalog,
    const spacecraft::Spacecraft& craft, time::CoordinateTime epoch,
    navigation::ExecutionModel execution) {
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();
    const auto earth = provider.state(celestial::bodies::earth, epoch, frame);

    spaceflight_godot::SceneTransferRequest scene{};
    scene.provider = &provider;
    scene.orientation = &provider;
    scene.catalog = &catalog;
    scene.craft = &craft;
    scene.j2_bodies = {celestial::bodies::earth};
    scene.integrator = make_integrator();
    // Absolute, as the simulation node holds it.  Converting it to a parking
    // orbit is one of the very few things the bridge computes, so it is made to
    // do the work rather than handed the answer.
    scene.initial.state.position =
        earth.state.position + math::Vec3{kParkingPosition[0], kParkingPosition[1],
                                          kParkingPosition[2]};
    scene.initial.state.velocity =
        earth.state.velocity + math::Vec3{kParkingVelocity[0], kParkingVelocity[1],
                                          kParkingVelocity[2]};
    scene.initial.mass = craft.initial_mass();
    scene.epoch = epoch;
    scene.center = celestial::bodies::earth;
    scene.target = celestial::bodies::moon;
    scene.target_periapsis_altitude_m = 100.0e3;
    scene.target_apoapsis_altitude_m = 100.0e3;
    scene.search_window_s = 2.0 * 3600.0;
    scene.departure_samples = 16;
    scene.execution = execution;
    scene.want_trajectory = false;
    return scene;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. The question
// ---------------------------------------------------------------------------

TEST(the_bridge_asks_the_campaigns_question) {
    const auto spice = sft::load_spice_or_skip();
    auto& provider = *spice.provider;
    const auto catalog = celestial::BodyCatalog::default_solar_system(provider);
    const auto craft = make_tug();
    const auto epoch = spice.time->parse(kEpoch);

    // Every execution model, because the translation is per-field and a drift in
    // one of them would otherwise only show up the day somebody flew it.
    for (const auto execution : {navigation::ExecutionModel::Impulsive,
                                 navigation::ExecutionModel::FiniteBurn,
                                 navigation::ExecutionModel::Autopilot}) {
        INFO(std::string{"  execution: "} + std::string{navigation::to_string(execution)});

        const auto expected = campaign_request(craft, execution);
        const auto scene = scene_request(provider, catalog, craft, epoch, execution);
        const auto actual = spaceflight_godot::request_for(scene);

        CHECK_EQ(actual.origin.name(), expected.origin.name());
        CHECK_EQ(actual.destination.name(), expected.destination.name());

        CHECK_EQ(actual.target_orbit.periapsis_altitude,
                 expected.target_orbit.periapsis_altitude);
        CHECK_EQ(actual.target_orbit.apoapsis_altitude, expected.target_orbit.apoapsis_altitude);
        CHECK_EQ(actual.target_orbit.minimum_periapsis_altitude,
                 expected.target_orbit.minimum_periapsis_altitude);
        CHECK_EQ(actual.target_orbit.maximum_apoapsis_altitude,
                 expected.target_orbit.maximum_apoapsis_altitude);
        CHECK_EQ(actual.target_orbit.maximum_eccentricity,
                 expected.target_orbit.maximum_eccentricity);

        CHECK_EQ(actual.departure_window.span.seconds(),
                 expected.departure_window.span.seconds());
        CHECK_EQ(actual.departure_window.samples, expected.departure_window.samples);

        // The grid, value by value.  A bridge that quietly narrowed it would
        // still fly, still converge, and still be a different planner.
        REQUIRE(actual.time_of_flight.days.size() == expected.time_of_flight.days.size());
        for (std::size_t i = 0; i < expected.time_of_flight.days.size(); ++i) {
            CHECK_EQ(actual.time_of_flight.days[i], expected.time_of_flight.days[i]);
        }

        CHECK_EQ(static_cast<int>(actual.objective), static_cast<int>(expected.objective));
        CHECK_EQ(static_cast<int>(actual.spacecraft.execution),
                 static_cast<int>(expected.spacecraft.execution));
        CHECK(actual.spacecraft.vehicle == expected.spacecraft.vehicle);
        CHECK_EQ(actual.spacecraft.hull_mass, expected.spacecraft.hull_mass);
        CHECK_EQ(actual.spacecraft.rcs_arm, expected.spacecraft.rcs_arm);
        CHECK_EQ(actual.spacecraft.rcs_mass_flow, expected.spacecraft.rcs_mass_flow);
        CHECK_EQ(actual.spacecraft.pointing.natural_frequency,
                 expected.spacecraft.pointing.natural_frequency);
        CHECK_EQ(actual.spacecraft.pointing.damping_ratio,
                 expected.spacecraft.pointing.damping_ratio);

        CHECK_EQ(actual.effort.screened_candidates, expected.effort.screened_candidates);
        CHECK_EQ(actual.effort.flown_candidates, expected.effort.flown_candidates);
        CHECK_EQ(actual.effort.b_plane_passes, expected.effort.b_plane_passes);
        CHECK_EQ(actual.effort.step_budget, expected.effort.step_budget);
        CHECK_EQ(actual.effort.approach_bracket_samples,
                 expected.effort.approach_bracket_samples);
        CHECK_EQ(actual.effort.periapsis_tolerance, expected.effort.periapsis_tolerance);
        CHECK_EQ(actual.effort.b_plane_angle.radians(), expected.effort.b_plane_angle.radians());
        CHECK_EQ(actual.effort.minimum_departure_perigee_altitude,
                 expected.effort.minimum_departure_perigee_altitude);
        CHECK_EQ(actual.effort.refuse_departure_conic_below_floor,
                 expected.effort.refuse_departure_conic_below_floor);
        CHECK_EQ(actual.effort.capture_burn_offset_seconds,
                 expected.effort.capture_burn_offset_seconds);
        CHECK_EQ(actual.effort.settling_threshold.radians(),
                 expected.effort.settling_threshold.radians());

        CHECK_EQ(actual.pinned.active, expected.pinned.active);
    }

    // And the state: the bridge is given the ship in the barycentric frame and
    // has to hand the core a parking orbit.  Checked because it is the only
    // arithmetic in the file, and arithmetic that appears once is arithmetic
    // nobody reviews twice.
    const auto scene = scene_request(provider, catalog, craft, epoch,
                                     navigation::ExecutionModel::FiniteBurn);
    const auto state = spaceflight_godot::state_for(scene);

    // NOT exact, and the reason is worth stating because it is the one place in
    // this file where a tolerance is unavoidable.
    //
    // The scene holds the ship at |r| = 1.4637e11 m from the Solar System
    // barycentre.  A 400 km parking orbit is 6.8e6 m from the Earth, five orders
    // of magnitude smaller, so `earth + parking - earth` is catastrophic
    // cancellation: a double carries 2.2e-16 relative precision, and 2.2e-16 of
    // 1.46e11 m is 3.2e-5 m.  Measured here: 4.5e-6 m, worst component.
    //
    // 1e-4 m is that bound with a factor of three of margin.  It is also, and
    // this matters more, ten times FINER than the integrator's own absolute
    // position tolerance of 1e-3 m -- so the cancellation cannot reach the
    // trajectory even in principle.
    for (int axis = 0; axis < 3; ++axis) {
        const double measured = axis == 0   ? state.vehicle.position.x
                                : axis == 1 ? state.vehicle.position.y
                                            : state.vehicle.position.z;
        CHECK_NEAR_ABS(measured, kParkingPosition[axis], 1.0e-4,
                       "earth + parking - earth at |r| = 1.46e11 m loses 2.2e-16 * 1.46e11 = "
                       "3.2e-5 m to cancellation; measured 4.5e-6 m, and the integrator's own "
                       "absolute position floor is 1e-3 m");
    }
    // The velocities round-trip EXACTLY, and that asymmetry is the same
    // arithmetic seen from the other side: the Earth moves at 3e4 m/s and the
    // parking orbit at 7.7e3, magnitudes within a factor of four, so there is
    // nothing to cancel.
    CHECK_EQ(state.vehicle.velocity.x, kParkingVelocity[0]);
    CHECK_EQ(state.vehicle.velocity.y, kParkingVelocity[1]);
    CHECK_EQ(state.vehicle.velocity.z, kParkingVelocity[2]);

    // What actually has to be exact, and is: the ABSOLUTE state the core
    // reconstructs by adding the origin back.  The intermediate parking vector
    // is a representation; this is the number the propagator integrates, and if
    // the round trip moved it the two callers would be flying different ships.
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();
    const auto earth = provider.state(celestial::bodies::earth, epoch, frame);
    const auto reconstructed = state.vehicle.position + earth.state.position;
    CHECK_EQ(reconstructed.x, scene.initial.state.position.x);
    CHECK_EQ(reconstructed.y, scene.initial.state.position.y);
    CHECK_EQ(reconstructed.z, scene.initial.state.position.z);

    CHECK_EQ(state.epoch.seconds_since_j2000(), epoch.seconds_since_j2000());
    REQUIRE(state.j2_bodies.size() == 1);
    CHECK_EQ(state.j2_bodies.front().name(), celestial::bodies::earth.name());
}

// ---------------------------------------------------------------------------
// 2. The answer
// ---------------------------------------------------------------------------

TEST(the_game_and_the_campaign_plan_the_same_transfer) {
    const auto spice = sft::load_spice_or_skip();
    auto& provider = *spice.provider;
    const auto catalog = celestial::BodyCatalog::default_solar_system(provider);
    const auto craft = make_tug();
    const auto epoch = spice.time->parse(kEpoch);

    // FINITE_BURN and not AUTOPILOT, for runtime and for nothing else.
    //
    // Under AUTOPILOT the two differential correctors invert a map with the
    // attitude controller inside it, and a single epoch costs about two minutes;
    // this test flies the mission TWICE.  The translation being checked is
    // identical either way -- the execution model is one field, and the test
    // above compares it for all three -- so the end-to-end flight is done in the
    // model that costs seconds.
    //
    // The full production configuration is used otherwise: the ten-value
    // time-of-flight grid, sixteen departure samples, twenty-four screened
    // candidates, six flown.  Narrowing it would have meant narrowing it on both
    // sides through a knob the cockpit does not have, which would have been a
    // test of the knob.
    const auto request = campaign_request(craft, navigation::ExecutionModel::FiniteBurn);
    const auto scene =
        scene_request(provider, catalog, craft, epoch, navigation::ExecutionModel::FiniteBurn);

    navigation::SimulationState state{};
    state.provider = &provider;
    state.orientation = &provider;
    state.catalog = catalog;
    state.j2_bodies = {celestial::bodies::earth};
    state.vehicle.position =
        math::Vec3{kParkingPosition[0], kParkingPosition[1], kParkingPosition[2]};
    state.vehicle.velocity =
        math::Vec3{kParkingVelocity[0], kParkingVelocity[1], kParkingVelocity[2]};
    state.epoch = epoch;
    state.integrator = make_integrator();

    const auto campaign = navigation::plan_mission(state, request);
    const auto game = spaceflight_godot::plan_transfer(scene);

    REQUIRE(campaign.ok());
    REQUIRE(game.ok());

    const auto& c = campaign.metrics;
    const auto& g = game.metrics;

    // ---- the eight quantities of section 6 --------------------------------
    CHECK_EQ(c.departure.seconds_since_j2000(), g.departure.seconds_since_j2000());
    CHECK_EQ(c.time_of_flight_s, g.time_of_flight_s);
    CHECK_EQ(static_cast<int>(c.branch), static_cast<int>(g.branch));
    CHECK_EQ(c.injection_delta_v, g.injection_delta_v);
    CHECK_EQ(c.b_plane_target.b_dot_t, g.b_plane_target.b_dot_t);
    CHECK_EQ(c.b_plane_target.b_dot_r, g.b_plane_target.b_dot_r);
    CHECK_EQ(c.capture_delta_v, g.capture_delta_v);
    CHECK_EQ(c.predicted_flyby_periapsis, g.predicted_flyby_periapsis);
    CHECK_EQ(c.predicted_periapsis_altitude, g.predicted_periapsis_altitude);
    CHECK_EQ(c.predicted_apoapsis_altitude, g.predicted_apoapsis_altitude);
    CHECK_EQ(c.predicted_eccentricity, g.predicted_eccentricity);
    CHECK_EQ(c.predicted_inclination.radians(), g.predicted_inclination.radians());
    CHECK_EQ(c.predicted_raan.radians(), g.predicted_raan.radians());

    // ---- and the burns, which are what actually gets flown -----------------
    //
    // Agreeing on the numbers and disagreeing on the maneuvers would be the
    // worst of the two failures: the readout would be right and the ship would
    // fly something else.
    REQUIRE(campaign.maneuvers.size() == game.maneuvers.size());
    REQUIRE(campaign.maneuvers.size() == 2);
    for (std::size_t i = 0; i < campaign.maneuvers.size(); ++i) {
        const auto& left = campaign.maneuvers.maneuvers()[i];
        const auto& right = game.maneuvers.maneuvers()[i];
        INFO("  maneuver: " + left.name);
        CHECK_EQ(left.name, right.name);
        CHECK_EQ(left.ignition.seconds_since_j2000(), right.ignition.seconds_since_j2000());
        CHECK_EQ(left.duration.seconds(), right.duration.seconds());
        CHECK_EQ(left.throttle, right.throttle);
        CHECK_EQ(static_cast<int>(left.guidance), static_cast<int>(right.guidance));
        CHECK_EQ(left.inertial_direction.x, right.inertial_direction.x);
        CHECK_EQ(left.inertial_direction.y, right.inertial_direction.y);
        CHECK_EQ(left.inertial_direction.z, right.inertial_direction.z);
    }

    // The alternatives too: section 13 puts them on the cockpit, so a cockpit
    // that saw a different list would be reporting a different search.
    REQUIRE(campaign.alternatives.size() == game.alternatives.size());
    for (std::size_t i = 0; i < campaign.alternatives.size(); ++i) {
        CHECK_EQ(campaign.alternatives[i].label, game.alternatives[i].label);
        CHECK_EQ(campaign.alternatives[i].predicted_inclination.radians(),
                 game.alternatives[i].predicted_inclination.radians());
    }

    INFO("  departure " + std::to_string(c.departure.seconds_since_j2000()) + " s TDB, tof " +
         std::to_string(c.time_of_flight_s / 86400.0) + " d, injection " +
         std::to_string(c.injection_delta_v) + " m/s, capture " +
         std::to_string(c.capture_delta_v) + " m/s, orbit " +
         std::to_string(c.predicted_periapsis_altitude / 1000.0) + " x " +
         std::to_string(c.predicted_apoapsis_altitude / 1000.0) + " km, i " +
         std::to_string(c.predicted_inclination.degrees()) + " deg");
}

// ---------------------------------------------------------------------------
// 3. The shape
// ---------------------------------------------------------------------------

// The bridge must not start deciding things again.
//
// The two tests above catch a divergence in the QUESTION and in the ANSWER.
// This catches a divergence in the SHAPE: a bridge that grows a Lambert call, a
// corrector or a candidate loop is on its way back to being a second planner,
// and it might well agree with the core for a while before it stops.
// The tank as it is, not as it left the factory.
//
// The planner used to start every search from the craft's INITIAL mass, whatever
// the ship actually weighed.  The burns are flown for a planned duration, so a
// lighter ship over-performs each of them, and a lunar transfer has no midcourse
// to absorb it: 0.015 kg short of full -- one second of the main engine --
// turned a 100 km capture into 79 km, 0.05 kg into 37 km, 0.2 kg into an impact.
// The M7 demonstration found it by testing the engine before planning, on a
// machine slow enough for "until the engine is running" to last a few seconds.
TEST(the_bridge_plans_for_the_tank_as_it_is) {
    const auto spice = sft::load_spice_or_skip();
    auto& provider = *spice.provider;
    const auto catalog = celestial::BodyCatalog::default_solar_system(provider);
    const auto craft = make_tug();
    const auto epoch = spice.time->parse(kEpoch);

    auto scene = scene_request(provider, catalog, craft, epoch,
                               navigation::ExecutionModel::FiniteBurn);
    const double lighter = craft.initial_mass() - 0.2;
    scene.initial.mass = lighter;

    const auto plan = spaceflight_godot::plan_transfer(scene);
    REQUIRE(plan.ok());
    // FINITE_BURN spends nothing before the injection, so the mass at ignition is
    // exactly the mass the search was handed.
    CHECK_NEAR_ABS(plan.metrics.mass_at_departure, lighter, 1.0e-9,
                   "coasting to the ignition burns no propellant under ideal guidance");
}

TEST(the_bridge_translates_and_does_not_compute) {
    const auto source = sft::read_repository_file("godot/gdextension/src/mission_planner.cpp");
    // A failure, not a skip: a missing file means the thing being checked has
    // moved, and a check that quietly stops checking is worse than no check.
    REQUIRE(!source.empty());

    const char* forbidden[] = {
        "solve_lambert",       // the search belongs to the core
        "correct_departure",   // so does the corrector
        "b_plane_from_state",  // and the B-plane
        "aim_for_periapsis",
        "plan_insertion",
        "maneuver_for_delta_v",  // the burns come back from the core already built
        "run_mission",           // the bridge never propagates anything
        "propagate",
        "Trajectory",            // nor records one
    };
    for (const char* symbol : forbidden) {
        INFO(std::string{"  must not appear: "} + symbol);
        CHECK(source.find(symbol) == std::string::npos);
    }

    // And it has to stay SMALL.  532 lines before this milestone, under 100
    // after; a bridge creeping back past two hundred is doing something, and
    // whatever it is belongs in the core.  The bound is loose on purpose -- it is
    // a tripwire, not a style rule.
    const auto lines =
        static_cast<std::size_t>(std::count(source.begin(), source.end(), '\n'));
    INFO("  bridge is " + std::to_string(lines) + " lines");
    CHECK(lines < 200);
}
