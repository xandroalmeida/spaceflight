// Orbital elements recovered from state vectors built by construction, so the
// expected answer is known exactly rather than measured.

#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <limits>

using sf::coordinates::StateVector;
using sf::math::Vec3;
using sf::trajectory::elements_from_state;

namespace {
// Earth's GM from DE440 (gm_de440.tpc, BODY399_GM = 3.9860043550702266e5 km^3/s^2).
// Hard-coded here ONLY because this test must run without kernels; the simulation
// itself never does this (docs/physics/gravity-model.md section 3).
constexpr double kEarthGm = 3.9860043550702266e14;
constexpr double kEps = std::numeric_limits<double>::epsilon();
}  // namespace

TEST(elements_of_a_circular_equatorial_orbit) {
    const double r = 7.0e6;
    const double v = std::sqrt(kEarthGm / r);
    const StateVector sv{Vec3{r, 0.0, 0.0}, Vec3{0.0, v, 0.0}};

    const auto el = elements_from_state(sv, kEarthGm);

    CHECK(el.bound);
    CHECK(el.circular);
    CHECK(el.equatorial);
    CHECK_NEAR_REL(el.semi_major_axis, r, 4.0 * kEps,
                   "a = -gm/(2*eps) with eps = v^2/2 - gm/r and v^2 = gm/r exactly by "
                   "construction; only the rounding of the sqrt and the division remains");
    CHECK_NEAR_ABS(el.eccentricity, 0.0, 1.0e-15,
                   "the eccentricity vector is a difference of two equal quantities here, so the "
                   "result is pure cancellation noise at the 1e-16 level");
    CHECK_NEAR_REL(el.periapsis_radius, r, 1.0e-14, "rp = p/(1+e) with e ~ 1e-16");
    CHECK_NEAR_REL(el.apoapsis_radius, r, 1.0e-14, "ra = p/(1-e) with e ~ 1e-16");
    CHECK_NEAR_REL(el.period, 2.0 * sf::units::pi * std::sqrt(r * r * r / kEarthGm), 1.0e-14,
                   "Kepler's third law, evaluated the same way on both sides");
    CHECK_NEAR_REL(el.specific_angular_momentum, r * v, 4.0 * kEps,
                   "h = |r x v| = r*v for a circular orbit, one product");
}

TEST(elements_of_an_eccentric_inclined_orbit) {
    // Build the orbit from its elements, then recover them.
    const double a = 2.0e7;
    const double e = 0.3;
    const double inclination = sf::units::deg_to_rad(28.5);

    // Start at periapsis, rotated into the desired inclination about the x axis.
    const double rp = a * (1.0 - e);
    const double vp = std::sqrt(kEarthGm * (1.0 + e) / (a * (1.0 - e)));

    const Vec3 position{rp, 0.0, 0.0};
    const Vec3 velocity{0.0, vp * std::cos(inclination), vp * std::sin(inclination)};
    const StateVector sv{position, velocity};

    const auto el = elements_from_state(sv, kEarthGm);

    CHECK(el.bound);
    CHECK(!el.circular);
    CHECK_NEAR_REL(el.semi_major_axis, a, 1.0e-14,
                   "a recovered from the vis-viva energy; inputs are exact by construction, "
                   "so only a handful of roundings separate the two sides");
    CHECK_NEAR_REL(el.eccentricity, e, 1.0e-14, "same argument as for a");
    CHECK_NEAR_REL(el.inclination.radians(), inclination, 1.0e-14,
                   "i = acos(h_z/|h|); acos is correctly rounded and the argument is well away "
                   "from +/-1 where it would lose precision");
    CHECK_NEAR_REL(el.periapsis_radius, rp, 1.0e-14, "rp = a(1-e), both recovered above");
    CHECK_NEAR_REL(el.apoapsis_radius, a * (1.0 + e), 1.0e-14, "ra = a(1+e)");
    CHECK_NEAR_ABS(el.true_anomaly.radians(), 0.0, 1.0e-7,
                   "the state was built at periapsis; nu is recovered through an acos whose "
                   "argument is within ~1e-16 of 1, where acos amplifies the error to sqrt(eps) "
                   "~ 1e-8. This amplification is inherent to the formulation, not a defect");
}

TEST(elements_of_a_hyperbolic_orbit_report_unbound) {
    const double r = 7.0e6;
    const double v_escape = std::sqrt(2.0 * kEarthGm / r);
    const StateVector sv{Vec3{r, 0.0, 0.0}, Vec3{0.0, 1.5 * v_escape, 0.0}};

    const auto el = elements_from_state(sv, kEarthGm);

    CHECK(!el.bound);
    CHECK(el.eccentricity > 1.0);
    CHECK(el.semi_major_axis < 0.0);
    CHECK(std::isinf(el.apoapsis_radius));
    CHECK_EQ(el.period, 0.0);
    CHECK(el.specific_energy > 0.0);
}

TEST(elements_reject_degenerate_input) {
    CHECK_THROWS_AS(elements_from_state(StateVector{}, kEarthGm), std::invalid_argument);
    CHECK_THROWS_AS(elements_from_state(StateVector{Vec3{1.0e7, 0, 0}, Vec3{0, 1.0e3, 0}}, 0.0),
                    std::invalid_argument);
}

TEST(circular_helpers_agree_with_kepler) {
    const double r = 4.2164e7;  // geostationary radius
    const double v = sf::trajectory::circular_speed(kEarthGm, r);
    const double period = sf::trajectory::circular_period(kEarthGm, r);

    CHECK_NEAR_REL(period, 2.0 * sf::units::pi * r / v, 4.0 * kEps,
                   "T = 2*pi*r/v must agree with T = 2*pi*sqrt(r^3/gm) identically; the two "
                   "expressions differ only by rounding");
    CHECK_NEAR_REL(period, 86164.0, 2.0e-5,
                   "a geostationary orbit has the period of one sidereal day, 86164.0905 s. "
                   "The 2e-5 relative bound (1.7 s) covers the rounding of the conventional "
                   "geostationary radius 42164 km, which is itself quoted to 5 digits");
}
