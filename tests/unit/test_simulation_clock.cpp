// The clock must keep wall time, coordinate time and proper time apart, and time
// warp must never be able to influence the integrator's step size (rule 21).

#include "core/simulation/simulation_clock.hpp"
#include "tests/support/test_harness.hpp"

#include <stdexcept>

using sf::simulation::SimulationClock;
using sf::time::CoordinateTime;
using sf::time::Duration;

TEST(clock_starts_at_its_epoch_with_all_clocks_aligned) {
    const auto epoch = CoordinateTime::from_seconds_since_j2000(820497600.0);
    const SimulationClock clock{epoch};

    CHECK(clock.coordinate_time() == epoch);
    CHECK_EQ(clock.elapsed_coordinate().seconds(), 0.0);
    CHECK_EQ(clock.proper_time().seconds(), 0.0);
    CHECK_EQ(clock.wall_time().seconds(), 0.0);
    CHECK_EQ(clock.time_warp(), 1.0);
}

TEST(time_warp_scales_wall_time_into_coordinate_time) {
    SimulationClock clock{CoordinateTime::j2000()};

    CHECK_EQ(clock.coordinate_advance_for(Duration::seconds(1.0)).seconds(), 1.0);

    clock.set_time_warp(1000.0);
    CHECK_EQ(clock.coordinate_advance_for(Duration::seconds(1.0)).seconds(), 1000.0);
    CHECK_EQ(clock.target_for(Duration::seconds(0.016)).seconds_since_j2000(), 16.0);

    for (const double level : SimulationClock::warp_levels()) {
        clock.set_time_warp(level);
        CHECK_EQ(clock.coordinate_advance_for(Duration::seconds(2.0)).seconds(), 2.0 * level);
    }
}

TEST(time_warp_rejects_nonsense) {
    SimulationClock clock{CoordinateTime::j2000()};
    CHECK_THROWS_AS(clock.set_time_warp(0.0), std::invalid_argument);
    CHECK_THROWS_AS(clock.set_time_warp(-10.0), std::invalid_argument);
    CHECK_EQ(clock.time_warp(), 1.0);
}

TEST(commit_records_what_the_propagator_actually_did) {
    const auto epoch = CoordinateTime::j2000();
    SimulationClock clock{epoch};
    clock.set_time_warp(100.0);

    // The propagator was asked for 100 s of coordinate time but stopped at 60 s.
    // The clock records the truth, not the request: otherwise the simulation
    // would drift away from the state the propagator actually produced.
    const auto reached = epoch + Duration::seconds(60.0);
    clock.commit(reached, Duration::seconds(59.999999), Duration::seconds(1.0));

    CHECK(clock.coordinate_time() == reached);
    CHECK_EQ(clock.elapsed_coordinate().seconds(), 60.0);
    CHECK_NEAR_ABS(clock.proper_time().seconds(), 59.999999, 0.0,
                   "the clock only stores what it was given; no arithmetic happens to it");
    CHECK_NEAR_ABS(clock.clock_difference().seconds(), 1.0e-6, 1.0e-14,
                   "60.0 - 59.999999 is catastrophic cancellation: 59.999999 is not exactly "
                   "representable, and one ulp at 60 s is 7.1e-15 s. The difference of two "
                   "~60 s clocks therefore carries an absolute error of a few times 1e-15 s, "
                   "which is 2.5e-9 RELATIVE to the 1 microsecond answer. This is precisely why "
                   "the cockpit's TIME DIFFERENCE readout must be computed from a stored "
                   "difference once proper time actually diverges (Milestone 4), not by "
                   "subtracting two large clocks");
    CHECK_EQ(clock.wall_time().seconds(), 1.0);
}

TEST(reset_moves_the_epoch_and_clears_proper_time) {
    SimulationClock clock{CoordinateTime::j2000()};
    clock.commit(CoordinateTime::from_seconds_since_j2000(500.0), Duration::seconds(500.0),
                 Duration::seconds(5.0));

    const auto new_epoch = CoordinateTime::from_seconds_since_j2000(1.0e6);
    clock.reset_to(new_epoch);

    CHECK(clock.epoch() == new_epoch);
    CHECK(clock.coordinate_time() == new_epoch);
    CHECK_EQ(clock.elapsed_coordinate().seconds(), 0.0);
    CHECK_EQ(clock.proper_time().seconds(), 0.0);
}
