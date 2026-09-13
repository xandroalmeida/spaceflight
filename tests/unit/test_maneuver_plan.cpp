// A plan is data with invariants.  These are the invariants.

#include "core/navigation/maneuver.hpp"
#include "tests/support/test_harness.hpp"

#include <stdexcept>

using namespace sf;
using sf::navigation::GuidanceMode;
using sf::navigation::Maneuver;
using sf::navigation::ManeuverPlan;

namespace {

Maneuver burn(const char* name, double start, double duration, double throttle = 1.0) {
    Maneuver m{};
    m.name = name;
    m.ignition = time::CoordinateTime::from_seconds_since_j2000(start);
    m.duration = time::Duration::seconds(duration);
    m.throttle = throttle;
    m.guidance = GuidanceMode::Prograde;
    m.reference = celestial::bodies::earth;
    return m;
}

time::CoordinateTime at(double seconds) {
    return time::CoordinateTime::from_seconds_since_j2000(seconds);
}

}  // namespace

TEST(a_maneuver_validates_its_own_fields) {
    CHECK_THROWS_AS(burn("zero", 0.0, 0.0).validate(), std::invalid_argument);
    CHECK_THROWS_AS(burn("negative", 0.0, -10.0).validate(), std::invalid_argument);
    CHECK_THROWS_AS(burn("hot", 0.0, 10.0, 1.5).validate(), std::invalid_argument);
    CHECK_THROWS_AS(burn("cold", 0.0, 10.0, -0.1).validate(), std::invalid_argument);

    Maneuver inertial = burn("inertial", 0.0, 10.0);
    inertial.guidance = GuidanceMode::Inertial;
    inertial.inertial_direction = math::Vec3{};
    CHECK_THROWS_AS(inertial.validate(), std::invalid_argument);

    inertial.inertial_direction = math::Vec3{0.0, 1.0, 0.0};
    inertial.validate();  // must not throw
    CHECK(true);
}

TEST(the_plan_keeps_itself_sorted_and_refuses_overlaps) {
    ManeuverPlan plan;
    plan.add(burn("third", 300.0, 50.0));
    plan.add(burn("first", 100.0, 50.0));
    plan.add(burn("second", 200.0, 50.0));

    CHECK_EQ(plan.size(), std::size_t{3});
    CHECK_EQ(plan.maneuvers()[0].name, std::string{"first"});
    CHECK_EQ(plan.maneuvers()[1].name, std::string{"second"});
    CHECK_EQ(plan.maneuvers()[2].name, std::string{"third"});

    // One engine cannot burn in two directions at once.
    CHECK_THROWS_AS(plan.add(burn("clash", 120.0, 50.0)), std::invalid_argument);
    CHECK_THROWS_AS(plan.add(burn("engulfs", 90.0, 300.0)), std::invalid_argument);
    CHECK_EQ(plan.size(), std::size_t{3});

    // Touching exactly is fine: the interval is half-open.
    plan.add(burn("adjacent", 150.0, 50.0));
    CHECK_EQ(plan.size(), std::size_t{4});
}

TEST(active_at_uses_a_half_open_interval) {
    ManeuverPlan plan;
    plan.add(burn("a", 100.0, 100.0));   // [100, 200)
    plan.add(burn("b", 200.0, 100.0));   // [200, 300)

    CHECK(plan.active_at(at(99.0)) == nullptr);
    REQUIRE(plan.active_at(at(100.0)) != nullptr);
    CHECK_EQ(plan.active_at(at(100.0))->name, std::string{"a"});
    CHECK_EQ(plan.active_at(at(199.999))->name, std::string{"a"});

    // The instant where they meet belongs to exactly one of them; if it belonged
    // to both, the executor would apply two burns at a step boundary.
    CHECK_EQ(plan.active_at(at(200.0))->name, std::string{"b"});
    CHECK(plan.active_at(at(300.0)) == nullptr);
}

TEST(switch_times_are_what_the_mission_runner_breaks_the_integration_at) {
    ManeuverPlan plan;
    plan.add(burn("a", 100.0, 100.0));
    plan.add(burn("b", 500.0, 50.0));

    const auto times = plan.switch_times(at(0.0), at(1000.0));
    REQUIRE(times.size() == std::size_t{4});
    CHECK_EQ(times[0].seconds_since_j2000(), 100.0);
    CHECK_EQ(times[1].seconds_since_j2000(), 200.0);
    CHECK_EQ(times[2].seconds_since_j2000(), 500.0);
    CHECK_EQ(times[3].seconds_since_j2000(), 550.0);

    // Endpoints of the interval are not switch points: the runner already stops
    // there.  Only interior discontinuities matter.
    const auto clipped = plan.switch_times(at(100.0), at(200.0));
    CHECK(clipped.empty());

    const auto partial = plan.switch_times(at(150.0), at(520.0));
    REQUIRE(partial.size() == std::size_t{2});
    CHECK_EQ(partial[0].seconds_since_j2000(), 200.0);
    CHECK_EQ(partial[1].seconds_since_j2000(), 500.0);

    // Backwards propagation gets them in the order it will meet them.
    const auto reverse = plan.switch_times(at(1000.0), at(0.0));
    REQUIRE(reverse.size() == std::size_t{4});
    CHECK_EQ(reverse[0].seconds_since_j2000(), 550.0);
    CHECK_EQ(reverse[3].seconds_since_j2000(), 100.0);
}

TEST(guidance_modes_round_trip_through_their_names) {
    for (const auto mode : {GuidanceMode::Inertial, GuidanceMode::Prograde, GuidanceMode::Retrograde,
                            GuidanceMode::Normal, GuidanceMode::AntiNormal, GuidanceMode::RadialOut,
                            GuidanceMode::RadialIn}) {
        const auto parsed = navigation::guidance_from_string(navigation::to_string(mode));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == mode);
    }

    CHECK(navigation::guidance_from_string("prograde").has_value());
    CHECK(navigation::guidance_from_string("PROGRADE").has_value());
    CHECK(!navigation::guidance_from_string("sideways").has_value());
}
