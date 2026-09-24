// The direct transfer: continuous guided thrust from low Earth orbit to every
// destination the directory accepts today, planned and flown in the full model.
// See docs/physics/direct-transfer-guidance.md.

#include "core/celestial/body_catalog.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/navigation/direct_transfer.hpp"
#include "core/navigation/maneuver_executor.hpp"
#include "core/navigation/mission_planner.hpp"
#include "core/propulsion/engine.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/units/constants.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>

using namespace sf;
using sf::math::Vec3;

namespace {

spacecraft::Spacecraft make_torch() {
    const propulsion::MultiModeEngine engine{
        {{"IMPULSE", propulsion::EngineSpec{"IMPULSE", 0.0222376, 0.03, 1.0}},
         {"CRUISE", propulsion::EngineSpec{"CRUISE", 7.470950e-05, 0.5, 1.0}},
         {"RELATIVISTIC", propulsion::EngineSpec{"RELATIVISTIC", 6.8866239e-02, 0.95, 1.0}, "annihilation"}}};
    spacecraft::Spacecraft craft{"Torch", 1000.0, 19000.0, engine};
    craft.select_mode("RELATIVISTIC");
    return craft;
}

template <typename Provider>
navigation::SimulationState leo(const Provider& provider, const celestial::BodyCatalog& catalog,
                                time::CoordinateTime epoch) {
    navigation::SimulationState state{};
    state.provider = &provider;
    state.orientation = &provider;
    state.catalog = catalog;
    state.j2_bodies = {celestial::bodies::earth};
    const double radius = provider.mean_radius(celestial::bodies::earth) + 400.0e3;
    const double speed = std::sqrt(provider.gravitational_parameter(celestial::bodies::earth) / radius);
    const double inclination = 51.6 * units::pi / 180.0;
    state.vehicle.position = Vec3{radius, 0.0, 0.0};
    state.vehicle.velocity = Vec3{0.0, speed * std::cos(inclination), speed * std::sin(inclination)};
    state.epoch = epoch;
    state.mass = 20000.0;
    state.integrator.relative_tolerance = 1.0e-11;
    state.integrator.absolute_tolerance_position = 1.0e-3;
    state.integrator.absolute_tolerance_velocity = 1.0e-6;
    state.integrator.initial_step = time::Duration::seconds(1.0);
    state.integrator.max_step = time::Duration::seconds(600.0);
    return state;
}

navigation::MissionPlanResult plan(const navigation::SimulationState& state, const spacecraft::Spacecraft& craft,
                                   celestial::BodyId destination, double altitude) {
    navigation::MissionRequest request{};
    request.kind = navigation::TransferKind::Direct;
    request.origin = celestial::bodies::earth;
    request.destination = destination;
    request.target_orbit.periapsis_altitude = altitude;
    request.target_orbit.apoapsis_altitude = altitude;
    request.spacecraft.vehicle = &craft;
    return navigation::plan_mission(state, request);
}

}  // namespace

TEST(the_thrust_asked_for_produces_exactly_the_commanded_acceleration) {
    // ManeuverExecutor::proper_thrust_for inverts what the integrator does with
    // a rest-frame thrust (dormand_prince_54.cpp): du/dt = e F / (gamma m), with
    // e = n + (gamma - 1)(n.b) b. Applied forwards here, and turned back into a
    // coordinate acceleration through v = u/gamma, it has to give the command
    // back -- at rest, at 0.5 c and at 0.95 c, along, across and oblique.
    const double c = units::c;
    const double mass = 1234.5;
    for (const double beta : {0.0, 0.5, 0.95}) {
        const Vec3 v{beta * c, 0.0, 0.0};
        for (const Vec3 a : {Vec3{3.0, 0.0, 0.0}, Vec3{0.0, 4.0, 0.0}, Vec3{-2.0, 1.0, 5.0}}) {
            const Vec3 force = navigation::ManeuverExecutor::proper_thrust_for(
                a, v, mass, propagation::Kinematics::SpecialRelativistic);
            const double gamma = 1.0 / std::sqrt(1.0 - beta * beta);
            const Vec3 n = force.normalized();
            Vec3 e = n;
            if (beta > 0.0) {
                e += Vec3::unit_x() * ((gamma - 1.0) * n.x);
            }
            const Vec3 du_dt = e * (force.norm() / (gamma * mass));
            const Vec3 u = v * gamma;
            const Vec3 dv_dt = du_dt / gamma - u * (dot(u, du_dt) / (gamma * gamma * gamma * c * c));
            CHECK_NEAR_ABS((dv_dt - a).norm(), 0.0, 1.0e-12 * std::max(1.0, a.norm()),
                           "the commanded coordinate acceleration comes back out, to rounding");
        }
        const Vec3 along = navigation::ManeuverExecutor::proper_thrust_for(
            Vec3{1.0, 0.0, 0.0}, v, 1.0, propagation::Kinematics::SpecialRelativistic);
        const Vec3 across = navigation::ManeuverExecutor::proper_thrust_for(
            Vec3{0.0, 1.0, 0.0}, v, 1.0, propagation::Kinematics::SpecialRelativistic);
        const double gamma = 1.0 / std::sqrt(1.0 - beta * beta);
        CHECK_NEAR_REL(along.norm(), gamma * gamma * gamma, 1.0e-12, "F = gamma^3 m a along the motion");
        CHECK_NEAR_REL(across.norm(), gamma * gamma, 1.0e-12, "F = gamma^2 m a across it");
    }
    CHECK_NEAR_REL(navigation::ManeuverExecutor::proper_thrust_for(Vec3{0.0, 2.0, 0.0}, Vec3{0.5 * c, 0.0, 0.0}, 3.0,
                                                                  propagation::Kinematics::Newtonian)
                       .norm(),
                   6.0, 1.0e-15, "and F = m a under Newton");
}

TEST(the_arrival_is_an_orbit_where_there_can_be_one_and_a_station_where_there_cannot) {
    const auto spice = sft::load_spice_or_skip();
    auto& provider = *spice.provider;
    const auto t = spice.time->parse("2026-01-02 00:00:00 TDB");
    const Vec3 ship = provider.state(celestial::bodies::earth, t, coordinates::ReferenceFrame::ssb_j2000()).state.position;

    const auto moon = navigation::direct_arrival_geometry(provider, celestial::bodies::moon, t, ship, 100.0e3);
    const double gm = provider.gravitational_parameter(celestial::bodies::moon);
    CHECK(!moon.station);
    CHECK_NEAR_REL(moon.offset.norm(), 1737.4e3 + 100.0e3, 1.0e-3, "on the requested orbit's radius");
    CHECK_NEAR_REL(moon.velocity.norm(), std::sqrt(gm / moon.radius), 1.0e-12, "at circular speed");
    CHECK_NEAR_ABS(dot(moon.offset.normalized(), moon.velocity.normalized()), 0.0, 1.0e-12, "moving along the circle");
    const Vec3 moon_position =
        provider.state(celestial::bodies::moon, t, coordinates::ReferenceFrame::ssb_j2000()).state.position;
    CHECK(dot(moon.offset, ship - moon_position) > 0.0);   // the near side

    // Phobos: a 20 km "orbit" is 31 km from a centre whose Hill sphere is ~17 km.
    const auto phobos = navigation::direct_arrival_geometry(provider, celestial::bodies::phobos, t, ship, 20.0e3);
    INFO("Phobos's Hill radius: " + std::to_string(phobos.hill_radius / 1000.0) + " km");
    CHECK(phobos.station);
    CHECK_EQ(phobos.velocity.norm(), 0.0);
}

TEST(direct_transfers_reach_every_destination_the_directory_accepts) {
    const auto spice = sft::load_spice_or_skip();
    auto& provider = *spice.provider;
    const auto catalog = celestial::BodyCatalog::default_solar_system(provider);
    const auto craft = make_torch();
    const auto epoch = spice.time->parse("2026-01-01 00:00:00 TDB");
    const auto state = leo(provider, catalog, epoch);

    struct Case { celestial::BodyId id; const char* name; double altitude; };
    for (const auto& c : {Case{celestial::bodies::moon, "Moon", 100.0e3}, Case{celestial::bodies::mars, "Mars", 500.0e3},
                          Case{celestial::bodies::venus, "Venus", 500.0e3}, Case{celestial::bodies::mercury, "Mercury", 500.0e3},
                          Case{celestial::bodies::phobos, "Phobos", 20.0e3}, Case{celestial::bodies::deimos, "Deimos", 20.0e3}}) {
        const auto result = plan(state, craft, c.id, c.altitude);
        const auto& m = result.metrics;
        char line[400];
        std::snprintf(line, sizeof line,
                      "%s: %s, %.1f min, peak %.5f c, %.1f kg, miss %.2f m / %.3f m/s, %.1f x %.1f km, e %.5f%s", c.name,
                      std::string{navigation::to_string(result.status)}.c_str(), m.time_of_flight_s / 60.0,
                      m.peak_speed / units::c, m.propellant_required, m.arrival_miss, m.arrival_speed_error,
                      m.predicted_periapsis_altitude / 1000.0, m.predicted_apoapsis_altitude / 1000.0,
                      m.predicted_eccentricity, m.station ? " (station)" : "");
        INFO(line);
        REQUIRE(result.ok());
        CHECK(m.direct);
        CHECK_EQ(result.maneuvers.size(), std::size_t{2});
        CHECK(m.peak_speed < 0.9 * units::c);
        CHECK(m.propellant_required > 0.0 && m.propellant_required < 19000.0);
        CHECK_NEAR_ABS(m.arrival_miss, 0.0, 100.0,
                       "the feedback drives the miss to zero; the bound is the one-second ballistic tail after "
                       "the cutoff plus the integrator, three orders above what was measured");
        CHECK_NEAR_ABS(m.arrival_speed_error, 0.0, 1.0,
                       "with the terminal acceleration brought to zero the last second is worth well under 1 m/s");
        if (m.station) {
            CHECK_NEAR_ABS(m.predicted_periapsis_altitude, c.altitude, 1.0e3,
                           "a station point, read ten minutes after the arrival: still where it was put");
        } else {
            CHECK_NEAR_ABS(m.predicted_periapsis_altitude, c.altitude, 2.0e3,
                           "the requested circular orbit, read one revolution after the arrival, through J2 and "
                           "third bodies; 2 km is the lunar campaign's order, measured here under 0.5 km");
            CHECK_NEAR_ABS(m.predicted_apoapsis_altitude, c.altitude, 2.0e3, "and its apoapsis");
            CHECK(m.predicted_eccentricity < 1.0e-3);
        }
        if (c.id == celestial::bodies::moon) {
            CHECK(m.time_of_flight_s < 3600.0);   // under an hour, against three to five days by Lambert
        }
    }
}
