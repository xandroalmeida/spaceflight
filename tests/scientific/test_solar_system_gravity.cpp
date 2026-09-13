// The gravity model against the real Solar System: magnitudes, superposition,
// and the size of the perturbations it is supposed to reproduce.

#include "core/celestial/body_catalog.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace sf;
using sf::math::Vec3;

namespace {

const char* kEpoch = "2026-01-01 00:00:00 TDB";

propagation::IntegratorConfig leo_config() {
    propagation::IntegratorConfig cfg{};
    cfg.relative_tolerance = 1.0e-12;
    cfg.absolute_tolerance_position = 1.0e-6;
    cfg.absolute_tolerance_velocity = 1.0e-9;
    cfg.initial_step = time::Duration::seconds(10.0);
    cfg.max_step = time::Duration::seconds(300.0);
    return cfg;
}

}  // namespace

TEST(the_sun_pulls_the_earth_with_the_textbook_acceleration) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse(kEpoch);
    const auto ssb = coordinates::ReferenceFrame::ssb_j2000();

    const std::vector<celestial::BodyId> only_sun{celestial::bodies::sun};
    const auto catalog = celestial::BodyCatalog::resolve(*fixture.provider, only_sun);
    const gravity::PointMassGravity model{*fixture.provider, catalog, ssb};

    propagation::PropagationState at_earth{};
    at_earth.state.position =
        fixture.provider->state(celestial::bodies::earth, t, ssb).state.position;

    const auto force = model.evaluate(at_earth, t);

    const double r = (at_earth.state.position -
                      fixture.provider->state(celestial::bodies::sun, t, ssb).state.position)
                         .norm();
    const double gm_sun = fixture.provider->gravitational_parameter(celestial::bodies::sun);
    const double expected = gm_sun / (r * r);

    std::ostringstream os;
    os << "Sun-Earth distance " << units::m_to_au(r) << " au, |a| = " << force.acceleration.norm()
       << " m/s^2";
    INFO(os.str());

    CHECK_NEAR_REL(force.acceleration.norm(), expected, 1.0e-15,
                   "the model computes GM/r^2 along the line of centres; the test recomputes the "
                   "same quantity from the same inputs, so only the rounding of the division and "
                   "the square root separates them (a few ulp)");
    // The textbook number is quoted at exactly 1 au; the Earth is at 0.9784 au
    // at this epoch, which is a 4.5% difference in 1/r^2.  Scaling it is part of
    // the prediction, not a fudge: an unscaled comparison would be comparing two
    // different questions.
    const double au_scaled = 5.93e-3 / std::pow(units::m_to_au(r), 2.0);
    CHECK_NEAR_REL(force.acceleration.norm(), au_scaled, 1.0e-3,
                   "GM_sun/au^2 = 5.93e-3 m/s^2 is quoted to three significant figures (the exact "
                   "value is 5.9301e-3); scaled to the actual Sun-Earth distance by 1/r^2. The "
                   "1e-3 relative bound is the precision of the three-figure reference value");
}

TEST(superposition_is_exactly_the_sum_of_the_individual_pulls) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse(kEpoch);
    const auto ssb = coordinates::ReferenceFrame::ssb_j2000();

    const auto full = celestial::BodyCatalog::default_solar_system(*fixture.provider);
    const gravity::PointMassGravity model{*fixture.provider, full, ssb};

    // A point 400 km above the Earth's surface, on the anti-Sun side.
    const Vec3 earth = fixture.provider->state(celestial::bodies::earth, t, ssb).state.position;
    propagation::PropagationState probe{};
    probe.state.position = earth + Vec3{6.778e6, 0.0, 0.0};

    const auto total = model.evaluate(probe, t);
    const auto parts = model.contributions(probe.state.position, t);

    Vec3 sum{};
    for (const auto& part : parts) {
        sum += part.acceleration;
    }

    CHECK_NEAR_ABS((sum - total.acceleration).norm(), 0.0, 1.0e-18,
                   "evaluate() and contributions() must sum the same terms in the same fixed "
                   "order, so the results are bit-identical apart from the final accumulation "
                   "order; 1e-18 m/s^2 is below one ulp of the 8.7 m/s^2 total");

    // The ordering of magnitudes is itself a check that no body is misconfigured.
    std::vector<std::pair<double, std::string>> ranked;
    for (const auto& part : parts) {
        ranked.emplace_back(part.acceleration.norm(), part.body.name());
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    std::ostringstream os;
    os << "dominant sources in LEO: ";
    for (std::size_t i = 0; i < 4 && i < ranked.size(); ++i) {
        os << ranked[i].second << " " << ranked[i].first << " m/s^2; ";
    }
    INFO(os.str());

    CHECK_EQ(ranked[0].second, std::string{"Earth"});
    CHECK_NEAR_REL(ranked[0].first, 8.678, 1.0e-3,
                   "GM_earth/r^2 at r = 6778 km is 8.6779 m/s^2; the bound covers the rounding of "
                   "the radius chosen here");
    CHECK_EQ(ranked[1].second, std::string{"Sun"});
    CHECK_EQ(ranked[2].second, std::string{"Moon"});
}

TEST(the_tidal_acceleration_from_sun_and_moon_matches_the_analytic_expression) {
    // The numbers quoted in docs/physics/gravity-model.md section 4, verified
    // against the closed form rather than against themselves.
    //
    // For a probe at distance r from the Earth's centre, ALONG the line to a
    // distant body of parameter GM at distance d, the differential (tidal)
    // acceleration relative to the Earth's centre is
    //
    //     a_tidal = GM [ 1/(d-r)^2 - 1/d^2 ] = 2 GM r / d^3 + O(r^2/d^4)
    //
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse(kEpoch);
    const auto ssb = coordinates::ReferenceFrame::ssb_j2000();

    const Vec3 earth = fixture.provider->position(celestial::bodies::earth, t, ssb);
    const double r = 6.778e6;  // LEO radius

    for (const auto body : {celestial::bodies::moon, celestial::bodies::sun}) {
        const Vec3 body_position = fixture.provider->position(body, t, ssb);
        const double gm = fixture.provider->gravitational_parameter(body);
        const Vec3 direction = (body_position - earth).normalized();
        const double d = (body_position - earth).norm();

        // Probe placed on the near side, on the line of centres.
        const std::vector<celestial::BodyId> single{body};
        const auto catalog = celestial::BodyCatalog::resolve(*fixture.provider, single);
        const gravity::PointMassGravity model{*fixture.provider, catalog, ssb};

        propagation::PropagationState probe{};
        probe.state.position = earth + direction * r;
        propagation::PropagationState centre{};
        centre.state.position = earth;

        const Vec3 tidal =
            model.evaluate(probe, t).acceleration - model.evaluate(centre, t).acceleration;

        const double expected = gm * (1.0 / ((d - r) * (d - r)) - 1.0 / (d * d));
        const double leading = 2.0 * gm * r / (d * d * d);

        std::ostringstream os;
        os << body.name() << " at " << d << " m: tidal |a| = " << tidal.norm()
           << " m/s^2, exact difference " << expected << ", leading term 2GMr/d^3 = " << leading;
        INFO(os.str());

        CHECK_NEAR_REL(tidal.norm(), expected, 1.0e-9,
                       "the exact difference of the two inverse-square pulls, evaluated from the "
                       "same GM and the same distances the model used. The residual is the "
                       "cancellation of two nearly equal accelerations: |a| ~ 3e-5 m/s^2 for the "
                       "Moon and the difference is ~1e-6, so about one part in 30 of the "
                       "significand is lost, leaving ~1e-14 relative. 1e-9 is far above that and "
                       "still an order tighter than the r/d correction this test is checking");

        CHECK_NEAR_REL(tidal.norm(), leading, 1.0e-1,
                       "the leading-order tidal expression 2GMr/d^3 is only the first term; the "
                       "next correction is 3(r/d), which is 5.3e-2 for the Moon and 1.4e-4 for "
                       "the Sun. The 10% bound is that truncation error, and this check is what "
                       "justifies quoting 1.1e-6 and 5.6e-7 m/s^2 in the gravity model document");
    }
}

TEST(dropping_the_sun_from_the_catalogue_visibly_breaks_a_low_earth_orbit) {
    // Not a perturbation test: a demonstration of why the model sums every body
    // at all times (rule section 11).  The Earth-only spacecraft does not feel
    // the Sun, but the Earth it orbits still moves along its SPICE trajectory,
    // which is itself the result of the Sun's pull.  The two trajectories
    // therefore separate at the full solar acceleration, not at the tidal
    // difference.
    const auto fixture = sft::load_spice_or_skip();
    const auto t0 = fixture.time->parse(kEpoch);
    const auto ssb = coordinates::ReferenceFrame::ssb_j2000();

    const auto earth_state = fixture.provider->state(celestial::bodies::earth, t0, ssb);
    const double gm_earth = fixture.provider->gravitational_parameter(celestial::bodies::earth);

    const double radius = 6.778e6;
    const double speed = trajectory::circular_speed(gm_earth, radius);
    const double period = trajectory::circular_period(gm_earth, radius);

    propagation::PropagationState initial{};
    initial.state.position = earth_state.state.position + Vec3{radius, 0.0, 0.0};
    initial.state.velocity = earth_state.state.velocity + Vec3{0.0, speed * 0.6, speed * 0.8};

    const std::vector<celestial::BodyId> earth_only{celestial::bodies::earth};
    const auto catalog_one = celestial::BodyCatalog::resolve(*fixture.provider, earth_only);
    const auto catalog_all = celestial::BodyCatalog::default_solar_system(*fixture.provider);

    const gravity::PointMassGravity model_one{*fixture.provider, catalog_one, ssb};
    const gravity::PointMassGravity model_all{*fixture.provider, catalog_all, ssb};

    propagation::DormandPrince54Propagator prop_one{model_one, leo_config()};
    propagation::DormandPrince54Propagator prop_all{model_all, leo_config()};

    const auto t1 = t0 + time::Duration::seconds(period);
    const auto one = prop_one.propagate(initial, t0, t1);
    const auto all = prop_all.propagate(initial, t0, t1);
    REQUIRE(one.ok());
    REQUIRE(all.ok());

    const double separation = (one.state.state.position - all.state.state.position).norm();

    const Vec3 sun = fixture.provider->position(celestial::bodies::sun, t0, ssb);
    const double d_sun = (earth_state.state.position - sun).norm();
    const double a_sun = fixture.provider->gravitational_parameter(celestial::bodies::sun) /
                         (d_sun * d_sun);
    const double free_fall_bound = 0.5 * a_sun * period * period;

    std::ostringstream os;
    os << "after one orbit (" << period << " s) the two models differ by " << separation
       << " m; the free-fall bound (1/2)a_sun*T^2 is " << free_fall_bound << " m";
    INFO(os.str());

    // Upper bound: if the omitted acceleration acted freely for the whole period,
    // the separation would be (1/2)a T^2.  It cannot exceed that.  Lower bound:
    // the orbital response only partially compensates, so the separation stays
    // within an order of magnitude of it.  A separation of ~26 m (the TIDAL
    // scale) would mean the test setup was measuring something else entirely.
    CHECK(separation < free_fall_bound);
    CHECK(separation > 0.1 * free_fall_bound);
    CHECK_NEAR_REL(separation, 0.54 * free_fall_bound, 0.25,
                   "the omitted term is the Sun's direct pull, a_sun = 6.13e-3 m/s^2, acting for "
                   "one orbital period T = 5553 s. Free fall would give (1/2)a T^2 = 9.5e4 m; the "
                   "Keplerian response to a near-constant external force over one period reduces "
                   "that by roughly a factor of two (measured 0.53). The 25% bound brackets that "
                   "factor without pretending we solved the forced Hill equations analytically");
}

TEST(the_barycentre_is_where_the_mass_distribution_puts_it) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse(kEpoch);
    const auto ssb = coordinates::ReferenceFrame::ssb_j2000();

    const auto catalog = celestial::BodyCatalog::default_solar_system(*fixture.provider);

    Vec3 weighted{};
    double total_gm = 0.0;
    for (const auto& body : catalog.bodies()) {
        weighted += fixture.provider->position(body.id, t, ssb) * body.gm;
        total_gm += body.gm;
    }
    const Vec3 computed_barycentre = weighted / total_gm;

    std::ostringstream os;
    os << "mass-weighted centre of our 10-body catalogue sits " << computed_barycentre.norm()
       << " m from the SSB";
    INFO(os.str());

    // The SSB of DE440 includes 343 asteroids that our catalogue omits, so this
    // does NOT come out at zero, and it must not be asserted to.  What we can
    // assert is that the omission is small compared to the Sun's own excursion
    // about the barycentre (~1e9 m), which is the scale that matters dynamically.
    const double sun_offset =
        fixture.provider->position(celestial::bodies::sun, t, ssb).norm();
    CHECK(computed_barycentre.norm() < 0.1 * sun_offset);
}
