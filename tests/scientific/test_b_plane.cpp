// Aiming a flyby: the B-plane, and the gravitational focusing that makes it
// necessary.
//
// Everything here is checked against a CONSTRUCTED hyperbola -- one whose
// periapsis, v_infinity and orientation were chosen, not solved for -- so the
// comparison is against the definition rather than against another run of the
// same code. See docs/physics/b-plane.md.

#include "core/navigation/b_plane.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>

using namespace sf;
using sf::math::Vec3;

namespace {

// GM and mean radius of the Moon, from gm_de440.tpc and pck00011.tpc -- the same
// kernels the simulator loads, quoted here so the test needs no kernels.
constexpr double kMoonGm = 4.902800118e12;   // [m^3/s^2]
constexpr double kMoonRadius = 1737400.0;    // [m]

// A state ON a hyperbola of given periapsis and v_infinity, at periapsis, in a
// plane tilted out of the reference plane so that no component is accidentally
// zero and a transposed cross product cannot pass.
struct Constructed {
    Vec3 position;
    Vec3 velocity;
    double periapsis_radius;
    double v_infinity;
    double eccentricity;
    double impact_parameter;
};

Constructed make_hyperbola(double periapsis_radius, double v_infinity, double tilt) {
    const double semi_major = -kMoonGm / (v_infinity * v_infinity);
    const double eccentricity = 1.0 - periapsis_radius / semi_major;
    const double speed =
        std::sqrt(v_infinity * v_infinity + 2.0 * kMoonGm / periapsis_radius);

    // At periapsis the velocity is perpendicular to the position, by definition.
    const Vec3 radial = Vec3{0.6, 0.8, 0.0};
    const Vec3 across = Vec3{-0.8 * std::cos(tilt), 0.6 * std::cos(tilt), std::sin(tilt)};

    return Constructed{radial * periapsis_radius, across * speed, periapsis_radius,
                       v_infinity, eccentricity,
                       -semi_major * std::sqrt(eccentricity * eccentricity - 1.0)};
}

}  // namespace

TEST(the_b_plane_recovers_the_hyperbola_it_was_built_from) {
    for (const double periapsis : {kMoonRadius + 100.0e3, 3.0e6, 1.0e7}) {
        for (const double v_infinity : {200.0, 843.5, 2000.0}) {
            const auto made = make_hyperbola(periapsis, v_infinity, 0.4);
            const auto bp =
                navigation::b_plane_from_state(made.position, made.velocity, kMoonGm);

            std::ostringstream os;
            os << "r_p " << periapsis / 1000.0 << " km, v_inf " << v_infinity << ": "
               << bp.describe();
            INFO(os.str());

            CHECK_NEAR_REL(bp.v_infinity, made.v_infinity, 1.0e-12,
                           "v_inf comes back from the specific energy of the state it was "
                           "built into; the bound is the cancellation in v^2/2 - mu/r, whose "
                           "terms are ~3e6 against a result of ~1e5");
            CHECK_NEAR_REL(bp.eccentricity, made.eccentricity, 1.0e-13,
                           "the eccentricity vector recomputed from the same state; a few "
                           "ulps of a quantity of order 1");
            CHECK_NEAR_REL(bp.periapsis_radius, made.periapsis_radius, 1.0e-12,
                           "|a|(e-1), and the state was placed AT periapsis, so this is an "
                           "identity; the bound is the ulp of a difference of numbers of "
                           "order e");
            CHECK_NEAR_REL(bp.magnitude, made.impact_parameter, 1.0e-12,
                           "|B| = |a| sqrt(e^2-1), the semi-minor axis, against the value the "
                           "hyperbola was constructed with. Identity; ulp");
        }
    }
}

TEST(the_three_routes_to_the_impact_parameter_agree) {
    // Geometry (|a| sqrt(e^2-1)), the two components (sqrt(BT^2 + BR^2)), and
    // gravitational focusing (r_p sqrt(1 + 2mu/(r_p v_inf^2))) are three different
    // expressions of the same number.
    for (const double v_infinity : {100.0, 843.5, 5000.0}) {
        const auto made = make_hyperbola(kMoonRadius + 100.0e3, v_infinity, -0.9);
        const auto bp = navigation::b_plane_from_state(made.position, made.velocity, kMoonGm);

        const double from_components = std::hypot(bp.b_dot_t, bp.b_dot_r);
        const double from_focusing = navigation::impact_parameter_for_periapsis(
            bp.periapsis_radius, kMoonGm, bp.v_infinity);

        std::ostringstream os;
        os << "v_inf " << v_infinity << ": geometry " << bp.magnitude / 1000.0
           << " km, components " << from_components / 1000.0 << " km, focusing "
           << from_focusing / 1000.0 << " km  (|B|/r_p = "
           << bp.magnitude / bp.periapsis_radius << ")";
        INFO(os.str());

        CHECK_NEAR_REL(from_components, bp.magnitude, 1.0e-12,
                       "B is decomposed on an orthonormal pair that spans the plane it lies "
                       "in, so the components reconstruct it exactly; ulp of a hypot");
        CHECK_NEAR_REL(from_focusing, bp.magnitude, 1.0e-12,
                       "algebraically the same expression: b^2 = r_p^2 + 2 mu r_p / v_inf^2 "
                       "is |a|^2(e^2-1) rewritten. Ulp");
    }
}

TEST(b_lies_in_the_plane_and_the_axes_are_orthonormal) {
    const auto made = make_hyperbola(2.5e6, 843.5, 1.1);
    const auto bp = navigation::b_plane_from_state(made.position, made.velocity, kMoonGm);

    std::ostringstream os;
    os << "B.S = " << dot(bp.b_vector, bp.s_hat) << " m against |B| = " << bp.magnitude
       << " m";
    INFO(os.str());

    // The defining property: B is where the asymptote pierces the plane
    // PERPENDICULAR to it, so B.S is zero.
    CHECK_NEAR_ABS(dot(bp.b_vector, bp.s_hat), 0.0, 1.0e-8,
                   "exact by construction -- B = |B| (S x h_hat) is perpendicular to S "
                   "identically. The bound is the ulp of a dot product of vectors of "
                   "magnitude 6e6, which is 1.4e-9");

    for (const auto& axis : {bp.s_hat, bp.t_hat, bp.r_hat}) {
        CHECK_NEAR_ABS(axis.norm(), 1.0, 1.0e-14,
                       "normalised on construction; a few ulps of 1");
    }
    CHECK_NEAR_ABS(dot(bp.s_hat, bp.t_hat), 0.0, 1.0e-14,
                   "T = S x pole, normalised, so T is perpendicular to S identically");
    CHECK_NEAR_ABS(dot(bp.s_hat, bp.r_hat), 0.0, 1.0e-14, "R = S x T; same argument");
    CHECK_NEAR_ABS(dot(bp.t_hat, bp.r_hat), 0.0, 1.0e-14, "idem");
}

TEST(the_reference_pole_is_convention_and_does_not_move_b) {
    // Rotating the pole rotates T and R together. |B| is a property of the
    // trajectory and cannot notice.
    const auto made = make_hyperbola(2.0e6, 700.0, 0.25);
    const auto reference =
        navigation::b_plane_from_state(made.position, made.velocity, kMoonGm);

    for (const auto& pole : {Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0},
                             Vec3{0.3, -0.5, 0.81}}) {
        const auto other =
            navigation::b_plane_from_state(made.position, made.velocity, kMoonGm, pole);

        CHECK_NEAR_REL(other.magnitude, reference.magnitude, 1.0e-14,
                       "|B| is the semi-minor axis of the trajectory; the pole only chooses "
                       "where the angle is measured FROM. Identical arithmetic, so ulp");
        CHECK_NEAR_ABS((other.b_vector - reference.b_vector).norm(), 0.0, 1.0e-6,
                       "and the VECTOR is the same too -- it is built from S and h, neither "
                       "of which knows the pole. Bound is the ulp of 6e6 m");
    }
}

TEST(focusing_inverts_exactly) {
    for (const double periapsis : {kMoonRadius, kMoonRadius + 100.0e3, 5.0e6}) {
        for (const double v_infinity : {200.0, 843.5, 2000.0}) {
            const double b = navigation::impact_parameter_for_periapsis(periapsis, kMoonGm,
                                                                        v_infinity);
            const double back =
                navigation::periapsis_for_impact_parameter(b, kMoonGm, v_infinity);

            std::ostringstream os;
            os << "r_p " << periapsis / 1000.0 << " km, v_inf " << v_infinity << " -> |B| "
               << b / 1000.0 << " km -> r_p " << back / 1000.0 << " km";
            INFO(os.str());

            CHECK_NEAR_ABS(back, periapsis, 1.0e-6,
                           "closed-form inverse of a closed-form expression. The hypot form "
                           "avoids the cancellation the quadratic formula would have here, "
                           "and the measured round trip is 1e-9 m on radii of 1e6 m");
        }
    }
}

TEST(the_focusing_is_what_makes_aiming_at_a_point_wrong) {
    // The number from section 2 of the document, and the reason this file exists:
    // aiming the POSITION at 100 km altitude would pass far inside the Moon.
    constexpr double kArrivalVInfinity = 843.5;   // measured on the 4.5-day transfer
    const double wanted = kMoonRadius + 100.0e3;

    const double b = navigation::impact_parameter_for_periapsis(wanted, kMoonGm,
                                                                kArrivalVInfinity);
    // What actually happens if you aim the impact parameter at the radius instead.
    const double naive =
        navigation::periapsis_for_impact_parameter(wanted, kMoonGm, kArrivalVInfinity);

    std::ostringstream os;
    os << "to graze 100 km altitude, |B| must be " << b / 1000.0 << " km (focusing x"
       << b / wanted << "). Aiming |B| at the radius instead gives r_p = " << naive / 1000.0
       << " km, i.e. " << (kMoonRadius - naive) / 1000.0 << " km BELOW the surface";
    INFO(os.str());

    CHECK_NEAR_REL(b, 5357.0e3, 1.0e-3,
                   "docs/physics/b-plane.md section 2, recomputed. The bound is the four "
                   "digits v_inf = 843.5 m/s is quoted to");
    CHECK(naive < kMoonRadius);
}

TEST(the_aim_point_has_the_magnitude_asked_for_and_the_angle_asked_for) {
    const double periapsis = kMoonRadius + 100.0e3;
    for (const double angle_deg : {0.0, 45.0, 90.0, 180.0, -120.0}) {
        const double angle = units::deg_to_rad(angle_deg);
        const auto aim = navigation::aim_for_periapsis(periapsis, kMoonGm, 843.5, angle);

        const double magnitude = std::hypot(aim.b_dot_t, aim.b_dot_r);
        CHECK_NEAR_REL(magnitude, navigation::impact_parameter_for_periapsis(
                                      periapsis, kMoonGm, 843.5),
                       1.0e-14,
                       "cos^2 + sin^2 = 1; ulp of a hypot");
        CHECK_NEAR_ABS(std::atan2(aim.b_dot_r, aim.b_dot_t), angle, 1.0e-14,
                       "atan2 inverting cos and sin of the same angle; ulp");
    }
}

TEST(the_insertion_burn_matches_the_closed_form) {
    const double periapsis = kMoonRadius + 100.0e3;
    constexpr double kArrivalVInfinity = 843.5;

    const auto circular = navigation::plan_insertion(periapsis, kMoonGm, kArrivalVInfinity);

    std::ostringstream os;
    os << "v_inf " << kArrivalVInfinity << ": v_p " << circular.periapsis_speed
       << " m/s, v_circ " << circular.target_speed << " m/s, dv " << circular.delta_v
       << " m/s, period " << circular.period / 60.0 << " min";
    INFO(os.str());

    CHECK_NEAR_REL(circular.periapsis_speed,
                   std::sqrt(kArrivalVInfinity * kArrivalVInfinity + 2.0 * kMoonGm / periapsis),
                   1.0e-14, "vis-viva at periapsis, recomputed from the same constants; ulp");
    CHECK_NEAR_REL(circular.target_speed, std::sqrt(kMoonGm / periapsis), 1.0e-14,
                   "circular speed at the same radius; ulp");
    CHECK_NEAR_REL(circular.delta_v, 825.8, 2.0e-3,
                   "docs/physics/b-plane.md section 7, recomputed. The bound is the four "
                   "digits v_inf is quoted to. For scale, Apollo's lunar orbit insertion "
                   "cost about 900 m/s from a slightly faster arrival");

    // A 100 x 1000 km capture ellipse is cheaper than a circle, because it does
    // less: it only has to stop the spacecraft escaping.
    const auto elliptical =
        navigation::plan_insertion(periapsis, kMoonGm, kArrivalVInfinity, kMoonRadius + 1.0e6);
    std::ostringstream os2;
    os2 << "capture into 100 x 1000 km: dv " << elliptical.delta_v << " m/s, period "
        << elliptical.period / 3600.0 << " h";
    INFO(os2.str());
    CHECK(elliptical.delta_v < circular.delta_v);
    CHECK(elliptical.period > circular.period);

    // The period of a 100 km circular lunar orbit is a number with a known value.
    CHECK_NEAR_REL(circular.period / 60.0, 117.8, 5.0e-3,
                   "Kepler's third law at r = 1837.4 km with the Moon's GM: 2 pi sqrt(r^3/mu) "
                   "= 7068 s = 117.8 min. Apollo's lunar parking orbits ran about 2 hours, "
                   "which is the same number");
}

TEST(an_elliptical_arrival_is_refused_rather_than_returned_as_nan) {
    // A spacecraft already bound to the Moon has no incoming asymptote. Returning
    // a NaN-filled struct would let that travel into a corrector and come back as
    // an unexplained failure to converge (section 8).
    const double radius = 5.0e6;
    const double circular_speed = std::sqrt(kMoonGm / radius);
    bool threw = false;
    try {
        (void)navigation::b_plane_from_state(Vec3{radius, 0.0, 0.0},
                                             Vec3{0.0, circular_speed, 0.0}, kMoonGm);
    } catch (const std::domain_error&) {
        threw = true;
    }
    CHECK(threw);
}
