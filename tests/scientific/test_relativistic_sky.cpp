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
#include <limits>
#include <sstream>
#include <stdexcept>
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

TEST(visual_effect_switches_are_independent_and_do_not_change_the_catalogue) {
    auto sky = make_sky();
    const Vec3 beta{0.9, 0.0, 0.0};
    const std::size_t index = 0;
    const auto rest_direction = sky.catalog().stars()[index].direction;

    sky.update(beta, false, false, false);
    CHECK_NEAR_ABS((sky.frame().apparent_direction[index] - rest_direction).norm(), 0.0, 0.0,
                   "aberration OFF is an exact identity");
    CHECK_EQ(sky.frame().doppler[index], 1.0F);
    CHECK_EQ(sky.frame().beaming[index], 1.0F);

    sky.update(beta, true, false, false);
    CHECK((sky.frame().apparent_direction[index] - rest_direction).norm() > 1.0e-6);
    CHECK_EQ(sky.frame().doppler[index], 1.0F);
    CHECK_EQ(sky.frame().beaming[index], 1.0F);

    sky.update(beta, false, true, false);
    CHECK_NEAR_ABS((sky.frame().apparent_direction[index] - rest_direction).norm(), 0.0, 0.0,
                   "Doppler does not move a source");
    CHECK(sky.frame().doppler[index] != 1.0F);
    CHECK_EQ(sky.frame().beaming[index], 1.0F);

    sky.update(beta, false, false, true);
    CHECK_EQ(sky.frame().doppler[index], 1.0F);
    CHECK(sky.frame().beaming[index] != 1.0F);
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

// ---------------------------------------------------------------------------
// The synthetic catalogue (Milestone 6.1, Part B).
//
// The starfield validation harness builds its stars by hand, and a harness whose
// inputs are wrong reports the renderer's answer as the renderer's fault.  These
// two tests fix the properties the harness depends on: that a synthetic star
// carries the same derived fields a BSC5 record would, and that the colour index
// it is given back is the one that produces its temperature.
// See docs/validation/starfield-debug.md section 5.
// ---------------------------------------------------------------------------

TEST(the_colour_index_inverse_is_exact_and_not_a_fit) {
    // Ballesteros is a quadratic in 0.92 (B-V), so its inverse is algebraic and
    // the round trip has no business losing anything but rounding.  Tolerance
    // 1e-12 relative -- origin #1 of docs/validation/tolerances.md: the exact
    // answer is known, and the only error allowed is the arithmetic's own.
    for (const double temperature : {1500.0, 3000.0, 4600.0, 5772.0, 8000.0, 12000.0, 25000.0,
                                     40000.0}) {
        const double colour_index = render::colour_index_from_temperature(temperature);
        const double back = render::temperature_from_colour_index(colour_index);
        CHECK_NEAR_REL(back, temperature, 1.0e-12,
                       "the inverse of an exact quadratic root, evaluated in double: nothing "
                       "but rounding may be lost");
    }

    // The physical branch, not the other root: B-V falls as a star gets hotter.
    double previous = std::numeric_limits<double>::infinity();
    for (const double temperature : {2000.0, 4000.0, 6000.0, 10000.0, 20000.0}) {
        const double colour_index = render::colour_index_from_temperature(temperature);
        CHECK(colour_index < previous);
        previous = colour_index;
    }

    CHECK_THROWS_AS(render::colour_index_from_temperature(0.0), std::invalid_argument);
    CHECK_THROWS_AS(render::colour_index_from_temperature(-1.0), std::invalid_argument);
}

TEST(a_synthetic_star_goes_through_the_same_arithmetic_as_a_catalogue_one) {
    render::StarCatalog catalogue;
    catalogue.add_star(Vec3{3.0, 0.0, 4.0}, 5800.0, 2.0, "on the x-z diagonal");
    catalogue.add_star(Vec3{0.0, -1.0, 0.0}, 3000.0, -1.0);

    REQUIRE(catalogue.size() == 2);
    // The report has to count them, or describe() would say a catalogue of two
    // stars was built from zero records.
    CHECK_EQ(catalogue.report().accepted, std::size_t{2});
    CHECK_EQ(catalogue.report().records_read, std::size_t{2});

    const auto& first = catalogue.stars()[0];
    // Normalised, so that a caller may hand in any vector along the direction.
    CHECK_NEAR_ABS(first.direction.norm(), 1.0, 1.0e-15, "add_star normalises the direction");
    CHECK_NEAR_ABS(first.direction.x, 0.6, 1.0e-15, "3-4-5 triangle, exactly");
    CHECK_NEAR_ABS(first.direction.z, 0.8, 1.0e-15, "3-4-5 triangle, exactly");
    // The derived fields are the ones the loader derives, from the same functions.
    CHECK_NEAR_REL(first.rest_flux, render::flux_from_magnitude(2.0), 1.0e-15,
                   "rest_flux must come from flux_from_magnitude, not from a second formula");
    CHECK_NEAR_REL(render::temperature_from_colour_index(first.colour_index), 5800.0, 1.0e-12,
                   "a synthetic star's B-V must be the one that produces its temperature, or a "
                   "catalogue whose fields disagree with each other travels");

    // Brightness ordering survives: the second star is three magnitudes brighter.
    CHECK_NEAR_REL(catalogue.stars()[1].rest_flux / first.rest_flux,
                   std::pow(10.0, -0.4 * (-1.0 - 2.0)), 1.0e-14,
                   "the magnitude scale is a definition, not an approximation");

    // A zero direction and a non-positive temperature are caller errors, not dim
    // stars, and returning a NaN-filled record would let them travel.
    CHECK_THROWS_AS(catalogue.add_star(Vec3{}, 5800.0, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(catalogue.add_star(Vec3::unit_x(), 0.0, 0.0), std::invalid_argument);

    // And it has to work as a sky: the harness feeds exactly this into
    // RelativisticSky, so the constructor must accept it.
    render::RelativisticSky sky{render::StarCatalog::from_stars(catalogue.stars()),
                                render::build_planck_table(256)};
    REQUIRE(sky.star_count() == 2);
    sky.update(Vec3{});
    CHECK_NEAR_ABS(sky.frame().doppler[0], 1.0, 0.0,
                   "at rest the Doppler factor is exactly one, and nothing about a synthetic "
                   "catalogue may change that");
}
