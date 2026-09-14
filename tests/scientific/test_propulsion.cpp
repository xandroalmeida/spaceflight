// Propulsion against closed-form results.
//
// The first tests run in FREE SPACE -- the force model contains the engine and
// nothing else -- so the rocket equation is not an approximation there: it is the
// exact solution, and any deviation is integration error.  The later tests put
// the same burns in a gravity field, where the difference between the impulsive
// plan and the finite burn becomes visible and gets measured.

#include "core/celestial/body_catalog.hpp"
#include "core/gravity/composite_force_model.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/attitude/inertia.hpp"
#include "core/navigation/mission.hpp"
#include "core/propulsion/main_engine_force.hpp"
#include "core/navigation/trajectory_planner.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "tests/support/analytic_ephemeris.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

using namespace sf;
using sf::math::Vec3;
using sf::navigation::GuidanceMode;
using sf::navigation::Maneuver;
using sf::navigation::ManeuverPlan;

namespace {

constexpr double kGm = 3.9860043550702266e14;
const celestial::BodyId kEarth = celestial::bodies::earth;
const auto kSsb = coordinates::ReferenceFrame::ssb_j2000();

// w = 1e-4 c is about 30 km/s: a plausible high performance thruster, and deep
// in the Newtonian regime, which is where Milestone 1 lives.
propulsion::EngineSpec engine(double max_flow = 1.0, double efficiency = 0.5) {
    return propulsion::EngineSpec{"test", max_flow, 1.0e-4, efficiency};
}

spacecraft::Spacecraft craft(double max_flow = 1.0) {
    return spacecraft::Spacecraft{"probe", 1000.0, 1000.0, engine(max_flow)};
}

propagation::IntegratorConfig integrator_config() {
    propagation::IntegratorConfig cfg{};
    cfg.relative_tolerance = 1.0e-12;
    cfg.absolute_tolerance_position = 1.0e-6;
    cfg.absolute_tolerance_velocity = 1.0e-9;
    cfg.absolute_tolerance_mass = 1.0e-9;
    cfg.initial_step = time::Duration::seconds(1.0);
    cfg.max_step = time::Duration::seconds(60.0);
    return cfg;
}

Maneuver prograde_burn(double start, double duration, double throttle = 1.0) {
    Maneuver m{};
    m.name = "burn";
    m.ignition = time::CoordinateTime::from_seconds_since_j2000(start);
    m.duration = time::Duration::seconds(duration);
    m.throttle = throttle;
    m.guidance = GuidanceMode::Prograde;
    m.reference = kEarth;
    return m;
}

}  // namespace

TEST(a_burn_in_free_space_reproduces_tsiolkovsky_exactly) {
    // No gravity in the force model at all: the engine is the only force, so
    // delta_v = v_eff ln(m0/m1) is the exact answer, not an approximation.
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};
    const auto ship = craft();

    ManeuverPlan plan;
    plan.add(prograde_burn(0.0, 100.0));

    navigation::ManeuverExecutor executor{provider, ship, plan, kSsb};
    propagation::DormandPrince54Propagator propagator{executor, integrator_config()};

    propagation::PropagationState initial{};
    initial.state.position = Vec3{1.0e7, 0.0, 0.0};
    initial.state.velocity = Vec3{100.0, 0.0, 0.0};
    initial.mass = ship.initial_mass();

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = navigation::run_mission(propagator, executor, initial, t0,
                                                t0 + time::Duration::seconds(200.0));
    REQUIRE(result.ok());
    INFO(result.describe_burns());

    const double expected_mass = 2000.0 - 100.0;  // q = 1 kg/s for 100 s
    CHECK_NEAR_REL(result.state.mass, expected_mass, 1.0e-12,
                   "dm/dt = -q with q constant, so the mass is linear in time and the 5th order "
                   "integrator is exact on it up to rounding; the bound is the accumulation over "
                   "~100 steps of the mass tolerance, 1e-9 kg out of 1900 kg");

    const double delta_v = ship.engine().effective_exhaust_velocity() * std::log(2000.0 / 1900.0);
    const double achieved = result.state.state.velocity.norm() - 100.0;

    std::ostringstream os;
    os << "free space: delta-v achieved " << achieved << " m/s, Tsiolkovsky says " << delta_v;
    INFO(os.str());

    CHECK_NEAR_REL(achieved, delta_v, 1.0e-10,
                   "with no other force, the rocket equation is the exact solution of the "
                   "equations of motion being integrated. The residual is the integrator's own "
                   "error over the burn: at rtol = 1e-12 over ~100 steps that is ~1e-10 relative");

    // Straight line: nothing bent the trajectory.
    CHECK_NEAR_ABS(result.state.state.velocity.y, 0.0, 1.0e-12,
                   "thrust was along +x and there was no other force; any transverse velocity "
                   "would mean the guidance had drifted");
    CHECK_NEAR_ABS(result.state.state.position.z, 0.0, 1.0e-9, "same argument for position");
}

TEST(an_empty_tank_stops_the_burn_and_says_so) {
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};
    const auto ship = craft();

    // 2000 s at 1 kg/s would need 2000 kg; the tank holds 1000.
    ManeuverPlan plan;
    plan.add(prograde_burn(0.0, 2000.0));

    navigation::ManeuverExecutor executor{provider, ship, plan, kSsb};
    propagation::DormandPrince54Propagator propagator{executor, integrator_config()};

    propagation::PropagationState initial{};
    initial.state.position = Vec3{1.0e7, 0.0, 0.0};
    initial.state.velocity = Vec3{100.0, 0.0, 0.0};
    initial.mass = ship.initial_mass();

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = navigation::run_mission(propagator, executor, initial, t0,
                                                t0 + time::Duration::seconds(2500.0));

    CHECK(result.status == propagation::PropagationStatus::OutOfPropellant);
    INFO("reported: " + result.message);
    INFO(result.describe_burns());

    REQUIRE(result.burns.size() == std::size_t{1});
    CHECK(result.burns.front().ran_dry);

    CHECK_NEAR_REL(result.state.mass, ship.dry_mass(), 1.0e-12,
                   "the run is cut exactly at the exhaustion instant, which is known in closed "
                   "form because the throttle is constant through a burn: t = propellant/q. No "
                   "search, no overshoot, no negative propellant");
    CHECK_NEAR_REL(result.burns.front().duration.seconds(), 1000.0, 1.0e-12,
                   "1000 kg of propellant at 1 kg/s is exactly 1000 s");

    const double budget = ship.engine().effective_exhaust_velocity() * std::log(2.0);
    CHECK_NEAR_REL(result.burns.front().delta_v_rocket, budget, 1.0e-12,
                   "burning the tank dry halves the mass, delivering exactly v_eff*ln(2) -- which "
                   "is what Spacecraft::delta_v_budget promised before the burn");
}

TEST(the_engine_delta_v_and_the_orbital_speed_change_differ_by_the_gravity_loss) {
    // Same delta-v, two thrust levels.  The impulsive plan is the same; what
    // changes is how long the ship spends climbing while it burns.
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};

    const std::vector<celestial::BodyId> ids{kEarth};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);

    const double radius = 6.778e6;
    const double v_circular = trajectory::circular_speed(kGm, radius);
    const double target_apoapsis = 2.0e7;

    const double planned_delta_v =
        navigation::delta_v_to_change_apsis(kGm, radius, v_circular, target_apoapsis);

    std::ostringstream header;
    header << "impulsive plan: raise apoapsis from " << radius << " m to " << target_apoapsis
           << " m needs " << planned_delta_v << " m/s";
    INFO(header.str());

    struct Outcome {
        double burn_seconds;
        double loss;
        double apoapsis_shortfall;
    };
    std::vector<Outcome> outcomes;

    for (const double flow : {10.0, 1.0}) {
        const auto ship = craft(flow);
        const double duration =
            ship.engine().burn_duration_for_delta_v(ship.initial_mass(), planned_delta_v);

        ManeuverPlan plan;
        plan.add(prograde_burn(0.0, duration));

        navigation::ManeuverExecutor executor{provider, ship, plan, kSsb};

        // The executor is BORROWED by the force sum: the mission runner needs to
        // keep reading the plan out of it while the integrator calls it.
        gravity::CompositeForceModel forces;
        forces.add(std::make_unique<gravity::PointMassGravity>(provider, catalog, kSsb));
        forces.add_reference(executor);

        propagation::PropagationState initial{};
        initial.state.position = Vec3{radius, 0.0, 0.0};
        initial.state.velocity = Vec3{0.0, v_circular, 0.0};
        initial.mass = ship.initial_mass();

        propagation::DormandPrince54Propagator propagator{forces, integrator_config()};

        const auto t0 = time::CoordinateTime::j2000();
        const auto result = navigation::run_mission(propagator, executor, initial, t0,
                                                    t0 + time::Duration::seconds(duration + 10.0));
        if (!result.ok()) {
            INFO("status: " + propagation::to_string(result.status) + " -- " + result.message);
        }
        REQUIRE(result.ok());
        REQUIRE(result.burns.size() == std::size_t{1});

        const auto& burn = result.burns.front();
        const double loss = burn.delta_v_rocket - burn.speed_change;

        const auto elements = trajectory::elements_from_state(result.state.state, kGm);
        const double shortfall = target_apoapsis - elements.apoapsis_radius;

        std::ostringstream os;
        os << "flow " << flow << " kg/s: burn " << burn.duration.seconds() << " s, engine delta-v "
           << burn.delta_v_rocket << " m/s, speed change " << burn.speed_change
           << " m/s, gravity loss " << loss << " m/s, apoapsis " << elements.apoapsis_radius
           << " m (short by " << shortfall << " m)";
        INFO(os.str());

        CHECK_NEAR_REL(burn.delta_v_rocket, planned_delta_v, 1.0e-9,
                       "the engine delivers exactly what the rocket equation says it will; that "
                       "part is independent of where the ship is");
        CHECK(loss > 0.0);
        outcomes.push_back(Outcome{burn.duration.seconds(), loss, shortfall});
    }

    // The physical prediction: gravity loss grows as the SQUARE of the burn
    // duration.  Ten times the thrust is a tenth of the burn time, so the loss
    // should fall by about a hundred.
    const double duration_ratio = outcomes[1].burn_seconds / outcomes[0].burn_seconds;
    const double loss_ratio = outcomes[1].loss / outcomes[0].loss;

    std::ostringstream os;
    os << "burn duration ratio " << duration_ratio << ", loss ratio " << loss_ratio
       << " (quadratic scaling predicts " << duration_ratio * duration_ratio << ")";
    INFO(os.str());

    CHECK_NEAR_REL(loss_ratio, duration_ratio * duration_ratio, 0.25,
                   "the loss scales as (n dt)^2: quadratic in the burn duration. The COEFFICIENT "
                   "is not universal -- for a prograde burn from a circular orbit the loss comes "
                   "from the ship climbing while it burns, not from the thrust vector rotating, "
                   "and the measured coefficient is ~0.31 rather than the 1/24 of the rotation "
                   "mechanism (docs/architecture/navigation.md section 2). The EXPONENT is what "
                   "this test asserts, and it is robust: 25% bounds the cubic correction for a "
                   "burn lasting 4% of the orbital period. What the test rejects is a loss that "
                   "scales linearly (a bookkeeping error) or not at all (an impulsive cheat)");
}

TEST(a_short_burn_reproduces_the_impulsive_plan) {
    // In the limit of high thrust the finite burn must converge on the impulsive
    // prediction.  This is the justification for planning impulsively at all.
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};
    const std::vector<celestial::BodyId> ids{kEarth};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);

    const double radius = 6.778e6;
    const double v_circular = trajectory::circular_speed(kGm, radius);
    const double target_apoapsis = 2.0e7;
    const double planned_delta_v =
        navigation::delta_v_to_change_apsis(kGm, radius, v_circular, target_apoapsis);

    // 200 kg/s: the burn lasts about a second.
    const auto ship = craft(200.0);
    const double duration =
        ship.engine().burn_duration_for_delta_v(ship.initial_mass(), planned_delta_v);

    ManeuverPlan plan;
    plan.add(prograde_burn(0.0, duration));

    navigation::ManeuverExecutor executor{provider, ship, plan, kSsb};

    gravity::CompositeForceModel forces;
    forces.add(std::make_unique<gravity::PointMassGravity>(provider, catalog, kSsb));
    forces.add_reference(executor);

    propagation::PropagationState initial{};
    initial.state.position = Vec3{radius, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, v_circular, 0.0};
    initial.mass = ship.initial_mass();

    propagation::DormandPrince54Propagator propagator{forces, integrator_config()};
    const auto t0 = time::CoordinateTime::j2000();
    const auto result = navigation::run_mission(propagator, executor, initial, t0,
                                                t0 + time::Duration::seconds(duration + 1.0));
    if (!result.ok()) {
        INFO("status: " + propagation::to_string(result.status) + " -- " + result.message);
    }
    REQUIRE(result.ok());

    const auto elements = trajectory::elements_from_state(result.state.state, kGm);

    std::ostringstream os;
    os << "burn of " << duration << " s: apoapsis " << elements.apoapsis_radius << " m vs target "
       << target_apoapsis << " m, periapsis " << elements.periapsis_radius << " m";
    INFO(os.str());

    CHECK_NEAR_REL(elements.apoapsis_radius, target_apoapsis, 1.0e-4,
                   "the burn lasts ~1 s out of a 5553 s orbit, so (n dt)^2/24 ~ 6e-8 of the "
                   "delta-v is lost to the climb. Propagated into the apoapsis that is ~1e-5 "
                   "relative; 1e-4 leaves an order of margin and still fails loudly if the "
                   "impulsive plan and the executed burn ever stop agreeing");
    CHECK_NEAR_REL(elements.periapsis_radius, radius, 1.0e-4,
                   "a prograde burn at periapsis raises the apoapsis and leaves the periapsis "
                   "where it was -- to the same order");
}


TEST(the_hohmann_delta_v_matches_vis_viva_computed_independently) {
    // Pure arithmetic: the planner's closed form against the vis-viva equation
    // evaluated directly.  No integration, no tolerance on physics -- only on
    // floating point.
    const double r1 = 6.778e6;
    const double r2 = 4.2164e7;
    const auto transfer = navigation::plan_hohmann(kGm, r1, r2);

    const double a_transfer = 0.5 * (r1 + r2);
    const double v_circular_1 = std::sqrt(kGm / r1);
    const double v_circular_2 = std::sqrt(kGm / r2);
    const double v_perigee = std::sqrt(kGm * (2.0 / r1 - 1.0 / a_transfer));
    const double v_apogee = std::sqrt(kGm * (2.0 / r2 - 1.0 / a_transfer));

    std::ostringstream os;
    os << "LEO " << r1 << " m -> GEO " << r2 << " m: dv1 = " << transfer.delta_v_departure
       << " m/s, dv2 = " << transfer.delta_v_arrival << " m/s, total "
       << transfer.total_delta_v << " m/s, transfer time " << transfer.transfer_time / 3600.0
       << " h";
    INFO(os.str());

    CHECK_NEAR_REL(transfer.delta_v_departure, v_perigee - v_circular_1, 1.0e-14,
                   "the planner's form sqrt(mu/r1)(sqrt(2 r2/(r1+r2)) - 1) is algebraically the "
                   "same as the vis-viva difference; only the order of the operations differs");
    CHECK_NEAR_REL(transfer.delta_v_arrival, v_circular_2 - v_apogee, 1.0e-14, "same at arrival");
    CHECK_NEAR_REL(transfer.transfer_semi_major_axis, a_transfer, 0.0,
                   "the transfer semi-major axis is (r1+r2)/2 by definition");
    CHECK_NEAR_REL(transfer.transfer_time,
                   units::pi * std::sqrt(a_transfer * a_transfer * a_transfer / kGm), 1.0e-15,
                   "half the period of the transfer ellipse");

    // Sanity against the number every textbook quotes for LEO to GEO.
    CHECK_NEAR_ABS(transfer.total_delta_v, 3900.0, 100.0,
                   "a LEO-to-GEO Hohmann transfer is quoted as ~3.9 km/s in every reference; the "
                   "100 m/s bound covers the range of LEO altitudes those references assume");
}

TEST(a_two_burn_hohmann_transfer_arrives_in_a_circular_orbit) {
    // The full pipeline: plan two impulsive burns, size them with the rocket
    // equation, execute both through the mission runner, and check the orbit that
    // comes out.  High thrust keeps the burns short so that the impulsive plan is
    // a good description of what was flown.
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};
    const std::vector<celestial::BodyId> ids{kEarth};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);

    const double r1 = 6.778e6;
    const double r2 = 4.2164e7;
    const auto transfer = navigation::plan_hohmann(kGm, r1, r2);

    const spacecraft::Spacecraft ship{"tug", 2000.0, 4000.0, engine(400.0)};

    const auto t0 = time::CoordinateTime::j2000();
    navigation::ManeuverPlan plan;

    const auto departure = navigation::maneuver_for_delta_v(
        ship, ship.initial_mass(), transfer.delta_v_departure, t0, GuidanceMode::Prograde, kEarth,
        1.0, "departure", navigation::BurnCentering::StartAtIgnition);
    const double mass_after_departure =
        ship.initial_mass() - ship.engine().mass_flow_at(1.0) * departure.duration.seconds();

    // Apoapsis arrives half a transfer period after the EFFECTIVE impulse, which
    // sits at the middle of the first burn.
    const auto arrival_epoch = t0 + time::Duration::seconds(0.5 * departure.duration.seconds() +
                                                            transfer.transfer_time);
    const auto arrival = navigation::maneuver_for_delta_v(
        ship, mass_after_departure, transfer.delta_v_arrival, arrival_epoch,
        GuidanceMode::Prograde, kEarth, 1.0, "circularise");

    plan.add(departure);
    plan.add(arrival);

    navigation::ManeuverExecutor executor{provider, ship, plan, kSsb};
    gravity::CompositeForceModel forces;
    forces.add(std::make_unique<gravity::PointMassGravity>(provider, catalog, kSsb));
    forces.add_reference(executor);

    propagation::PropagationState initial{};
    initial.state.position = Vec3{r1, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, trajectory::circular_speed(kGm, r1), 0.0};
    initial.mass = ship.initial_mass();

    auto cfg = integrator_config();
    cfg.max_step = time::Duration::seconds(300.0);
    propagation::DormandPrince54Propagator propagator{forces, cfg};

    const auto end = arrival.cutoff() + time::Duration::seconds(60.0);
    const auto mission = navigation::run_mission(propagator, executor, initial, t0, end);
    REQUIRE(mission.ok());
    REQUIRE(mission.burns.size() == std::size_t{2});
    INFO(mission.describe_burns());

    const auto elements = trajectory::elements_from_state(mission.state.state, kGm);

    std::ostringstream os;
    os << "after both burns: a = " << elements.semi_major_axis << " m (target " << r2
       << "), e = " << elements.eccentricity << ", rp = " << elements.periapsis_radius
       << " m, ra = " << elements.apoapsis_radius << " m, propellant used "
       << (ship.initial_mass() - mission.state.mass) << " kg";
    INFO(os.str());

    CHECK_NEAR_REL(elements.semi_major_axis, r2, 2.0e-3,
                   "with 400 kg/s the two burns last 2.2 s and 1.2 s out of a 5.3 hour transfer, "
                   "so the gravity loss is (n dt)^2-small: ~1e-6 of each delta-v at departure and "
                   "far less at apoapsis. What remains at the 1e-3 level is the finite burn arc "
                   "and the fact that the second burn is centred on an epoch computed from the "
                   "impulsive plan rather than on the true apoapsis");
    CHECK_NEAR_ABS(elements.eccentricity, 0.0, 5.0e-3,
                   "the point of the second burn is to circularise; a residual eccentricity of "
                   "5e-3 corresponds to ~200 km of radius variation on a 42 164 km orbit. "
                   "Measured: 4.9e-6, i.e. 200 m -- three orders better than the bound, because "
                   "at this thrust the burns really are nearly impulsive. The loose bound is "
                   "deliberate: it is the level at which the transfer would stop being usable, "
                   "and it is what a regression in the executor would have to cross");

    const double total_delta_v =
        mission.burns[0].delta_v_rocket + mission.burns[1].delta_v_rocket;
    CHECK_NEAR_REL(total_delta_v, transfer.total_delta_v, 1.0e-9,
                   "the engine delivers exactly what the plan asked for; whether that was the "
                   "right amount is what the orbit above answers");
}


TEST(the_throttle_burns_along_the_nose_and_nowhere_else) {
    // The interactive path: no plan, no guidance mode, just an attitude and a
    // throttle. Where the burn goes is decided by where the ship points, which
    // is the coupling a cockpit needs (and a planned burn deliberately avoids).
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};
    const spacecraft::Spacecraft ship{"probe", 1000.0, 1000.0, engine(1.0)};
    propulsion::MainEngineForce main_engine{ship};

    propagation::PropagationState initial{};
    initial.state.position = Vec3{1.0e7, 0.0, 0.0};
    initial.state.velocity = Vec3{};
    initial.mass = ship.initial_mass();

    // Nose along +y: 90 degrees from the body's default +x.
    initial.attitude.orientation =
        math::Quaternion::from_axis_angle(Vec3::unit_z(), units::Angle::degrees(90.0));

    const auto inertia = attitude::InertiaTensor::solid_box(1000.0, Vec3{8.0, 3.0, 3.0});
    propagation::DormandPrince54Propagator propagator{main_engine, integrator_config()};
    propagator.set_inertia(&inertia);

    // Idle: nothing happens at all.
    const auto t0 = time::CoordinateTime::j2000();
    const auto coasting = propagator.propagate(initial, t0, t0 + time::Duration::seconds(10.0));
    REQUIRE(coasting.ok());
    CHECK_NEAR_ABS(coasting.state.state.velocity.norm(), 0.0, 0.0,
                   "throttle zero means no acceleration and no consumption: not a small number, "
                   "exactly zero");
    CHECK_NEAR_ABS(coasting.state.mass, ship.initial_mass(), 0.0, "and no propellant burnt");

    main_engine.set_throttle(1.0);
    const double dt = 100.0;
    const auto burn = propagator.propagate(initial, t0, t0 + time::Duration::seconds(dt));
    REQUIRE(burn.ok());

    const double delta_v = ship.engine().effective_exhaust_velocity() *
                           std::log(ship.initial_mass() / (ship.initial_mass() - dt));

    std::ostringstream os;
    os << "throttle 1.0 for " << dt << " s with the nose along +y: dv = "
       << burn.state.state.velocity.norm() << " m/s, Tsiolkovsky " << delta_v
       << ", transverse " << std::hypot(burn.state.state.velocity.x, burn.state.state.velocity.z);
    INFO(os.str());

    CHECK_NEAR_REL(burn.state.state.velocity.y, delta_v, 1.0e-10,
                   "with no gravity in this force model the rocket equation is exact, and the "
                   "burn went along the nose");
    CHECK_NEAR_ABS(std::hypot(burn.state.state.velocity.x, burn.state.state.velocity.z), 0.0,
                   1.0e-9,
                   "nothing pushed sideways: the direction comes from the attitude quaternion and "
                   "nothing else");
    CHECK_NEAR_REL(burn.state.mass, ship.initial_mass() - dt, 1.0e-12, "q = 1 kg/s for 100 s");

    // Point the other way and the same throttle undoes it.
    propagation::PropagationState reversed = burn.state;
    reversed.attitude.orientation =
        math::Quaternion::from_axis_angle(Vec3::unit_z(), units::Angle::degrees(-90.0));
    const auto back = propagator.propagate(reversed, t0, t0 + time::Duration::seconds(10.0));
    REQUIRE(back.ok());
    CHECK(back.state.state.velocity.y < burn.state.state.velocity.y);

    // Tank dry: thrust stops with no special case anywhere.
    propagation::PropagationState empty = initial;
    empty.mass = ship.dry_mass();
    const auto nothing = propagator.propagate(empty, t0, t0 + time::Duration::seconds(10.0));
    REQUIRE(nothing.ok());
    CHECK_NEAR_ABS(nothing.state.state.velocity.norm(), 0.0, 0.0,
                   "no propellant, no thrust -- because the thrust IS the consumption");
}
