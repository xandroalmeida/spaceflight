// The Milestone 8 presentation tests (rule 108), ported from the suite
// they replaced (the Godot suite test_m8.gd, removed with the engine) check for check.
//
// What they cover is what M8 added on the presentation side: the body directory
// that reaches the interface, Mars as a selectable destination, the solar-system
// map, the formatting of interplanetary durations and distances, and the
// reference body. The physics is verified by the scientific suite and this file
// does not repeat a single sum of it.

#include "app/presentation/format.hpp"
#include "app/presentation/instruments/orbit_map.hpp"
#include "app/session/flight_session.hpp"
#include "core/ephemeris/spice_kernel_set.hpp"
#include "tests/support/test_harness.hpp"

#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>

using namespace sf;
using app::Vec3;

namespace {

app::FlightSession& session() {
    // Deliberately never destroyed: the session's destructor takes the CSPICE
    // mutex, a function-local static created AFTER this one -- and so destroyed
    // BEFORE it at exit. Leaking the one object is the order that works.
    static app::FlightSession* instance = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        auto* s = new app::FlightSession();
        s->set_diagnostic_sink([](const std::string&, bool) {});
        if (s->configure(ephemeris::SpiceKernelSet::default_directory(), "2026-01-01T00:00:00")) {
            s->start_circular_orbit(400000.0, 51.6);
            s->align_attitude_to_flight(25.0);
            s->set_render_scale(1.0e-6);
            s->advance(0.016);
            instance = s;
        } else {
            delete s;
        }
    }
    if (instance == nullptr) {
        throw sft::TestSkipped{"no SPICE kernels (run scripts/fetch_kernels.sh)"};
    }
    return *instance;
}

const app::BodyDirectoryEntry* entry(const std::vector<app::BodyDirectoryEntry>& directory, const std::string& name) {
    for (const auto& e : directory) {
        if (e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

// Waits for the worker, as a frame loop would, up to `limit` seconds.
double wait_for_search(app::FlightSession& sim, double limit) {
    const auto started = std::chrono::steady_clock::now();
    while (sim.is_planning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (waited > limit) {
            break;
        }
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

}  // namespace

TEST(body_directory_rule_19_one_table) {
    const auto directory = session().body_directory();
    INFO(app::fmt::format("the directory holds the solar system (%zu bodies)", directory.size()));
    CHECK(directory.size() >= 20);
    for (const char* name : {"Sun", "Mercury", "Venus", "Earth", "Moon", "Mars", "Jupiter", "Saturn", "Uranus",
                             "Neptune"}) {
        INFO(std::string{name} + " is in the directory");
        CHECK(entry(directory, name) != nullptr);
    }
    const auto* mars = entry(directory, "Mars");
    REQUIRE(mars != nullptr);
    CHECK_EQ(mars->naif_id, 499);   // Mars is body 499 and not barycentre 4
    CHECK(mars->radius_m > 3.3e6 && mars->radius_m < 3.5e6);   // from the PCK
    CHECK(mars->has_ephemeris);
    CHECK(!mars->position_substituted);   // Mars's own position, not the barycentre's
}

TEST(mars_as_a_target_rules_16_21) {
    auto& sim = session();
    const auto targets = sim.selectable_targets();
    CHECK(std::find(targets.begin(), targets.end(), "Mars") != targets.end());
    CHECK(std::find(targets.begin(), targets.end(), "Moon") != targets.end());
    CHECK(sim.set_target_body("Mars"));
    CHECK_EQ(sim.target_body(), std::string{"Mars"});
    // Mars is between 0.37 and 2.7 AU from the Earth. Anything outside means the
    // distance was measured against the wrong body.
    const double distance = sim.snapshot().target_distance_m;
    INFO(app::fmt::format("the distance to Mars is interplanetary (%.3f AU)", distance / app::fmt::AU_M));
    CHECK(distance > 5.0e10 && distance < 4.2e11);
    CHECK(sim.set_target_body("Moon"));
}

TEST(hierarchy_rule_20) {
    const auto directory = session().body_directory();
    CHECK_EQ(entry(directory, "Moon")->parent, 399);
    CHECK_EQ(entry(directory, "Earth")->parent, 10);
    CHECK_EQ(entry(directory, "Phobos")->parent, 499);
    CHECK_EQ(entry(directory, "Io")->parent, 599);
    CHECK_EQ(entry(directory, "Titan")->parent_name, std::string{"Saturn"});   // the parent's name, for the menu
}

TEST(catalogue_honesty_rule_21) {
    const auto directory = session().body_directory();
    // Jupiter is drawn at its system's BARYCENTRE, because the planet's own SPK
    // is not distributed with the project. That is enough to draw and not enough
    // to be the centre of a capture orbit -- and the catalogue has to say both.
    const auto* jupiter = entry(directory, "Jupiter");
    REQUIRE(jupiter != nullptr);
    if (jupiter->position_substituted) {
        CHECK(!jupiter->can_be_destination);
        CHECK_EQ(jupiter->mission_support, std::string{"observation"});
    }
    CHECK_EQ(entry(directory, "Moon")->mission_support, std::string{"qualified"});
    CHECK(entry(directory, "Mars")->can_be_destination);
}

TEST(interplanetary_formatting_rules_96_98) {
    // 92 days and 14 hours, rule 96's example.
    const std::string three_months = app::fmt::duration(92.0 * 86400.0 + 14.0 * 3600.0);
    INFO("a three-month duration reads: " + three_months);
    CHECK(three_months.find("92") != std::string::npos);
    CHECK_EQ(three_months, std::string{"92d 14h"});
    const std::string far = app::fmt::distance(8.42e10);
    INFO("84 million km fits a display: " + far);
    CHECK(far.size() < 20);
    // ⚠️ An invalid format was not an error in the Godot formatter: it printed
    // LITERALLY, and the first capture of the solar-system map had "%.3g AU" on
    // every ring. What is checked is the absence of the format character in the
    // OUTPUT, the only place that defect shows.
    for (const double au : {0.05, 0.387, 1.0, 1.524, 5.2, 30.1}) {
        const std::string label = app::OrbitMap::au_label(au);
        INFO("the label of " + app::fmt::format("%.3f", au) + " AU is text: \"" + label + "\"");
        CHECK(label.find('%') == std::string::npos);
    }
}

TEST(solar_system_map_rules_26_31) {
    const auto map = session().system_map();
    REQUIRE(map.valid);
    // Rule 31: every layer declares its frame. Here it is checkable: the payload
    // names the frame.
    CHECK(map.frame.find("Sun") != std::string::npos);
    CHECK_EQ(map.centre, std::string{"Sun"});
    CHECK(map.bodies.size() >= 8);

    const auto has = [&](const std::string& name) {
        return std::any_of(map.bodies.begin(), map.bodies.end(), [&](const auto& b) { return b.name == name; });
    };
    CHECK(has("Sun"));
    CHECK(has("Earth"));
    CHECK(has("Mars"));
    // Rule 27: moons stay off the heliocentric zoom.
    CHECK(!has("Moon"));
    CHECK(!has("Phobos"));

    // Heliocentric metres: the Earth is one AU from the centre. In scene units,
    // or relative to the ship, this would be off by orders of magnitude.
    for (const auto& body : map.bodies) {
        if (body.name == "Earth") {
            const double r = body.position.norm();
            INFO(app::fmt::format("the Earth is %.3f AU from the Sun", r / app::fmt::AU_M));
            CHECK(r > 1.4e11 && r < 1.6e11);
        }
        if (body.name == "Sun") {
            CHECK(body.position.norm() < 1.0e3);
        }
    }
    CHECK(map.ship_position.norm() > 1.4e11 && map.ship_position.norm() < 1.6e11);
}

TEST(planetary_paths_sampled_from_the_ephemeris_rule_29) {
    const auto paths = session().system_orbit_paths(48);
    CHECK(paths.count("Earth") == 1);
    CHECK(paths.count("Mars") == 1);
    CHECK(paths.count("Moon") == 0);   // moons get no heliocentric path
    if (paths.count("Earth") == 1) {
        const auto& row = paths.at("Earth");
        CHECK(row.path.size() >= 24);
        // A terrestrial year, read from the osculating ellipses themselves and not
        // from a table of periods.
        const double period_days = row.period_s / 86400.0;
        INFO(app::fmt::format("the Earth's period comes out of the ephemeris: %.2f days", period_days));
        CHECK(period_days > 360.0 && period_days < 370.0);
        // The path closes.
        const double closure = (row.path.front() - row.path.back()).norm();
        CHECK(closure < 0.05 * row.path.front().norm());
    }
    if (paths.count("Neptune") == 1) {
        // Neptune's year is 165 years and de440s covers 1849-2150. The field says
        // whether it was clipped instead of extrapolating in silence.
        INFO(std::string{"Neptune's path clipped: "} + (paths.at("Neptune").clipped ? "yes" : "no"));
        CHECK(paths.at("Neptune").path.size() >= 3);
    }
}

TEST(asynchronous_search_rules_48_49_120) {
    auto& sim = session();
    CHECK(!sim.is_planning());
    // Mars, because it is the expensive search: the one a blocking planner would
    // turn into a minute of frozen frames.
    CHECK(sim.start_planning("Mars", 500.0, 500.0, 2.0));
    CHECK(sim.is_planning());

    // The frame keeps going. The assertion that matters: if planning blocked,
    // this line would only be reached at the end of the search.
    int advanced = 0;
    for (int i = 0; i < 30; ++i) {
        sim.advance(0.016);
        ++advanced;
    }
    CHECK_EQ(advanced, 30);
    const auto progress = sim.planning_progress();
    CHECK(progress.present && progress.running);

    sim.cancel_planning();
    CHECK(sim.planning_progress().cancelled);
    // The search stops between candidates, never inside one, so this waits --
    // and the point is that it STOPS instead of being left running.
    const double waited = wait_for_search(sim, 120.0);
    INFO(app::fmt::format("the worker stopped in %.2f s", waited));
    CHECK(!sim.is_planning());
    const auto plan = sim.collect_plan();
    CHECK(plan.cancelled || !plan.valid);
    CHECK(!sim.has_planned_transfer());
}

TEST(the_planned_arc_in_the_maps_frame_rule_31) {
    // The planned arc reaches the system map in heliocentric metres, each sample
    // rebuilt by adding the ORIGIN's position at THAT sample's epoch. The test is
    // geometric: the arc has to START at the ship and END at the destination, and
    // those two conditions catch any frame or epoch error. The Moon plans in
    // seconds instead of a minute.
    auto& sim = session();
    sim.set_target_body("Moon");
    REQUIRE(sim.start_planning("Moon", 100.0, 100.0, 2.0));
    const double waited = wait_for_search(sim, 180.0);
    const auto plan = sim.collect_plan();
    if (!plan.valid) {
        SFT_FAIL("the lunar plan was found (" + sim.last_error() + ")");
        return;
    }
    INFO(app::fmt::format("lunar plan in %.1f s", waited));

    const auto map = sim.system_map();
    const auto& arc = map.planned_trajectory;
    CHECK(arc.size() > 32);
    REQUIRE(arc.size() >= 2);
    // ⚠️ Against the ANCHORS, and against the anchor of the RIGHT EPOCH. The test
    // was wrong twice in Godot for the same reason, and both times it reported
    // the correct code as broken: two positions of the same body at two epochs
    // are never comparable here without saying which epochs.
    REQUIRE(map.origin_at_departure.has_value());
    REQUIRE(map.destination_at_arrival.has_value());
    REQUIRE(map.destination_at_trajectory_end.has_value());

    const double start_gap = (arc.front() - *map.origin_at_departure).norm();
    INFO(app::fmt::format("the arc STARTS near the Earth at departure (%.0f km)", start_gap / 1000.0));
    CHECK(start_gap < 5.0e7);
    const double end_gap = (arc.back() - *map.destination_at_trajectory_end).norm();
    INFO(app::fmt::format("the arc ENDS near the Moon at the end of the arc (%.0f km)", end_gap / 1000.0));
    CHECK(end_gap < 2.0e7);
    sim.clear_plan();
}

TEST(choosing_an_alternative_rule_42) {
    // Rule 42 and step 8 of the vertical slice: the table of alternatives is a
    // CHOICE and not a report. Choosing alternative i must return geometry i and
    // not the one the cost function preferred.
    auto& sim = session();
    REQUIRE(sim.start_planning("Moon", 100.0, 100.0, 2.0));
    wait_for_search(sim, 180.0);
    const auto first = sim.collect_plan();
    if (!first.valid) {
        SFT_FAIL("the lunar search found a plan");
        return;
    }
    const auto alternatives = sim.plan_alternatives();
    INFO(app::fmt::format("the search flew %zu geometries", alternatives.size()));
    CHECK(alternatives.size() >= 2);

    // One that is NOT the one the planner chose. If it were the same, the test
    // would pass without proving anything.
    int wanted = -1;
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        if (alternatives[i].feasible &&
            std::abs(alternatives[i].time_of_flight_days - first.time_of_flight_days) > 0.1) {
            wanted = static_cast<int>(i);
            break;
        }
    }
    if (wanted < 0) {
        INFO("only one feasible geometry at this epoch -- nothing to choose");
        sim.clear_plan();
        return;
    }
    const auto chosen = alternatives[static_cast<std::size_t>(wanted)];
    CHECK(sim.start_planning_alternative(wanted));
    const double waited = wait_for_search(sim, 180.0);
    const auto second = sim.collect_plan();
    INFO(app::fmt::format("the pinned geometry replans in %.1f s", waited));
    REQUIRE(second.valid);
    // It is the geometry ASKED FOR and not the one the cost preferred.
    CHECK_NEAR_ABS(second.time_of_flight_days, chosen.time_of_flight_days, 0.01, "the chosen geometry's flight time");
    CHECK(std::abs(second.time_of_flight_days - first.time_of_flight_days) > 0.1);
    sim.clear_plan();
}
