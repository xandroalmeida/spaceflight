// Does the BSC5 parser read what BSC5 says?
//
// Fixed-width parsing fails in a way that unit tests exist for: an off-by-one in
// a column range still produces plausible-looking numbers.  The defence is to
// check stars whose values are published independently of the file being parsed.
// See docs/physics/relativistic-rendering.md section 12.

#include "core/render/star_catalog.hpp"
#include "core/render/tone_response.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/catalog_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace sf;
using sf::render::StarCatalog;

namespace {

const render::CatalogStar* find_hr(const StarCatalog& catalog, int hr) {
    const auto& stars = catalog.stars();
    const auto it = std::find_if(stars.begin(), stars.end(),
                                 [hr](const render::CatalogStar& s) { return s.hr == hr; });
    return it == stars.end() ? nullptr : &*it;
}

}  // namespace

TEST(the_parser_reads_stars_whose_values_are_published_elsewhere) {
    const auto catalog = sft::load_catalog_or_skip();
    INFO(catalog.describe());

    // HR number, name, V, B-V, RA (deg), Dec (deg) -- from the Bright Star
    // Catalogue as quoted in general references, NOT read out of the file being
    // tested.  Six stars spread over the sky and over the colour range, so that a
    // column shifted by one byte cannot survive all six.
    struct Known {
        int hr;
        const char* name;
        double v;
        double bv;
        double ra_deg;
        double dec_deg;
    };
    const Known known[] = {
        {2491, "Sirius", -1.46, 0.00, 101.287, -16.716},
        {7001, "Vega", 0.03, 0.00, 279.235, 38.784},
        {5340, "Arcturus", -0.04, 1.23, 213.915, 19.182},
        {424, "Polaris", 2.02, 0.60, 37.953, 89.264},
        {1457, "Aldebaran", 0.85, 1.54, 68.980, 16.509},
        {472, "Achernar", 0.46, -0.16, 24.429, -57.237},
    };

    for (const auto& k : known) {
        const auto* star = find_hr(catalog, k.hr);
        REQUIRE(star != nullptr);

        std::ostringstream os;
        os << k.name << " (HR " << k.hr << "): V " << star->visual_magnitude << ", B-V "
           << star->colour_index << ", T " << star->temperature << " K";
        INFO(os.str());

        CHECK_NEAR_ABS(star->visual_magnitude, k.v, 0.005,
                       "BSC5 publishes V to two decimals (F5.2), so the parsed value is "
                       "either exact or the columns are wrong; half of the last digit "
                       "admits nothing else");
        CHECK_NEAR_ABS(star->colour_index, k.bv, 0.005,
                       "B-V is F5.2 in the same record; same argument");

        const auto expected = render::equatorial_to_unit_vector(k.ra_deg, k.dec_deg);
        const double separation_arcsec =
            units::rad_to_deg(math::angle_between(star->direction, expected)) * 3600.0;
        CHECK_NEAR_ABS(separation_arcsec, 0.0, 30.0,
                       "the file gives declination to whole arcseconds and right ascension "
                       "to 0.1 s of time (1.5 arcsec at the equator); the reference "
                       "positions here are quoted to three decimal degrees, i.e. 3.6 "
                       "arcsec. 30 arcsec is ten times that rounding and a thousand times "
                       "smaller than any column-offset error, which would move a star by "
                       "degrees");
    }
}

TEST(the_catalogue_accounts_for_every_record_it_read) {
    const auto catalog = sft::load_catalog_or_skip();
    const auto& r = catalog.report();

    std::ostringstream os;
    os << r.records_read << " records = " << r.accepted << " accepted + "
       << r.without_coordinates << " without coordinates + " << r.without_colour
       << " without B-V";
    INFO(os.str());

    // Nothing may vanish silently: the three buckets have to add up, or the
    // loader is dropping rows it does not report.
    CHECK_EQ(r.accepted + r.without_coordinates + r.without_colour, r.records_read);

    CHECK_EQ(r.records_read, std::size_t{9110});
    CHECK_EQ(r.without_coordinates, std::size_t{14});
    CHECK_EQ(r.accepted, std::size_t{8786});
}

TEST(colour_index_to_temperature_reproduces_ballesteros) {
    // Ballesteros (2012) calibrated the formula on the Sun; that is the one point
    // where the tolerance is the arithmetic and not the model.
    const double sun = render::temperature_from_colour_index(0.65);
    CHECK_NEAR_ABS(sun, 5778.0, 1.0,
                   "Ballesteros (2012) EPL 97, 34008 is calibrated on the Sun as the paper "
                   "quotes it: B-V = 0.65 -> 5778 K, the solar effective temperature in use "
                   "when it was written. The 1 K bound is the rounding of the published "
                   "coefficients (4600, 0.92, 1.70, 0.62), not a fit. Against the IAU 2015 "
                   "nominal 5772 K the formula is 6 K high -- 0.1%, two orders below its own "
                   "accuracy, and not worth recalibrating a published relation for");

    // And the published disagreements, which are the model's error and belong in
    // the validity table rather than hidden behind a loose bound.
    struct Check {
        const char* name;
        double bv;
        double published_t;
        double allowed_relative;
        const char* why;
    };
    const Check checks[] = {
        {"Vega A0V", 0.00, 9600.0, 0.08,
         "two-band approximation against a spectroscopic T_eff; measured +5.5%"},
        {"Aldebaran K5III", 1.54, 3900.0, 0.08,
         "red giant, where the two-band fit runs low; measured -4.3%"},
    };
    for (const auto& c : checks) {
        const double t = render::temperature_from_colour_index(c.bv);
        std::ostringstream os;
        os << c.name << ": B-V " << c.bv << " -> " << t << " K, published " << c.published_t;
        INFO(os.str());
        CHECK_NEAR_REL(t, c.published_t, c.allowed_relative, c.why);
    }

    // Monotone and finite over the catalogue's whole colour range: a pole in the
    // formula sits at B-V = -0.674, well outside the -0.28 the catalogue reaches,
    // and this is what says so.
    double previous = render::temperature_from_colour_index(-0.30);
    for (double bv = -0.29; bv <= 6.0; bv += 0.01) {
        const double t = render::temperature_from_colour_index(bv);
        CHECK(std::isfinite(t) && t > 0.0 && t < previous);
        previous = t;
    }
}

TEST(magnitude_and_flux_are_inverses_by_definition) {
    // Five magnitudes is a factor of 100, exactly, by the definition of the
    // scale: this is origin #1 of the tolerance policy.
    CHECK_NEAR_REL(render::flux_from_magnitude(0.0) / render::flux_from_magnitude(5.0), 100.0,
                   1.0e-14,
                   "Pogson's ratio is exact by definition; the bound is a few ulps of a "
                   "pow() round trip");

    for (const double m : {-1.46, 0.0, 2.0, 6.0, 7.96}) {
        CHECK_NEAR_ABS(render::magnitude_from_flux(render::flux_from_magnitude(m)), m, 1.0e-13,
                       "round trip through two logarithms; the bound is the ulp of a "
                       "magnitude of order 1 after log10/pow");
    }
}
