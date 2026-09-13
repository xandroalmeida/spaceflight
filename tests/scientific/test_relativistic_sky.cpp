// The real sky, aberrated: the claims of section 3 measured against 8786 stars.
//
// The interesting property here is exact, and it is not obvious: the FRACTION of
// the catalogue inside the forward cone arccos(beta) does not depend on beta.
// The aberration map carries the hemisphere theta < 90 degrees onto that cone
// star by star, so the set is invariant and so is its size.  Tolerance zero --
// origin #1 of docs/validation/tolerances.md -- and a sign error or a confusion
// between the propagation direction n and the source direction s breaks it at
// once.
//
// See docs/physics/relativistic-rendering.md sections 3 and 12.3.

#include "core/relativity/optics.hpp"
#include "core/render/relativistic_sky.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/catalog_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace sf;
using sf::math::Vec3;

namespace {

render::RelativisticSky make_sky() {
    // A coarse table here on purpose: this file is about the optics, and 256
    // entries make it run in a fraction of the time.  The table's own accuracy is
    // test_blackbody_colour.cpp's job.
    return render::RelativisticSky{sft::load_catalog_or_skip(), render::build_planck_table(256)};
}

}  // namespace

TEST(the_forward_cone_holds_the_same_stars_at_every_speed) {
    auto sky = make_sky();
    INFO(sky.catalog().describe());

    const Vec3 heading{0.6, 0.8, 0.0};   // nothing special, and not an axis

    // Which stars are in the forward hemisphere at rest?  That is the set the
    // aberration map must carry onto the cone, whatever beta is.
    std::vector<bool> forward_at_rest(sky.star_count());
    for (std::size_t i = 0; i < sky.star_count(); ++i) {
        forward_at_rest[i] = dot(sky.catalog().stars()[i].direction, heading) >= 0.0;
    }
    const auto expected_count =
        static_cast<std::size_t>(std::count(forward_at_rest.begin(), forward_at_rest.end(), true));

    for (const double beta : {0.01, 0.0896, 0.5, 0.9048, 0.99, 0.9999}) {
        sky.update(heading * beta);
        const double cone = std::acos(beta);
        const double cos_cone = std::cos(cone);

        std::size_t inside = 0;
        std::size_t mismatched = 0;
        for (std::size_t i = 0; i < sky.star_count(); ++i) {
            const bool in_cone = dot(sky.frame().apparent_direction[i], heading) >= cos_cone;
            if (in_cone) {
                ++inside;
            }
            if (in_cone != forward_at_rest[i]) {
                ++mismatched;
            }
        }

        std::ostringstream os;
        os << "beta " << beta << ": cone " << units::rad_to_deg(cone) << " deg holds " << inside
           << " stars (" << 100.0 * static_cast<double>(inside) /
                                 static_cast<double>(sky.star_count())
           << " %), " << mismatched << " differ from the rest-frame hemisphere";
        INFO(os.str());

        // Zero, not "small": the map is exact.  A star exactly ON the boundary
        // could fall either way, and there is none in BSC5 -- if one ever is, the
        // right fix is to name it, not to loosen this.
        CHECK_EQ(mismatched, std::size_t{0});
        CHECK_EQ(inside, expected_count);
    }

    std::ostringstream os;
    os << "the invariant fraction is "
       << 100.0 * static_cast<double>(expected_count) / static_cast<double>(sky.star_count())
       << " %, and the 0.5 % it misses from half is the catalogue's own anisotropy";
    INFO(os.str());

    // Half, to within how anisotropic a magnitude-limited catalogue crowded on
    // the galactic plane actually is.
    CHECK_NEAR_ABS(static_cast<double>(expected_count) / static_cast<double>(sky.star_count()),
                   0.5, 0.05,
                   "a UNIFORM sky would give exactly 0.5, because the cone arccos(beta) has "
                   "solid-angle fraction (1-beta)/2. BSC5 is not uniform: it is "
                   "magnitude-limited and crowds the galactic plane, and over 400 random "
                   "axes the fraction in a 25.2 deg cone has mean 4.80 % against 4.76 % "
                   "uniform with a standard deviation of 1.36 % -- about 28 % of the mean. "
                   "The 0.05 bound is roughly twice that spread applied to a hemisphere");
}

TEST(the_doppler_extremes_are_where_the_velocity_points) {
    auto sky = make_sky();
    const Vec3 heading{0.0, 0.0, 1.0};
    const double beta = 0.9048;
    sky.update(heading * beta);

    const double gamma = 1.0 / std::sqrt(1.0 - beta * beta);
    const auto& d = sky.frame().doppler;
    const auto largest = static_cast<double>(*std::max_element(d.begin(), d.end()));
    const auto smallest = static_cast<double>(*std::min_element(d.begin(), d.end()));

    std::ostringstream os;
    os << "D over the whole catalogue: " << smallest << " .. " << largest
       << ", bounds gamma(1-beta) = " << gamma * (1.0 - beta) << " and gamma(1+beta) = "
       << gamma * (1.0 + beta);
    INFO(os.str());

    // No star can exceed the poles of the map, and with 8786 of them some star is
    // within a degree of each pole.
    CHECK(largest <= gamma * (1.0 + beta) + 1.0e-12);
    CHECK(smallest >= gamma * (1.0 - beta) - 1.0e-12);
    CHECK_NEAR_REL(largest, gamma * (1.0 + beta), 2.0e-3,
                   "the nearest catalogue star to the velocity axis; with 8786 stars the "
                   "closest is typically under a degree away, and D varies as "
                   "1 - O(beta theta^2 / 2) near the pole, giving ~1e-4 for one degree");
    CHECK_NEAR_REL(smallest, gamma * (1.0 - beta), 2.0e-3, "the same argument astern");

    // The reciprocity of section 4, on the actual data rather than on a formula.
    CHECK_NEAR_REL(largest * smallest, 1.0, 5.0e-3,
                   "gamma^2 (1 - beta^2) = 1 exactly; the residual is the two stars' offset "
                   "from the poles, which is what the bounds above measure");
}

TEST(the_aft_sky_goes_out) {
    // Section 10.2 as a rendered quantity: not "the Doppler factor is small" but
    // "the detector reads zero", through the whole chain the shader uses.
    auto sky = make_sky();

    // Sirius, the brightest star there is, put directly astern by flying at it.
    const auto& stars = sky.catalog().stars();
    const auto sirius = std::find_if(stars.begin(), stars.end(),
                                     [](const render::CatalogStar& s) { return s.hr == 2491; });
    REQUIRE(sirius != stars.end());
    const std::size_t index = static_cast<std::size_t>(sirius - stars.begin());

    // Fly directly AWAY from Sirius, so it sits at exactly 180 degrees.
    const Vec3 away = -sirius->direction;

    std::ostringstream os;
    os << "Sirius (V " << sirius->visual_magnitude << ", T " << sirius->temperature
       << " K) receding:";
    INFO(os.str());

    double previous = 2.0;
    for (const double beta : {0.0, 0.0896, 0.5, 0.9048, 0.99}) {
        sky.update(away * beta);
        const double response = sky.response_of(index);

        std::ostringstream line;
        line << "  beta " << beta << ": D = " << sky.frame().doppler[index]
             << ", detector response " << response;
        INFO(line.str());

        CHECK(response < previous);
        CHECK(response >= 0.0);
        previous = response;
    }

    // At beta = 0.99 the brightest star in the sky is gone -- not dim, gone:
    // D = 0.0709 puts a 9940 K star at 705 K, and e^-52 of its flux is in the
    // band.  The response underflows to zero, which is the physics and not a
    // clamp (section 10.4).
    sky.update(away * 0.99);
    CHECK(sky.response_of(index) < 1.0e-12);
}

TEST(the_sky_at_rest_is_the_catalogue_untouched) {
    // The zero-beta identity, which is the cheapest way to catch a transform that
    // is subtly always-on.
    auto sky = make_sky();
    sky.update(Vec3{});

    for (std::size_t i = 0; i < sky.star_count(); ++i) {
        const auto& rest = sky.catalog().stars()[i].direction;
        const auto& now = sky.frame().apparent_direction[i];
        CHECK_NEAR_ABS((now - rest).norm(), 0.0, 1.0e-15,
                       "aberration at beta = 0 is the identity, up to the normalisation "
                       "inside it; a few ulps of a unit vector");
        CHECK_NEAR_ABS(static_cast<double>(sky.frame().doppler[i]), 1.0, 1.0e-6,
                       "D = gamma(1 - 0) = 1 exactly; the bound is the float the frame "
                       "stores it in (epsilon 1.2e-7)");
    }
}

TEST(the_diagnostics_agree_with_the_closed_forms) {
    auto sky = make_sky();
    const Vec3 beta_vector = Vec3{0.6, 0.0, 0.8} * 0.9048;
    sky.update(beta_vector);
    const auto d = sky.diagnostics(beta_vector);

    std::ostringstream os;
    os << "beta " << d.beta << ", gamma " << d.lorentz_factor << ", cone "
       << units::rad_to_deg(d.forward_cone_half_angle) << " deg, "
       << 100.0 * d.fraction_in_forward_cone << " % of the catalogue inside; D from "
       << d.min_doppler << " to " << d.max_doppler << "; a 5800 K star reads "
       << d.reference_forward_visible << "x ahead and " << d.reference_aft_visible
       << "x astern in the visible band";
    INFO(os.str());

    CHECK_NEAR_ABS(units::rad_to_deg(d.forward_cone_half_angle), 25.204, 1.0e-3,
                   "arccos(0.9048) = 25.204 deg, the row in section 3; the bound is the "
                   "four digits beta is quoted to");
    CHECK_NEAR_REL(d.max_doppler, 4.4731, 1.0e-4,
                   "gamma(1+beta) at beta = 0.9048, the row in section 4");
    CHECK_NEAR_REL(d.reference_forward_visible, 51.5, 0.05,
                   "section 10.2, recomputed here through the 256-entry table this file "
                   "uses; the 5% bound is that table's coarseness against the 3% the "
                   "4096-entry one achieves in test_blackbody_colour.cpp");
}
