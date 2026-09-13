// Time scales are where silent errors of tens of seconds come from.  Each
// relation checked here is either exact by definition or a published constant.

#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>

using namespace sf;

TEST(the_j2000_epoch_is_where_it_is_defined_to_be) {
    const auto fixture = sft::load_spice_or_skip();

    const auto t = fixture.time->parse("2000-01-01 12:00:00 TDB");
    CHECK_NEAR_ABS(t.seconds_since_j2000(), 0.0, 0.0,
                   "J2000 is DEFINED as 2000-01-01 12:00:00 TDB; the toolkit returns exactly 0.0, "
                   "so no tolerance is warranted");

    const auto jd = fixture.time->parse("JD 2451545.0 TDB");
    CHECK_NEAR_ABS(jd.seconds_since_j2000(), 0.0, 1.0e-6,
                   "the same instant expressed as a Julian date; the conversion multiplies a "
                   "difference of two ~2.45e6 values by 86400, costing ~5e-7 s of resolution");
}

TEST(tt_minus_tai_is_exact_by_definition) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse("2026-06-15T12:00:00");

    const double tt = fixture.time->seconds_in_scale(t, time::TimeScale::TT);
    const double tai = fixture.time->seconds_in_scale(t, time::TimeScale::TAI);

    CHECK_NEAR_ABS(tt - tai, 32.184, 1.0e-6,
                   "TT = TAI + 32.184 s is exact by definition (IAU 1991), so the true answer has "
                   "no uncertainty at all. What the tolerance covers is arithmetic: both scales "
                   "are returned as seconds past their own J2000 epoch, ~8.3e8 s, where one ulp "
                   "is 1.2e-7 s. The measured residual is 1.5e-8 s. A bound of 1e-6 s is ~8 ulp "
                   "and would still catch a missing or doubled offset, which would be 32 s");
}

TEST(tdb_minus_tt_stays_within_its_known_amplitude) {
    const auto fixture = sft::load_spice_or_skip();

    double worst = 0.0;
    for (int month = 0; month < 12; ++month) {
        const std::string date = "2026-" + std::string(month < 9 ? "0" : "") +
                                 std::to_string(month + 1) + "-01T00:00:00";
        const auto t = fixture.time->parse(date);
        const double difference =
            t.seconds_since_j2000() - fixture.time->seconds_in_scale(t, time::TimeScale::TT);
        worst = std::max(worst, std::abs(difference));
    }

    CHECK(worst > 1.0e-4);
    CHECK_NEAR_ABS(worst, 0.0, 2.0e-3,
                   "TDB - TT is periodic with amplitude 1.657 ms, dominated by the annual term of "
                   "the Earth's eccentric orbit; 2 ms bounds it with a small margin. A value of "
                   "zero would mean the two scales had been conflated, which is why the test also "
                   "requires it to be above 0.1 ms");
}

TEST(utc_offset_matches_the_leap_second_count) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse("2026-01-01T00:00:00");

    const double utc = fixture.time->seconds_in_scale(t, time::TimeScale::UTC);
    const double offset = t.seconds_since_j2000() - utc;

    CHECK_NEAR_ABS(offset, 69.184, 2.0e-3,
                   "TDB - UTC = 32.184 s (TT-TAI) + 37 s (leap seconds as of 2017-01-01, none "
                   "added since) + the periodic TDB-TT term of at most 1.7 ms");
}

TEST(utc_round_trips_through_the_toolkit) {
    const auto fixture = sft::load_spice_or_skip();

    for (const char* text : {"2026-01-01T00:00:00.000000", "1999-12-31T23:59:59.000000",
                             "2049-07-04T18:30:15.500000"}) {
        const auto t = fixture.time->parse(text);
        const std::string formatted = fixture.time->to_utc_string(t, 6);
        CHECK_EQ(formatted, std::string{text});
    }
}

TEST(a_bare_timestamp_is_interpreted_as_utc_not_tdb) {
    const auto fixture = sft::load_spice_or_skip();

    const auto bare = fixture.time->parse("2026-01-01T00:00:00");
    const auto tdb = fixture.time->parse("2026-01-01 00:00:00 TDB");

    CHECK_NEAR_ABS((bare - tdb).seconds(), 69.184, 2.0e-3,
                   "the two spellings must differ by exactly the TDB-UTC offset; if they did not, "
                   "every scenario file would be silently 69 s off");
}
