// Orbital elements recovered from state vectors built by construction, so the
// expected answer is known exactly rather than measured.

#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

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

// ---------------------------------------------------------------------------
// state_from_elements: the inverse, checked as a round trip.
//
// A round trip is the right test for an inverse and a weak test for either half
// on its own -- two rotations that are each wrong in opposite ways compose to
// the identity.  So the first case pins the ABSOLUTE geometry of a case whose
// answer is known by inspection, and the round trips then check that the general
// rotation agrees with it.
// ---------------------------------------------------------------------------

TEST(state_from_elements_places_a_known_orbit_where_it_belongs) {
    // Circular, equatorial, at the ascending node: the ship is on +x moving
    // along +y.  No rotation is involved, so any error here is in the perifocal
    // construction and nowhere else.
    sf::trajectory::OrbitalElements el{};
    el.semi_major_axis = 7.0e6;
    el.eccentricity = 0.0;
    el.inclination = sf::units::Angle::radians(0.0);
    el.raan = sf::units::Angle::radians(0.0);
    el.argument_of_periapsis = sf::units::Angle::radians(0.0);
    el.true_anomaly = sf::units::Angle::radians(0.0);

    const auto sv = sf::trajectory::state_from_elements(el, kEarthGm);
    const double v = std::sqrt(kEarthGm / 7.0e6);

    CHECK_NEAR_REL(sv.position.x, 7.0e6, 4.0 * kEps, "r = p/(1+e cos nu) = a exactly for e = 0");
    CHECK_NEAR_ABS(sv.position.y, 0.0, 1.0e-9, "sin(0) is exactly 0; only the products round");
    CHECK_NEAR_ABS(sv.position.z, 0.0, 1.0e-9, "an equatorial orbit has no z component");
    CHECK_NEAR_ABS(sv.velocity.x, 0.0, 1.0e-9, "at periapsis the velocity is purely transverse");
    CHECK_NEAR_REL(sv.velocity.y, v, 4.0 * kEps, "sqrt(gm/p)*(e + cos nu) = sqrt(gm/a) for e = 0");
    CHECK_NEAR_ABS(sv.velocity.z, 0.0, 1.0e-9, "an equatorial orbit has no z velocity");

    // A quarter of a turn later the ship is on +y moving along -x.
    el.true_anomaly = sf::units::Angle::degrees(90.0);
    const auto quarter = sf::trajectory::state_from_elements(el, kEarthGm);
    CHECK_NEAR_ABS(quarter.position.x, 0.0, 1.0e-8, "cos(90 deg) is zero to rounding");
    CHECK_NEAR_REL(quarter.position.y, 7.0e6, 1.0e-14, "sin(90 deg) = 1");
    CHECK_NEAR_REL(quarter.velocity.x, -v, 1.0e-14, "-sqrt(gm/p) sin nu");
    CHECK_NEAR_ABS(quarter.velocity.y, 0.0, 1.0e-6,
                   "e + cos(90 deg) is zero to rounding, times a speed of 7.5 km/s");
}

TEST(elements_and_state_are_inverses) {
    // Three orbits that between them exercise every rotation in the 3-1-3: an
    // inclined ellipse away from every node, a retrograde one, and a hyperbola.
    struct Case {
        const char* name;
        double a;
        double e;
        double i_deg;
        double raan_deg;
        double argp_deg;
        double nu_deg;
    };
    const Case cases[] = {
        {"inclined ellipse", 2.0e7, 0.3, 51.6, 137.0, 42.0, 73.0},
        {"retrograde ellipse", 1.1e7, 0.12, 115.0, 300.0, 210.0, 250.0},
        {"hyperbola", -3.0e7, 1.7, 28.5, 20.0, 95.0, 33.0},
    };

    for (const auto& c : cases) {
        INFO(std::string{"  case: "} + c.name);
        sf::trajectory::OrbitalElements el{};
        el.semi_major_axis = c.a;
        el.eccentricity = c.e;
        el.inclination = sf::units::Angle::degrees(c.i_deg);
        el.raan = sf::units::Angle::degrees(c.raan_deg);
        el.argument_of_periapsis = sf::units::Angle::degrees(c.argp_deg);
        el.true_anomaly = sf::units::Angle::degrees(c.nu_deg);

        const auto sv = sf::trajectory::state_from_elements(el, kEarthGm);
        const auto back = elements_from_state(sv, kEarthGm);

        // 1e-12 relative: the round trip is a dozen trigonometric evaluations
        // and two vector rotations, each losing a few units in the last place of
        // a double, against quantities of order 1e7.  Nothing here approximates
        // anything -- the bound is pure floating point accumulation, and it is
        // set four orders of magnitude above the 1e-16 epsilon rather than at it
        // because the angles go through acos, which loses half the digits near
        // its endpoints.
        CHECK_NEAR_REL(back.semi_major_axis, c.a, 1.0e-12, "a recovered from the energy");
        CHECK_NEAR_REL(back.eccentricity, c.e, 1.0e-12, "|e_vec| recovered from the state");
        CHECK_NEAR_REL(back.inclination.degrees(), c.i_deg, 1.0e-12, "acos(h_z/|h|)");
        CHECK_NEAR_REL(back.raan.degrees(), c.raan_deg, 1.0e-12, "acos(n_x/|n|), quadrant fixed");
        CHECK_NEAR_REL(back.argument_of_periapsis.degrees(), c.argp_deg, 1.0e-11,
                       "acos(n.e/(|n||e|)); the extra decade covers the three-way product");
        CHECK_NEAR_REL(back.true_anomaly.degrees(), c.nu_deg, 1.0e-11, "acos(e.r/(|e||r|))");
    }
}

TEST(state_from_elements_refuses_a_conic_that_is_not_one) {
    sf::trajectory::OrbitalElements el{};
    // a > 0 with e > 1 gives a negative semi-latus rectum: no such conic.
    el.semi_major_axis = 1.0e7;
    el.eccentricity = 1.5;
    CHECK_THROWS_AS(sf::trajectory::state_from_elements(el, kEarthGm), std::invalid_argument);

    // Past the asymptote of a real hyperbola.
    el.semi_major_axis = -1.0e7;
    el.true_anomaly = sf::units::Angle::degrees(179.0);
    CHECK_THROWS_AS(sf::trajectory::state_from_elements(el, kEarthGm), std::invalid_argument);

    el.eccentricity = 0.0;
    el.semi_major_axis = 1.0e7;
    CHECK_THROWS_AS(sf::trajectory::state_from_elements(el, 0.0), std::invalid_argument);
}
