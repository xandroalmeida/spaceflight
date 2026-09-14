// Does the departure hyperbola actually leave along the asymptote it was asked
// for?
//
// The question matters because the alternative -- burning prograde with the
// patched-conic SPEED and hoping -- is indistinguishable from this one on a
// spreadsheet and misses Mars by tens of millions of kilometres in flight. So
// the checks here are not "the numbers look plausible": they reconstruct the
// conic from the returned state and ask whether ITS asymptote is the requested
// one, which is the only property the planner is relying on.
//
// No ephemeris, no propagation, no kernels: pure two-body arithmetic, so this
// test never skips.

#include "core/math/vec3.hpp"
#include "core/trajectory/departure_hyperbola.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>

using sf::math::Vec3;
using sf::trajectory::departure_onto_asymptote;
using sf::trajectory::speed_for_asymptote;

namespace {

// Earth, from gm_de440.tpc, to the digits this test needs.
constexpr double kGmEarth = 3.986004355e14;   // [m^3/s^2]
constexpr double kParkingRadius = 6378.137e3 + 400.0e3;

// The outgoing asymptote of a hyperbola, rebuilt from a state vector by the
// independent route: elements first, then the asymptote from the eccentricity
// vector and the angular momentum. If this agrees with what was requested, the
// solver did the geometry and not just the arithmetic.
Vec3 asymptote_of(const Vec3& position, const Vec3& velocity, double gm) {
    const Vec3 h = sf::math::cross(position, velocity);
    const double r = position.norm();
    const Vec3 e_vec =
        (sf::math::cross(velocity, h) / gm) - position / r;
    const double e = e_vec.norm();
    const Vec3 e_hat = e_vec / e;
    const Vec3 p_hat = sf::math::cross(h.normalized(), e_hat);

    // At infinity the position direction tends to the asymptote, at the true
    // anomaly where the denominator of the conic equation vanishes:
    // cos(nu_inf) = -1/e.
    const double cos_nu = -1.0 / e;
    const double sin_nu = std::sqrt(1.0 - cos_nu * cos_nu);
    return (e_hat * cos_nu + p_hat * sin_nu).normalized();
}

}  // namespace

TEST(speed_matches_the_patched_conic_formula) {
    // Rule 34's formula, as an independent check on the energy: the solver is
    // free to pick any direction it likes, and the speed is not negotiable.
    const double v_inf = 3000.0;
    const Vec3 position{kParkingRadius, 0.0, 0.0};
    const Vec3 v_infinity{0.0, v_inf, 0.0};

    const auto departure = departure_onto_asymptote(position, v_infinity, kGmEarth);
    REQUIRE(departure.ok);

    const double v_escape = std::sqrt(2.0 * kGmEarth / kParkingRadius);
    const double expected = std::sqrt(v_inf * v_inf + v_escape * v_escape);

    CHECK_NEAR_REL(departure.speed, expected, 1.0e-12,
                   "vis-viva is an identity, not an approximation: any disagreement beyond "
                   "double rounding means the returned state is not on the conic it claims");
    CHECK_NEAR_REL(speed_for_asymptote(kParkingRadius, v_inf, kGmEarth), expected, 1.0e-12,
                   "the screening helper must agree with the full solve exactly; they are the "
                   "same equation");
}

TEST(the_outgoing_asymptote_is_the_one_that_was_asked_for) {
    // Six geometries: the departure point swept around the asymptote direction.
    // The point of sweeping is that the ANSWER changes -- the required flight
    // path angle is different at every one of them -- while the property being
    // checked does not.
    const double v_inf = 3000.0;
    const Vec3 s_hat = Vec3{0.3, 0.9, 0.316}.normalized();

    for (const double degrees : {20.0, 45.0, 75.0, 100.0, 130.0, 160.0}) {
        const double theta = sf::units::deg_to_rad(degrees);
        // A position at angle `theta` from the asymptote, in a plane containing it.
        const Vec3 perpendicular =
            sf::math::cross(s_hat, Vec3{0.0, 0.0, 1.0}).normalized();
        const Vec3 position =
            (s_hat * std::cos(theta) + perpendicular * std::sin(theta)) * kParkingRadius;

        const auto departure = departure_onto_asymptote(position, s_hat * v_inf, kGmEarth);
        REQUIRE(departure.ok);

        const Vec3 achieved = asymptote_of(position, departure.velocity, kGmEarth);
        const double error_deg =
            sf::units::rad_to_deg(sf::math::angle_between(achieved, s_hat));

        CHECK_NEAR_ABS(error_deg, 0.0, 1.0e-4,
                       "the eccentricity is found by 200 bisections on a bracket that starts "
                       "at [1, 2], so it is resolved to ~1e-60 and the residual here is the "
                       "conditioning of rebuilding the conic from the state, not the solve");

        // And the conic really is a hyperbola whose energy matches the request.
        const sf::coordinates::StateVector state{position, departure.velocity};
        const auto elements = sf::trajectory::elements_from_state(state, kGmEarth);
        CHECK(elements.eccentricity > 1.0);
        CHECK_NEAR_REL(elements.eccentricity, departure.eccentricity, 1.0e-9,
                       "the eccentricity the solver reports and the one the independent "
                       "element conversion reads off the same state must be the same number");
    }
}

TEST(a_departure_point_along_the_asymptote_is_refused) {
    // Not a numerical edge case to be nudged past: standing exactly along the
    // direction you need to leave in means every orbital plane is equally good,
    // and the honest answer is that this departure point does not define a
    // transfer. The search skips it; it must not get a silently arbitrary plane.
    const Vec3 s_hat{0.0, 1.0, 0.0};
    const auto along = departure_onto_asymptote(s_hat * kParkingRadius, s_hat * 3000.0, kGmEarth);
    CHECK(!along.ok);
    const auto against =
        departure_onto_asymptote(s_hat * -kParkingRadius, s_hat * 3000.0, kGmEarth);
    CHECK(!against.ok);

    // A parabolic escape has no asymptotic speed to aim along.
    const auto parabolic =
        departure_onto_asymptote(Vec3{kParkingRadius, 0.0, 0.0}, Vec3{}, kGmEarth);
    CHECK(!parabolic.ok);
}

TEST(periapsis_of_the_departure_conic_is_reported_and_can_be_below_the_surface) {
    // The whole reason the planner asks for this: a departure geometry can be
    // perfectly valid as a conic and dive through the planet on the way out.
    // Milestone 6 lost 46 of 59 cases to exactly that, and the value has to come
    // back so the screen can price it.
    const double v_inf = 3000.0;
    const Vec3 s_hat{0.0, 1.0, 0.0};

    // Nearly opposite the asymptote: the ship has to swing right around the body,
    // which puts periapsis far inside it.
    const double theta = sf::units::deg_to_rad(170.0);
    const Vec3 position = Vec3{std::sin(theta), std::cos(theta), 0.0} * kParkingRadius;

    const auto departure = departure_onto_asymptote(position, s_hat * v_inf, kGmEarth);
    REQUIRE(departure.ok);
    CHECK(departure.periapsis_radius > 0.0);
    CHECK(departure.periapsis_radius < kParkingRadius);
    CHECK_NEAR_REL(departure.semi_major_axis, -kGmEarth / (v_inf * v_inf), 1.0e-12,
                   "the semi-major axis of a hyperbola is fixed by the excess velocity alone, "
                   "independent of where the departure happens");
}

TEST(a_tangential_departure_reproduces_the_textbook_case) {
    // The one geometry where the closed form is known: leaving from PERIAPSIS,
    // the velocity is perpendicular to the radius and the asymptote is turned
    // away from it by nu_inf = arccos(-1/e). Asking the solver for a departure at
    // that angle must give back a flight path angle of zero.
    const double v_inf = 3000.0;
    const double r = kParkingRadius;
    const double v_p = std::sqrt(v_inf * v_inf + 2.0 * kGmEarth / r);
    // Periapsis of the conic through r with this speed and zero flight path angle.
    const double e = 1.0 + r * v_inf * v_inf / kGmEarth;
    const double nu_infinity = std::acos(-1.0 / e);

    const Vec3 r_hat{1.0, 0.0, 0.0};
    const Vec3 t_hat{0.0, 1.0, 0.0};
    const Vec3 s_hat = r_hat * std::cos(nu_infinity) + t_hat * std::sin(nu_infinity);

    const auto departure = departure_onto_asymptote(r_hat * r, s_hat * v_inf, kGmEarth);
    REQUIRE(departure.ok);

    CHECK_NEAR_ABS(sf::units::rad_to_deg(departure.flight_path_angle), 0.0, 1.0e-5,
                   "departing from periapsis is the definition of a zero flight path angle; a "
                   "non-zero answer here would mean the solver picked a different conic than "
                   "the closed form it is being compared with");
    CHECK_NEAR_REL(departure.speed, v_p, 1.0e-12, "vis-viva again, at the one point where the "
                                                  "textbook formula applies unmodified");
    CHECK_NEAR_REL(departure.periapsis_radius, r, 1.0e-9,
                   "if the flight path angle is zero the departure point IS periapsis");
}
