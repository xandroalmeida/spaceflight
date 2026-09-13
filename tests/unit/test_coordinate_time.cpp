// The two-part time representation exists for one reason: to survive Milestone 4
// (see docs/architecture/coordinate-system.md section 5).  These tests pin down
// the property it was built for.

#include "core/time/coordinate_time.hpp"
#include "core/units/constants.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>

using sf::time::CoordinateTime;
using sf::time::Duration;

TEST(coordinate_time_j2000_is_the_origin) {
    const auto t = CoordinateTime::j2000();
    CHECK_EQ(t.seconds_since_j2000(), 0.0);
    CHECK_NEAR_ABS(t.julian_date_tdb(), sf::units::julian_date_j2000, 0.0,
                   "J2000 is defined as JD 2451545.0 TDB; both sides are exactly representable");
}

TEST(coordinate_time_accumulates_small_steps_without_losing_them) {
    // A 2026 epoch: ~8.2e8 s past J2000, where a single double resolves only
    // ~1.9e-7 s.  We add 1e6 steps of 1 ms and expect exactly 1000 s.
    const auto epoch = CoordinateTime::from_seconds_since_j2000(820497600.0);
    const int steps = 1'000'000;
    const double step = 1.0e-3;

    auto t = epoch;
    for (int i = 0; i < steps; ++i) {
        t += Duration::seconds(step);
    }

    const double elapsed = (t - epoch).seconds();
    CHECK_NEAR_ABS(elapsed, 1000.0, 1.0e-9,
                   "1e6 compensated additions; the residual is bounded by ~1e6 * eps * 1000 s "
                   "= 2e-7 s in the worst case without compensation, and by a few ulp of 1000 s "
                   "(2e-13 s) with it. 1e-9 s is a deliberately loose bound that still fails "
                   "loudly if the compensation is ever removed");

    // The naive alternative, measured rather than asserted, so the regression is
    // visible if anyone ever proposes "just use a double".
    double naive = 820497600.0;
    for (int i = 0; i < steps; ++i) {
        naive += step;
    }
    std::ostringstream os;
    os << "naive single-double accumulation error over the same sum: "
       << std::abs((naive - 820497600.0) - 1000.0) << " s";
    INFO(os.str());
}

TEST(coordinate_time_difference_is_exact_between_distant_epochs) {
    const auto a = CoordinateTime::from_seconds_since_j2000(820497600.0);
    const auto b = a + Duration::seconds(1.0e-6);
    CHECK_NEAR_REL((b - a).seconds(), 1.0e-6, 1.0e-12,
                   "a microsecond separation at an epoch where a single double resolves only "
                   "1.9e-7 s: this is exactly the case the two-part representation exists for");

    const auto far = CoordinateTime::from_seconds_since_j2000(-1.0e9);
    CHECK_NEAR_REL((a - far).seconds(), 1820497600.0, 1.0e-15,
                   "difference of two exactly representable integers; only the final subtraction rounds");
}

TEST(coordinate_time_normalization_keeps_the_fraction_small) {
    const auto t = CoordinateTime::from_parts(100.0, 0.75);
    CHECK_EQ(t.whole_seconds(), 101.0);
    CHECK_NEAR_ABS(t.fractional_seconds(), -0.25, 0.0,
                   "0.75 and 0.25 are exact binary fractions; the carry is exact");
    CHECK_NEAR_ABS(t.seconds_since_j2000(), 100.75, 0.0,
                   "sum of two exactly representable values");
    CHECK(std::abs(t.fractional_seconds()) <= 0.5);
}

TEST(coordinate_time_ordering_and_equality) {
    const auto a = CoordinateTime::from_seconds_since_j2000(100.0);
    const auto b = a + Duration::seconds(1.0e-12);
    CHECK(a < b);
    CHECK(b > a);
    CHECK(a == CoordinateTime::from_seconds_since_j2000(100.0));
    CHECK(a <= a);
    CHECK(!(a > a));
}

TEST(coordinate_time_julian_date_round_trip) {
    const double jd = 2461041.5;  // 2026-01-01 00:00 TDB
    const auto t = CoordinateTime::from_julian_date_tdb(jd);
    CHECK_NEAR_ABS(t.seconds_since_j2000(), 820497600.0, 0.0,
                   "(2461041.5 - 2451545.0) * 86400 = 820497600 exactly: both the day count and "
                   "the product are exactly representable in binary floating point");
    CHECK_NEAR_REL(t.julian_date_tdb(), jd, 1.0e-15,
                   "round trip through one division by 86400; a couple of ulp of 2.46e6");
}

TEST(duration_conversions_are_exact_for_si_units) {
    CHECK_EQ(Duration::days(1.0).seconds(), 86400.0);
    CHECK_EQ(Duration::hours(1.0).seconds(), 3600.0);
    CHECK_EQ(Duration::minutes(1.5).seconds(), 90.0);
    CHECK_EQ((Duration::seconds(10.0) * 3.0).seconds(), 30.0);
    CHECK_EQ(Duration::seconds(10.0) / Duration::seconds(4.0), 2.5);
    CHECK(Duration::seconds(-5.0).abs().seconds() == 5.0);
    CHECK(Duration::seconds(-5.0).sign() == -1);
}
