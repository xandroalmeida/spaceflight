// Does the toolkit boundary behave?  Kernels load, missing data is refused
// loudly, frames compose, and the constants we pull are the ones we think.

#include "core/celestial/body_catalog.hpp"
#include "core/ephemeris/errors.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <sstream>

using namespace sf;

TEST(kernels_load_and_report_what_they_loaded) {
    const auto fixture = sft::load_spice_or_skip();

    CHECK(fixture.kernels->loaded().size() >= 4);
    CHECK(fixture.kernels->has_leapseconds());
    CHECK(!fixture.kernels->spk_files().empty());
    INFO(fixture.kernels->summary());
}

TEST(missing_kernel_directory_fails_loudly) {
    CHECK_THROWS_AS(ephemeris::SpiceKernelSet::from_directory("/nonexistent/kernels"),
                    ephemeris::KernelLoadError);
}

TEST(gravitational_parameters_match_independent_published_values) {
    const auto fixture = sft::load_spice_or_skip();

    // These reference values come from outside the kernel we are reading, which
    // is what makes this a test rather than a tautology.
    CHECK_NEAR_REL(fixture.provider->gravitational_parameter(celestial::bodies::earth),
                   3.986004418e14, 2.0e-8,
                   "WGS84/EGM96 GM_earth = 3.986004418e14 m^3/s^2 (includes the atmosphere) vs "
                   "DE440's 3.9860043550702266e14; the two determinations differ by ~1.6e-8 "
                   "relative, so the bound is set just above that difference");

    CHECK_NEAR_REL(fixture.provider->gravitational_parameter(celestial::bodies::sun),
                   1.32712440018e20, 1.0e-9,
                   "IAU 2009 nominal GM_sun = 1.32712440018e20 m^3/s^2 vs DE440's "
                   "1.32712440041e20; they agree to 1.7e-10 relative");

    CHECK_NEAR_REL(fixture.provider->gravitational_parameter(celestial::bodies::moon),
                   4.9028e12, 1.0e-5,
                   "the commonly quoted lunar GM is 4.9028e12 m^3/s^2, given to 5 significant "
                   "figures; the bound is the quoted precision");

    // Earth/Moon mass ratio, an independently measured quantity (LLR): 81.3005690.
    const double ratio = fixture.provider->gravitational_parameter(celestial::bodies::earth) /
                         fixture.provider->gravitational_parameter(celestial::bodies::moon);
    CHECK_NEAR_REL(ratio, 81.3005690, 1.0e-8,
                   "lunar laser ranging gives the Earth/Moon mass ratio as 81.3005690(2); "
                   "DE440 fits the same data, so agreement at 1e-8 is expected");
}

TEST(mean_radii_are_sane_and_barycentres_have_none) {
    const auto fixture = sft::load_spice_or_skip();

    CHECK_NEAR_REL(fixture.provider->mean_radius(celestial::bodies::earth), 6371008.4, 1.0,
                   "IUGG mean Earth radius R1 = 6371.0084 km, computed as (2a+b)/3 from the same "
                   "IAU triaxial radii the PCK stores, so the agreement should be exact to the "
                   "metre; 1 m of slack covers PCK version changes");
    CHECK_NEAR_REL(fixture.provider->mean_radius(celestial::bodies::moon), 1737400.0, 1.0e-4,
                   "IAU mean lunar radius 1737.4 km, quoted to 5 figures");
    CHECK_EQ(fixture.provider->mean_radius(celestial::bodies::jupiter_barycenter), 0.0);
}

TEST(epochs_outside_the_loaded_span_are_refused_not_extrapolated) {
    const auto fixture = sft::load_spice_or_skip();

    const auto window = fixture.provider->coverage(celestial::bodies::earth);
    REQUIRE(window.valid);
    INFO("Earth SPK coverage: " + fixture.time->to_utc_string(window.begin, 0) + " .. " +
         fixture.time->to_utc_string(window.end, 0));

    const auto modern = fixture.time->parse("2026-01-01T00:00:00");
    CHECK(window.contains(modern));

    // de440s starts in 1849.  Year 1700 must be an error, never a guess.
    const auto too_early = fixture.time->parse("1700-01-01T00:00:00");
    CHECK(!window.contains(too_early));
    CHECK_THROWS_AS(fixture.provider->state(celestial::bodies::earth, too_early,
                                            coordinates::ReferenceFrame::ssb_j2000()),
                    ephemeris::EphemerisUnavailable);

    const auto too_late = fixture.time->parse("2200-01-01T00:00:00");
    CHECK_THROWS_AS(fixture.provider->state(celestial::bodies::earth, too_late,
                                            coordinates::ReferenceFrame::ssb_j2000()),
                    ephemeris::EphemerisUnavailable);
}

TEST(unknown_bodies_are_refused) {
    const auto fixture = sft::load_spice_or_skip();
    const celestial::BodyId nonsense{987654};
    const auto t = fixture.time->parse("2026-01-01T00:00:00");

    CHECK(!fixture.provider->has_body(nonsense));
    CHECK_THROWS_AS(fixture.provider->state(nonsense, t, coordinates::ReferenceFrame::ssb_j2000()),
                    ephemeris::SpaceflightError);
    CHECK_THROWS_AS(fixture.provider->gravitational_parameter(nonsense),
                    ephemeris::EphemerisUnavailable);
}

TEST(changing_the_origin_is_a_translation_and_nothing_else) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse("2026-03-15T06:30:00");

    const auto ssb = coordinates::ReferenceFrame::ssb_j2000();
    const auto geocentric = coordinates::ReferenceFrame::centered_on(celestial::bodies::earth);

    const auto moon_ssb = fixture.provider->state(celestial::bodies::moon, t, ssb);
    const auto earth_ssb = fixture.provider->state(celestial::bodies::earth, t, ssb);
    const auto moon_geo = fixture.provider->state(celestial::bodies::moon, t, geocentric);

    const auto difference = moon_ssb.state - earth_ssb.state;

    // Both sides are computed by SPICE from the same segments, so they must
    // agree to the rounding of the subtraction of two ~1.5e11 m quantities.
    CHECK_NEAR_ABS((difference.position - moon_geo.state.position).norm(), 0.0, 1.0e-4,
                   "cancellation of two barycentric positions of magnitude ~1.5e11 m: one ulp is "
                   "3.3e-5 m, so a few ulp is 1e-4 m. Nothing physical lives at this scale");
    CHECK_NEAR_ABS((difference.velocity - moon_geo.state.velocity).norm(), 0.0, 1.0e-9,
                   "same cancellation argument at ~3e4 m/s: one ulp is 6.7e-12 m/s");

    CHECK_NEAR_ABS(fixture.provider->state(celestial::bodies::earth, t, geocentric)
                       .state.position.norm(),
                   0.0, 0.0,
                   "a body observed from itself is exactly at the origin: the provider "
                   "short-circuits this case rather than asking SPICE");
}

TEST(the_default_catalogue_resolves_completely) {
    const auto fixture = sft::load_spice_or_skip();
    const auto catalog = celestial::BodyCatalog::default_solar_system(*fixture.provider);

    CHECK_EQ(catalog.size(), std::size_t{10});
    for (const auto& body : catalog.bodies()) {
        CHECK(body.gm > 0.0);
        CHECK(fixture.provider->has_body(body.id));
    }

    // The Sun must dominate: any catalogue where it does not is misconfigured.
    const auto* sun = catalog.find(celestial::bodies::sun);
    REQUIRE(sun != nullptr);
    for (const auto& body : catalog.bodies()) {
        if (body.id != celestial::bodies::sun) {
            CHECK(body.gm < sun->gm);
        }
    }
}
