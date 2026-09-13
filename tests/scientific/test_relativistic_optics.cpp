// What a moving observer sees: light time, aberration, Doppler, beaming.
//
// The light-time solver is checked against SPICE's own "LT" correction -- two
// independent solutions of the same implicit equation. The optics are checked
// against their closed forms and against each other's limits.
// See docs/physics/relativistic-rendering.md.

#include "core/relativity/light_time.hpp"
#include "core/relativity/optics.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>
#include <vector>

using namespace sf;
using sf::math::Vec3;

namespace {
const auto kSsb = coordinates::ReferenceFrame::ssb_j2000();
}

TEST(the_light_time_solver_agrees_with_spice) {
    // Our independently implemented retarded-time root against the JPL toolkit's own correction. Both
    // solve |x_obs(t) - x_body(t_r)| = c(t - t_r); neither was written from the
    // other.
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse("2026-01-01 00:00:00 TDB");

    const auto observer_body = celestial::bodies::earth;
    const Vec3 observer = fixture.provider->position(observer_body, t, kSsb);

    for (const auto target : {celestial::bodies::moon, celestial::bodies::sun,
                              celestial::bodies::mars_barycenter,
                              celestial::bodies::jupiter_barycenter}) {
        const auto ours = relativity::apparent_position(*fixture.provider, target, observer, t, kSsb);

        // SPICE, asked the same question with the observer being the frame origin.
        // Note it is asked for "CN", the converged correction: the toolkit's "LT"
        // stops after three iterations on purpose, and comparing against that
        // measures SPICE's truncation instead of our accuracy. It differs from
        // "CN" by 252 m for Mars, which is exactly the size of the disagreement
        // the first version of this test reported as a failure of OUR solver.
        const auto geocentric = coordinates::ReferenceFrame::centered_on(observer_body);
        const auto theirs = fixture.provider->light_time_corrected_state(target, t, geocentric);

        const double difference = (ours.relative_position - theirs.state.position).norm();

        std::ostringstream os;
        os << target.name() << ": light time " << ours.light_time << " s ("
           << ours.light_time / 60.0 << " min), " << ours.iterations
           << " iterations, difference from SPICE " << difference << " m";
        INFO(os.str());

        CHECK(ours.converged);
        CHECK_NEAR_ABS(difference, 0.0, 1.0e-3,
                       "two independent solutions of the same implicit equation, and they agree "
                       "to the last bit available: measured 0 m for the Sun and Mars, 6.5e-6 m "
                       "for the Moon, 3.1e-5 m for Jupiter -- which is the ulp of subtracting two "
                       "barycentric positions of order 1e11 m (3.3e-5 m). The 1e-3 m bound leaves "
                       "room for a different SPICE release without admitting a real error");
        CHECK(ours.iterations <= 5);
    }

    // The scales quoted in the document.
    const auto moon = relativity::apparent_position(*fixture.provider, celestial::bodies::moon,
                                                    observer, t, kSsb);
    CHECK_NEAR_ABS(moon.light_time, 1.273, 0.09,
                   "the Moon ranges from 356 400 to 406 700 km, i.e. 1.189 to 1.357 light "
                   "seconds. The mean is 1.273 s and the bound is the half-range; at this epoch "
                   "the Moon is near perigee and the answer is 1.204 s");

    const auto sun = relativity::apparent_position(*fixture.provider, celestial::bodies::sun,
                                                   observer, t, kSsb);
    CHECK_NEAR_ABS(sun.light_time / 60.0, 8.25, 0.15,
                   "8.317 light minutes at exactly 1 au, and the Earth is at 0.983 au in early "
                   "January -- perihelion is 3 January -- which gives 8.178. The bound spans the "
                   "whole year; the first version of this test quoted the 1 au figure and failed "
                   "by the eccentricity of the Earth's orbit");
}

TEST(the_apparent_position_is_where_the_body_was_not_where_it_is) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse("2026-01-01 00:00:00 TDB");
    const Vec3 observer = fixture.provider->position(celestial::bodies::earth, t, kSsb);

    const auto apparent = relativity::apparent_position(*fixture.provider, celestial::bodies::sun,
                                                        observer, t, kSsb);
    const Vec3 geometric = fixture.provider->position(celestial::bodies::sun, t, kSsb) - observer;

    const double displacement = (apparent.relative_position - geometric).norm();
    const double angle = units::rad_to_deg(angle_between(apparent.relative_position, geometric));

    std::ostringstream os;
    os << "the Sun appears " << displacement / 1000.0 << " km (" << angle * 3600.0
       << " arcsec) from where it geometrically is, after " << apparent.light_time / 60.0
       << " minutes of light travel";
    INFO(os.str());

    // The Sun moves ~12 m/s relative to the barycentre, so the displacement is
    // small; the Earth's own 30 km/s is what makes the classical annual
    // aberration of 20 arcsec, and that belongs to section 3, not here.
    CHECK(displacement > 1000.0);
    CHECK(displacement < 1.0e7);
    CHECK(apparent.retarded_epoch < t);
    CHECK_NEAR_REL((t - apparent.retarded_epoch).seconds(), apparent.light_time, 1.0e-12,
                   "the retarded epoch is t minus the light time, by construction");
}

TEST(aberration_matches_its_closed_form_and_crowds_the_sky_forwards) {
    // cos theta' = (cos theta + beta)/(1 + beta cos theta), for the direction TO
    // the source measured from the velocity.
    for (const double b : {0.01, 0.0896, 0.5, 0.9048, 0.99}) {
        const Vec3 beta{b, 0.0, 0.0};

        for (const double degrees : {0.0, 30.0, 90.0, 150.0, 180.0}) {
            const double theta = units::deg_to_rad(degrees);
            const Vec3 to_source{std::cos(theta), std::sin(theta), 0.0};

            const Vec3 aberrated = relativity::aberrate_source_direction(to_source, beta);
            const double measured = std::atan2(aberrated.y, aberrated.x);

            const double expected =
                std::acos((std::cos(theta) + b) / (1.0 + b * std::cos(theta)));

            CHECK_NEAR_ABS(std::abs(measured), expected, 1.0e-12,
                           "the vector form and the scalar form are the same transformation; only "
                           "the arithmetic path differs");
            CHECK_NEAR_REL(aberrated.norm(), 1.0, 1.0e-12, "and it stays a unit vector");
        }

        // A source at 90 degrees appears at arccos(beta).
        const Vec3 side{0.0, 1.0, 0.0};
        const double moved = units::rad_to_deg(
            angle_between(relativity::aberrate_source_direction(side, beta), Vec3::unit_x()));

        std::ostringstream os;
        os << "beta = " << b << ": a source at 90 deg appears at " << moved
           << " deg, so half the sky fits in a cone of that half-angle ("
           << (1.0 - std::cos(units::deg_to_rad(moved))) / 2.0 * 100.0 << "% of the solid angle)";
        INFO(os.str());

        CHECK_NEAR_REL(moved, units::rad_to_deg(std::acos(b)), 1.0e-9,
                       "arccos(beta) exactly: at beta = 0.9048 that is 25.2 degrees, and half of "
                       "every star in the sky is inside it");
    }

    // Straight ahead and straight behind are fixed points -- there is nowhere
    // else for them to go.
    const Vec3 beta{0.9, 0.0, 0.0};
    CHECK_NEAR_ABS(
        (relativity::aberrate_source_direction(Vec3::unit_x(), beta) - Vec3::unit_x()).norm(), 0.0,
        1.0e-12, "a source dead ahead stays dead ahead");
    CHECK_NEAR_ABS(
        (relativity::aberrate_source_direction(-Vec3::unit_x(), beta) + Vec3::unit_x()).norm(), 0.0,
        1.0e-12, "and dead astern stays astern");

    // Zero velocity changes nothing.
    CHECK_NEAR_ABS(
        (relativity::aberrate_source_direction(Vec3{0.3, -0.5, 0.8}.normalized(), Vec3{}) -
         Vec3{0.3, -0.5, 0.8}.normalized())
            .norm(),
        0.0, 0.0, "identity at beta = 0, exactly");
}

TEST(the_doppler_factors_fore_and_aft_are_exact_reciprocals) {
    for (const double b : {0.0896, 0.5, 0.9048, 0.99}) {
        const Vec3 beta{b, 0.0, 0.0};
        const double gamma = 1.0 / std::sqrt(1.0 - b * b);

        const double ahead = relativity::doppler_factor_to_source(Vec3::unit_x(), beta);
        const double astern = relativity::doppler_factor_to_source(-Vec3::unit_x(), beta);

        std::ostringstream os;
        os << "beta = " << b << ": D ahead = " << ahead << ", D astern = " << astern
           << ", product = " << ahead * astern << ", brightness ahead x"
           << relativity::beaming_factor(ahead) << ", astern x"
           << relativity::beaming_factor(astern);
        INFO(os.str());

        CHECK_NEAR_REL(ahead, gamma * (1.0 + b), 1.0e-14, "D = gamma(1 + beta) looking forward");
        CHECK_NEAR_REL(astern, gamma * (1.0 - b), 1.0e-14, "D = gamma(1 - beta) looking back");
        CHECK_NEAR_REL(ahead * astern, 1.0, 1.0e-14,
                       "gamma^2 (1 - beta^2) = 1 identically, so the two are reciprocals -- not "
                       "approximately, exactly");

        // Transverse Doppler: a photon arriving perpendicular to the motion IN
        // THE COORDINATE FRAME is blueshifted by gamma. It has no classical
        // counterpart; it is time dilation seen as colour.
        const double transverse = relativity::doppler_factor(Vec3::unit_y(), beta);
        CHECK_NEAR_REL(transverse, gamma, 1.0e-14,
                       "D = gamma(1 - beta.n) with beta perpendicular to n gives exactly gamma. "
                       "This is the transverse Doppler effect, and a purely Newtonian model has "
                       "nothing to put here");
    }
}

TEST(beaming_and_the_black_body_shift) {
    const double b = 0.9048;
    const Vec3 beta{b, 0.0, 0.0};

    const double ahead = relativity::doppler_factor_to_source(Vec3::unit_x(), beta);
    const double astern = relativity::doppler_factor_to_source(-Vec3::unit_x(), beta);

    CHECK_NEAR_REL(relativity::beaming_factor(ahead), std::pow(ahead, 4.0), 1.0e-14,
                   "I' = D^4 I, because I_nu/nu^3 is invariant");
    CHECK_NEAR_REL(relativity::beaming_factor(ahead), 400.3, 1.0e-3,
                   "at beta = 0.9048 the forward sky is 400 times brighter");
    CHECK_NEAR_REL(relativity::beaming_factor(astern), 0.0025, 1.0e-2,
                   "and the rear sky 400 times dimmer; the ratio across the sky is 160 000");

    // A black body stays a black body: only the temperature moves. That is what
    // makes an exact star field possible instead of a stylised one.
    const double sun_like = 5800.0;
    const double forward = relativity::shifted_temperature(sun_like, ahead);
    const double backward = relativity::shifted_temperature(sun_like, astern);

    std::ostringstream os;
    os << "a 5800 K star seen at beta = " << b << ": " << forward << " K ahead, " << backward
       << " K astern";
    INFO(os.str());

    CHECK_NEAR_REL(forward, 25944.0, 1.0e-3,
                   "T' = D T with D = 4.4731. At 25 944 K the star has left the visible through "
                   "the blue end");
    CHECK_NEAR_REL(backward, 1297.0, 1.0e-3,
                   "and at 1297 K it has left through the red end. Both directions go dark, for "
                   "opposite reasons");

    // Stefan-Boltzmann consistency: the D^4 of the beaming IS the (DT)^4 of the
    // shifted black body. Two routes to the same number.
    CHECK_NEAR_REL(relativity::beaming_factor(ahead), std::pow(forward / sun_like, 4.0), 1.0e-12,
                   "sigma T'^4 = D^4 sigma T^4. If these disagreed, one of the two formulas would "
                   "be wrong, and nothing else in the model would notice");
}

TEST(aberration_and_doppler_are_consistent_with_each_other) {
    // The same denominator gamma(1 - beta.n) appears in both transformations,
    // which is not a coincidence: it is the time component of the photon's
    // four-momentum. Checking the relation catches a sign error in either.
    const Vec3 beta{0.6, 0.0, 0.0};
    const double gamma = 1.0 / std::sqrt(1.0 - 0.36);

    for (const double degrees : {0.0, 45.0, 90.0, 135.0, 180.0}) {
        const double theta = units::deg_to_rad(degrees);
        const Vec3 n{std::cos(theta), std::sin(theta), 0.0};  // propagation direction

        const double d = relativity::doppler_factor(n, beta);
        const Vec3 aberrated = relativity::aberrate_propagation(n, beta);

        // In the ship frame the angle satisfies cos theta' = (cos theta - beta)
        // / (1 - beta cos theta); equivalently D cos theta' = cos theta - beta,
        // scaled by gamma.
        CHECK_NEAR_REL(d * aberrated.x, gamma * (std::cos(theta) - 0.6), 1.0e-12,
                       "the aberrated direction times the Doppler factor recovers the boosted "
                       "photon four-momentum, because both are that same four-vector read in "
                       "different places");
    }
}
