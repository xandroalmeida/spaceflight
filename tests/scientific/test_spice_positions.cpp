// Validation of the ephemeris layer against data produced outside this project.
//
// The reference states below were retrieved from JPL Horizons (which serves
// DE441) on 2026-09-13 for epoch 2026-01-01 00:00:00.000 TDB = JD 2461041.5,
// centre "@0" (solar system barycentre), reference frame ICRF, units km and
// km/s.  We integrate against de440s (DE440).  DE440 and DE441 are separate fits
// and are NOT expected to agree exactly; the measured differences are quoted in
// each tolerance below.
//
// Reproduce with:
//   curl -G https://ssd.jpl.nasa.gov/api/horizons.api \
//     --data-urlencode "format=text" --data-urlencode "COMMAND='399'" \
//     --data-urlencode "EPHEM_TYPE='VECTORS'" --data-urlencode "CENTER='@0'" \
//     --data-urlencode "START_TIME='2026-01-01 00:00'" --data-urlencode "STOP_TIME='2026-01-01 00:01'" \
//     --data-urlencode "STEP_SIZE='1 m'" --data-urlencode "OUT_UNITS='KM-S'" \
//     --data-urlencode "REF_PLANE='FRAME'" --data-urlencode "REF_SYSTEM='ICRF'" \
//     --data-urlencode "VEC_TABLE='2'" --data-urlencode "CSV_FORMAT='YES'"

#include "core/celestial/body_catalog.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <sstream>

using namespace sf;
using sf::math::Vec3;

namespace {

struct ReferenceState {
    const char* name;
    celestial::BodyId body;
    Vec3 position;  // [m], SSB/ICRF
    Vec3 velocity;  // [m/s]
    double measured_position_difference;  // [m], DE440 vs DE441, measured 2026-09-13
};

const ReferenceState kHorizons2026[] = {
    {"Sun", celestial::bodies::sun,
     Vec3{-4.588639674035421e8, -7.673041037601740e8, -3.111955872581376e8},
     Vec3{1.242505794360611e1, 3.760600763841905e-1, -9.339934521605677e-2},
     0.0},
    {"Earth", celestial::bodies::earth,
     Vec3{-2.653100241556548e10, 1.320643995462592e11, 5.726870337495859e10},
     Vec3{-2.977650610770464e4, -4.950768712267514e3, -2.146229769311948e3},
     0.030},
    {"Moon", celestial::bodies::moon,
     Vec3{-2.638667668807356e10, 1.323539837038041e11, 5.742886229796027e10},
     Vec3{-3.078082024537686e4, -4.566854102149870e3, -1.973694872363547e3},
     2.421},
};

constexpr const char* kReferenceEpoch = "2026-01-01 00:00:00 TDB";

}  // namespace

TEST(barycentric_states_match_jpl_horizons) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse(kReferenceEpoch);
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();

    for (const auto& ref : kHorizons2026) {
        const auto state = fixture.provider->state(ref.body, t, frame);

        const double position_error = (state.state.position - ref.position).norm();
        const double velocity_error = (state.state.velocity - ref.velocity).norm();

        std::ostringstream os;
        os << ref.name << ": |dr| = " << position_error << " m, |dv| = " << velocity_error
           << " m/s, |r| = " << state.state.position.norm() << " m";
        INFO(os.str());

        CHECK_NEAR_ABS(position_error, 0.0, 25.0,
                       "DE440 (de440s.bsp) vs Horizons/DE441 at this epoch, measured: Sun 0.000 m, "
                       "Earth 0.030 m, Moon 2.421 m. These are genuine differences between two "
                       "JPL fits, not integration error. 25 m is ~10x the worst measured "
                       "difference: tight enough to catch a frame, unit or epoch mistake (which "
                       "would show up as 1e3 m or more), loose enough to survive an ephemeris "
                       "release. Relative to the 1.5e11 m distances involved, this is 1.7e-10");

        CHECK_NEAR_ABS(velocity_error, 0.0, 1.0e-4,
                       "the same comparison for velocity; measured: Sun 4e-15, Earth 8e-8, "
                       "Moon 7e-6 m/s. 1e-4 m/s keeps ~15x margin over the worst case and is "
                       "3e-9 relative to the 3e4 m/s orbital speed");
    }
}

TEST(the_earth_moon_barycentre_is_where_the_masses_say_it_is) {
    // This test uses no external reference at all: it checks that the positions
    // in the SPK and the masses in the PCK describe the same physical system.
    // If someone ever pairs a barycentre position with a planet GM, this fails.
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse(kReferenceEpoch);
    const auto emb = coordinates::ReferenceFrame::centered_on(celestial::bodies::earth_moon_barycenter);

    const Vec3 r_earth = fixture.provider->state(celestial::bodies::earth, t, emb).state.position;
    const Vec3 r_moon = fixture.provider->state(celestial::bodies::moon, t, emb).state.position;

    const double gm_earth = fixture.provider->gravitational_parameter(celestial::bodies::earth);
    const double gm_moon = fixture.provider->gravitational_parameter(celestial::bodies::moon);

    const Vec3 weighted = r_earth * gm_earth + r_moon * gm_moon;
    const double scale = gm_earth * r_earth.norm();

    std::ostringstream os;
    os << "Earth offset from EMB: " << r_earth.norm() << " m, Moon: " << r_moon.norm()
       << " m, residual/scale = " << weighted.norm() / scale;
    INFO(os.str());

    CHECK_NEAR_ABS(weighted.norm() / scale, 0.0, 1.0e-9,
                   "the definition of a barycentre, m1*r1 + m2*r2 = 0, evaluated with the GMs "
                   "from gm_de440.tpc and the positions from de440s.bsp. These come from the same "
                   "JPL solution, so the residual is limited only by the rounding of the "
                   "published constants (~1e-11 relative). 1e-9 is two orders of margin");
}

TEST(earth_moon_distance_stays_inside_the_observed_range) {
    const auto fixture = sft::load_spice_or_skip();
    const auto geocentric = coordinates::ReferenceFrame::centered_on(celestial::bodies::earth);

    double minimum = 1.0e30;
    double maximum = 0.0;
    const auto start = fixture.time->parse("2026-01-01T00:00:00");
    for (int hour = 0; hour < 24 * 40; ++hour) {
        const auto t = start + time::Duration::hours(static_cast<double>(hour));
        const double d =
            fixture.provider->state(celestial::bodies::moon, t, geocentric).state.position.norm();
        minimum = std::min(minimum, d);
        maximum = std::max(maximum, d);
    }

    std::ostringstream os;
    os << "over 40 days: perigee " << minimum / 1000.0 << " km, apogee " << maximum / 1000.0 << " km";
    INFO(os.str());

    CHECK(minimum > 356'400e3);
    CHECK(maximum < 406'800e3);
    CHECK_NEAR_ABS(minimum, 363'300e3, 7'000e3,
                   "the lunar perigee distance varies between 356 400 and 370 400 km depending on "
                   "the lunar month; the mean is 363 300 km and the bound is the half-range");
    CHECK_NEAR_ABS(maximum, 405'500e3, 2'000e3,
                   "apogee varies between 404 000 and 406 700 km; mean 405 500 km, bound is the "
                   "half-range");
}

TEST(earth_sun_distance_matches_the_astronomical_unit) {
    const auto fixture = sft::load_spice_or_skip();
    const auto heliocentric = coordinates::ReferenceFrame::centered_on(celestial::bodies::sun);

    double minimum = 1.0e30;
    double maximum = 0.0;
    const auto start = fixture.time->parse("2026-01-01T00:00:00");
    for (int day = 0; day < 366; ++day) {
        const auto t = start + time::Duration::days(static_cast<double>(day));
        const double d =
            fixture.provider->state(celestial::bodies::earth, t, heliocentric).state.position.norm();
        minimum = std::min(minimum, d);
        maximum = std::max(maximum, d);
    }

    std::ostringstream os;
    os << "2026 perihelion " << units::m_to_au(minimum) << " au, aphelion "
       << units::m_to_au(maximum) << " au";
    INFO(os.str());

    CHECK_NEAR_ABS(units::m_to_au(minimum), 0.98330, 5.0e-4,
                   "Earth's perihelion distance is 0.98329 au (147.10 Gm); the daily sampling "
                   "here can miss the true extremum by up to half a day, which moves the distance "
                   "by ~1e-6 au, so the 5e-4 bound is set by the 5-digit reference value");
    CHECK_NEAR_ABS(units::m_to_au(maximum), 1.01671, 5.0e-4,
                   "aphelion 1.01671 au (152.10 Gm), same argument");

    const double mean = 0.5 * (minimum + maximum);
    CHECK_NEAR_REL(mean, units::au, 2.0e-4,
                   "the semi-major axis of Earth's orbit is 1.00000102 au; the mean of perihelion "
                   "and aphelion recovers it up to the sampling error of the extrema");
}

TEST(the_solar_system_barycentre_is_close_to_the_sun_but_not_at_it) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse(kReferenceEpoch);

    const double sun_offset =
        fixture.provider->state(celestial::bodies::sun, t, coordinates::ReferenceFrame::ssb_j2000())
            .state.position.norm();

    std::ostringstream os;
    os << "Sun-SSB separation at the reference epoch: " << sun_offset << " m = "
       << sun_offset / 6.957e8 << " solar radii";
    INFO(os.str());

    // This is the quantitative reason ADR-0004 refuses a heliocentric origin.
    CHECK(sun_offset > 1.0e8);
    CHECK(sun_offset < 2.5e9);
}
