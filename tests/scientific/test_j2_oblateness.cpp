// The J2 term against closed-form results.
//
// Most of this file needs no kernels: the body sits at the origin with a pole the
// test chooses, which makes the geometry exact and isolates the perturbation from
// everything else.  Only the last test asks SPICE where a real pole points.
//
// Reference expressions and numbers: docs/physics/geopotential.md.

#include "core/celestial/body_catalog.hpp"
#include "core/celestial/oblateness.hpp"
#include "core/gravity/composite_force_model.hpp"
#include "core/gravity/oblateness_gravity.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/analytic_ephemeris.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

using namespace sf;
using sf::math::Vec3;

namespace {

constexpr double kGm = 3.9860043550702266e14;               // DE440 GM_earth
constexpr double kJ2 = 1.0826266835e-3;                     // EGM96
constexpr double kRef = 6378136.3;                          // EGM96 reference radius
const celestial::BodyId kEarth = celestial::bodies::earth;
const auto kSsb = coordinates::ReferenceFrame::ssb_j2000();

struct J2Rig {
    sft::FixedPointMassProvider provider{kEarth, kGm, 6371008.0};
    sft::FixedPoleOrientation orientation{Vec3{0.0, 0.0, 1.0}};
    celestial::BodyCatalog catalog;
    gravity::CompositeForceModel forces;

    J2Rig() {
        const std::vector<celestial::BodyId> ids{kEarth};
        catalog = celestial::BodyCatalog::resolve(provider, ids);
        forces.add(std::make_unique<gravity::PointMassGravity>(provider, catalog, kSsb));
        forces.add(std::make_unique<gravity::OblatenessGravity>(
            provider, orientation, kEarth, celestial::GeopotentialModel{kJ2, kRef}, kSsb));
    }
};

propagation::PropagationState state_at(double radius, double inclination, double speed_factor = 1.0) {
    propagation::PropagationState s{};
    const double v = speed_factor * std::sqrt(kGm / radius);
    s.state.position = Vec3{radius, 0.0, 0.0};
    s.state.velocity = Vec3{0.0, v * std::cos(inclination), v * std::sin(inclination)};
    return s;
}

propagation::IntegratorConfig config() {
    propagation::IntegratorConfig cfg{};
    cfg.relative_tolerance = 1.0e-12;
    cfg.absolute_tolerance_position = 1.0e-6;
    cfg.absolute_tolerance_velocity = 1.0e-9;
    cfg.initial_step = time::Duration::seconds(10.0);
    cfg.max_step = time::Duration::seconds(120.0);
    return cfg;
}

double unwrap(double angle, double previous) {
    while (angle - previous > units::pi) {
        angle -= units::two_pi;
    }
    while (previous - angle > units::pi) {
        angle += units::two_pi;
    }
    return angle;
}

}  // namespace

TEST(j2_has_the_right_magnitude_and_sign_at_the_equator_and_at_the_pole) {
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};
    sft::FixedPoleOrientation orientation{Vec3{0.0, 0.0, 1.0}};
    const gravity::OblatenessGravity j2{provider, orientation, kEarth,
                                        celestial::GeopotentialModel{kJ2, kRef}, kSsb};

    const double r = 6.778e6;

    propagation::PropagationState equator{};
    equator.state.position = Vec3{r, 0.0, 0.0};
    const Vec3 a_equator = j2.evaluate(equator, time::CoordinateTime::j2000()).acceleration;

    propagation::PropagationState pole{};
    pole.state.position = Vec3{0.0, 0.0, r};
    const Vec3 a_pole = j2.evaluate(pole, time::CoordinateTime::j2000()).acceleration;

    const double expected_equator = 1.5 * kJ2 * kGm * kRef * kRef / (r * r * r * r);
    const double expected_pole = 2.0 * expected_equator;

    std::ostringstream os;
    os << "equator |a| = " << a_equator.norm() << " m/s^2 (inward), pole |a| = " << a_pole.norm()
       << " m/s^2 (outward); point mass is " << kGm / (r * r) << " m/s^2";
    INFO(os.str());

    CHECK_NEAR_REL(a_equator.norm(), expected_equator, 1.0e-14,
                   "closed form (3/2) J2 GM R^2 / r^4 at s = 0, recomputed from the same "
                   "constants: only the rounding of a handful of products separates the two");
    CHECK_NEAR_REL(a_pole.norm(), expected_pole, 1.0e-14,
                   "at the pole the same expression gives exactly twice the equatorial magnitude, "
                   "3 J2 GM R^2 / r^4");

    // Sign is the physics, not a detail: an oblate body pulls LESS than a point
    // mass at the pole (the equatorial bulge is farther away) and MORE at the
    // equator.  Getting this backwards reverses the nodal regression.
    CHECK(dot(a_equator, equator.state.position) < 0.0);   // inward
    CHECK(dot(a_pole, pole.state.position) > 0.0);         // outward

    CHECK_NEAR_ABS(a_equator.y, 0.0, 1.0e-20,
                   "on the x axis with the pole along z, the perturbation is purely radial by "
                   "symmetry; any out-of-plane component would be a formula error");
    CHECK_NEAR_ABS(a_equator.z, 0.0, 1.0e-20, "same symmetry argument");
}

TEST(j2_matches_the_ratio_quoted_in_the_gravity_model_document) {
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};
    sft::FixedPoleOrientation orientation{};
    const gravity::OblatenessGravity j2{provider, orientation, kEarth,
                                        celestial::GeopotentialModel{kJ2, kRef}, kSsb};

    const double r = 6.778e6;
    propagation::PropagationState probe{};
    probe.state.position = Vec3{r, 0.0, 0.0};

    const double ratio = j2.evaluate(probe, time::CoordinateTime::j2000()).acceleration.norm() /
                         (kGm / (r * r));

    CHECK_NEAR_REL(ratio, 1.438e-3, 1.0e-3,
                   "docs/physics/gravity-model.md section 4 quotes 1.438e-3 for the J2/point-mass "
                   "ratio in LEO, to four significant figures; the bound is that precision. This "
                   "is the check that keeps the document and the code from drifting apart");

    // Far field: the same ratio falls as 1/r^2.
    propagation::PropagationState distant{};
    distant.state.position = Vec3{3.844e8, 0.0, 0.0};  // lunar distance
    const double far_ratio =
        j2.evaluate(distant, time::CoordinateTime::j2000()).acceleration.norm() /
        (kGm / (3.844e8 * 3.844e8));
    CHECK_NEAR_REL(far_ratio, 1.5 * kJ2 * (kRef / 3.844e8) * (kRef / 3.844e8), 1.0e-12,
                   "(3/2) J2 (R/r)^2 evaluated directly; this is the quantitative reason J2 is "
                   "irrelevant beyond the Earth's neighbourhood");
}

TEST(j2_conserves_the_angular_momentum_component_along_the_pole) {
    // An axially symmetric potential exerts no torque about its symmetry axis, so
    // L.n is an exact invariant while |L| is not.  This distinguishes "J2
    // implemented" from "some perturbation implemented": almost any error in the
    // formula breaks the axial symmetry.
    J2Rig rig;
    const Vec3 pole{0.0, 0.0, 1.0};

    const auto initial = state_at(6.778e6, units::deg_to_rad(51.6));
    propagation::DormandPrince54Propagator propagator{rig.forces, config()};

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::hours(6.0));
    REQUIRE(result.ok());

    const Vec3 l0 = cross(initial.state.position, initial.state.velocity);
    const Vec3 l1 = cross(result.state.state.position, result.state.state.velocity);

    const double lz_drift = std::abs((dot(l1, pole) - dot(l0, pole)) / dot(l0, pole));
    const double l_drift = std::abs((l1.norm() - l0.norm()) / l0.norm());

    std::ostringstream os;
    os << "over 6 h: L.n drift " << lz_drift << " relative, |L| drift " << l_drift << " relative";
    INFO(os.str());

    CHECK_NEAR_ABS(lz_drift, 0.0, 1.0e-11,
                   "L.n is an exact invariant of an axially symmetric field, so the only residual "
                   "is the integrator's own error in the state. With rtol = 1e-12 over ~4 orbits "
                   "that is ~1e-12; the bound is one order above");

    // And the total must NOT be conserved -- if it were, J2 would be doing nothing.
    CHECK(l_drift > 100.0 * lz_drift);
}

TEST(j2_reproduces_the_secular_nodal_regression) {
    J2Rig rig;

    const double a = 6.778e6;
    const double inclination = units::deg_to_rad(51.6);
    const double period = trajectory::circular_period(kGm, a);
    const double orbits = 30.0;

    const auto initial = state_at(a, inclination);
    propagation::DormandPrince54Propagator propagator{rig.forces, config()};

    const auto t0 = time::CoordinateTime::j2000();
    const auto t1 = t0 + time::Duration::seconds(orbits * period);
    const auto result = propagator.propagate(initial, t0, t1);
    REQUIRE(result.ok());

    const auto before = trajectory::elements_from_state(initial.state, kGm);
    const auto after = trajectory::elements_from_state(result.state.state, kGm);

    const double raan_end = unwrap(after.raan, before.raan);
    const double measured = (raan_end - before.raan) / (orbits * period);

    // First-order secular theory: dOmega/dt = -(3/2) n J2 (R/p)^2 cos i
    const double n = std::sqrt(kGm / (a * a * a));
    const double p = a;  // circular
    const double predicted = -1.5 * n * kJ2 * (kRef / p) * (kRef / p) * std::cos(inclination);

    std::ostringstream os;
    os << "measured dRAAN/dt = " << units::rad_to_deg(measured) * 86400.0 << " deg/day, predicted "
       << units::rad_to_deg(predicted) * 86400.0 << " deg/day (ISS observed: about -5.0)";
    INFO(os.str());

    CHECK_NEAR_REL(measured, predicted, 1.5e-2,
                   "the reference is the FIRST-ORDER secular formula; what it omits are the "
                   "short-period terms, whose amplitude in RAAN is of order J2 (R/p)^2 = 9.6e-4 "
                   "rad = 0.055 deg. Over 30 orbits the secular change is 9.65 deg, so the "
                   "unmodelled oscillation is up to 0.6% of the signal. The 1.5% bound is that, "
                   "with margin for where in the orbit the sampling lands. A tighter bound would "
                   "be testing the theory, not the code");
}

TEST(apsidal_precession_changes_sign_at_the_critical_inclination) {
    // dw/dt = (3/4) n J2 (R/p)^2 (5cos^2 i - 1) vanishes at i = 63.4349 deg.
    // Molniya orbits exist because of this; a wrong J2 rarely reproduces it.
    //
    // The eccentricity matters for the TEST, not for the physics: J2 makes e
    // oscillate with amplitude ~J2 (R/p)^2 ~ 1e-3, and the short-period terms in
    // argp scale as 1/e. With e = 2e-3 the periapsis direction wanders more than
    // it precesses and the measurement is meaningless. e = 0.1 puts the
    // oscillation at 1% of e, where the secular rate is what dominates.
    J2Rig rig;

    const double a = 9.0e6;
    const double e = 0.1;
    const double p = a * (1.0 - e * e);
    const double n = std::sqrt(kGm / (a * a * a));
    const double period = units::two_pi / n;
    const double orbits = 100.0;

    for (const double degrees : {30.0, 51.6, 80.0}) {
        const double inclination = units::deg_to_rad(degrees);
        const double rp = a * (1.0 - e);
        const double vp = std::sqrt(kGm * (1.0 + e) / (a * (1.0 - e)));

        propagation::PropagationState initial{};
        initial.state.position = Vec3{rp, 0.0, 0.0};
        initial.state.velocity =
            Vec3{0.0, vp * std::cos(inclination), vp * std::sin(inclination)};

        propagation::DormandPrince54Propagator propagator{rig.forces, config()};

        const auto t0 = time::CoordinateTime::j2000();
        const auto result = propagator.propagate(initial, t0,
                                                 t0 + time::Duration::seconds(orbits * period));
        REQUIRE(result.ok());

        const auto before = trajectory::elements_from_state(initial.state, kGm);
        const auto after = trajectory::elements_from_state(result.state.state, kGm);
        const double measured =
            (unwrap(after.argument_of_periapsis, before.argument_of_periapsis) -
             before.argument_of_periapsis) / (orbits * period);

        const double predicted = 0.75 * n * kJ2 * (kRef / p) * (kRef / p) *
                                 (5.0 * std::cos(inclination) * std::cos(inclination) - 1.0);

        std::ostringstream os;
        os << "i = " << degrees << " deg: measured dargp/dt = "
           << units::rad_to_deg(measured) * 86400.0 << " deg/day, predicted "
           << units::rad_to_deg(predicted) * 86400.0 << " deg/day";
        INFO(os.str());

        CHECK_NEAR_REL(measured, predicted, 3.0e-2,
                       "first-order secular theory, same argument as for the node. The residual is "
                       "the short-period term in argp, of amplitude ~J2 (R/p)^2 / e = 1.4e-2 rad "
                       "= 0.8 deg, against a secular change of ~20 deg over 100 orbits: about 4% "
                       "in the worst phase, and much less on average. 3% is the measured margin "
                       "with room for the sampling phase; a tighter bound would be testing the "
                       "first-order theory rather than the code");

        // The sign is the real assertion here.
        CHECK((measured > 0.0) == (degrees < 63.4349));
    }
}

TEST(the_expansion_is_refused_inside_the_reference_sphere) {
    // Below R the spherical harmonic series does not merely lose accuracy: it
    // diverges.  Reporting is the only honest option.
    sft::FixedPointMassProvider provider{kEarth, kGm, 6371008.0};
    sft::FixedPoleOrientation orientation{};
    const gravity::OblatenessGravity j2{provider, orientation, kEarth,
                                        celestial::GeopotentialModel{kJ2, kRef}, kSsb};

    propagation::PropagationState inside{};
    inside.state.position = Vec3{5.0e6, 0.0, 0.0};
    const auto result = j2.evaluate(inside, time::CoordinateTime::j2000());

    CHECK(result.inside_body);
    CHECK(result.inside_of == kEarth);

    propagation::PropagationState outside{};
    outside.state.position = Vec3{6.778e6, 0.0, 0.0};
    CHECK(!j2.evaluate(outside, time::CoordinateTime::j2000()).inside_body);
}

TEST(a_body_without_a_documented_j2_is_refused_not_guessed) {
    sft::FixedPointMassProvider provider{celestial::bodies::sun, 1.32712440041e20};
    sft::FixedPoleOrientation orientation{};

    CHECK(!celestial::geopotential_for(celestial::bodies::sun).has_value());
    CHECK(celestial::geopotential_for(celestial::bodies::earth).has_value());
    CHECK_THROWS_AS(gravity::OblatenessGravity::for_body(provider, orientation,
                                                         celestial::bodies::sun, kSsb),
                    std::invalid_argument);
}

TEST(the_pole_comes_from_the_kernels_and_not_from_an_assumption) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse("2026-01-01 00:00:00 TDB");

    const Vec3 pole_j2000 =
        fixture.provider->pole_direction(celestial::bodies::earth, t, coordinates::FrameAxes::J2000);
    const Vec3 pole_ecliptic = fixture.provider->pole_direction(celestial::bodies::earth, t,
                                                                coordinates::FrameAxes::ECLIPJ2000);

    CHECK_NEAR_REL(pole_j2000.norm(), 1.0, 1.0e-12, "a rotation matrix column is a unit vector");

    const double tilt_from_z = units::rad_to_deg(angle_between(pole_j2000, Vec3::unit_z()));
    const double obliquity = units::rad_to_deg(angle_between(pole_ecliptic, Vec3::unit_z()));

    std::ostringstream os;
    os << "Earth pole in 2026: " << tilt_from_z << " deg from the J2000 z axis, " << obliquity
       << " deg from the ecliptic pole";
    INFO(os.str());

    // In J2000 equatorial axes the pole is near z, but NOT at z: precession has
    // moved it since the epoch.  If this came out as exactly zero, it would mean
    // someone had hard-coded the pole instead of asking SPICE.
    CHECK(tilt_from_z > 0.01);
    CHECK(tilt_from_z < 0.5);

    CHECK_NEAR_ABS(obliquity, 23.4393, 0.02,
                   "the angle between the Earth's pole and the ecliptic pole is the obliquity, "
                   "23.4393 deg at J2000 and decreasing by 46.8 arcsec per century. Over the 26 "
                   "years to 2026 that is 0.0034 deg, well inside the 0.02 deg bound, which is "
                   "set by the precision of the quoted reference value plus nutation (up to "
                   "9 arcsec = 0.0025 deg)");
}
