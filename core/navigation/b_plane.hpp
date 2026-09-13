#pragma once

// Aiming a FLYBY instead of a body.
//
// `orbit-cli intercept` corrects a departure velocity until the full model puts
// the spacecraft at the target's POSITION -- and arriving at the centre of a body
// is, by definition, hitting it.  Asking instead for "pass at 100 km altitude"
// cannot be done by aiming at a point 100 km off centre, because the body pulls:
// the distance aimed at is not the distance flown past.
//
//     B = r_p sqrt(1 + 2 mu / (r_p v_inf^2))
//
// For the Moon, with the 843.5 m/s arrival of the 4.5-day transfer, grazing at
// 100 km needs an impact parameter of 5357 km -- 2.9 times the periapsis radius.
//
// The B-plane is the plane through the body's centre perpendicular to the
// INCOMING ASYMPTOTE, and B is where that asymptote pierces it.  Its two
// components separate what distance alone confuses: the magnitude sets how close
// the flyby passes, the angle sets which side -- and therefore the plane of any
// orbit an insertion burn would leave the spacecraft in.
//
// See docs/physics/b-plane.md.

#include "core/math/vec3.hpp"

#include <string>

namespace sf::navigation {

struct BPlane {
    // The frame.  S is the incoming asymptote; T and R span the B-plane.
    math::Vec3 s_hat{};
    math::Vec3 t_hat{};
    math::Vec3 r_hat{};

    math::Vec3 b_vector{};       // [m], from the body centre, lies in the plane
    double b_dot_t{0.0};         // [m] -- the two numbers a corrector aims at
    double b_dot_r{0.0};         // [m]
    double magnitude{0.0};       // [m], = sqrt(b_dot_t^2 + b_dot_r^2) = |a| sqrt(e^2-1)

    double v_infinity{0.0};      // [m/s]
    double eccentricity{0.0};    // > 1
    double semi_major_axis{0.0}; // [m], NEGATIVE for a hyperbola
    double periapsis_radius{0.0};// [m]

    // The angle of B in the plane, measured from T towards R.  This is the knob
    // that picks which side of the body the flyby passes, and it has no natural
    // default -- whoever plans the mission says which (section 4).
    [[nodiscard]] double angle() const;

    [[nodiscard]] std::string describe() const;
};

// The reference pole that orients T and R.  Pure convention: rotating it rotates
// T and R together and does not move B, which the test checks.  J2000's pole,
// because it is the integration frame's own axis and needs no rotation.
inline constexpr math::Vec3 kDefaultBPlanePole{0.0, 0.0, 1.0};

// Throws std::domain_error when the state is not hyperbolic about this body: an
// elliptical arrival has no asymptote and therefore no B-plane, and returning a
// NaN-filled struct would let that travel.
[[nodiscard]] BPlane b_plane_from_state(const math::Vec3& position_relative,
                                        const math::Vec3& velocity_relative,
                                        double gm,
                                        const math::Vec3& pole = kDefaultBPlanePole);

// Gravitational focusing, both ways (section 2 and section 4).
[[nodiscard]] double impact_parameter_for_periapsis(double periapsis_radius, double gm,
                                                    double v_infinity);
[[nodiscard]] double periapsis_for_impact_parameter(double impact_parameter, double gm,
                                                    double v_infinity);

// The aim point, as the two numbers the corrector drives to zero.
struct BPlaneTarget {
    double b_dot_t{0.0};
    double b_dot_r{0.0};
};

[[nodiscard]] BPlaneTarget aim_for_periapsis(double periapsis_radius, double gm,
                                             double v_infinity, double plane_angle);

// Insertion at periapsis: retrograde, impulsive, into a circle or an ellipse.
struct InsertionBurn {
    double periapsis_speed{0.0};   // [m/s] on the arrival hyperbola
    double target_speed{0.0};      // [m/s] after the burn
    double delta_v{0.0};           // [m/s], positive = retrograde
    double period{0.0};            // [s] of the resulting orbit
};

// `apoapsis_radius <= 0` means circularise.
[[nodiscard]] InsertionBurn plan_insertion(double periapsis_radius, double gm,
                                           double v_infinity,
                                           double apoapsis_radius = 0.0);

}  // namespace sf::navigation
