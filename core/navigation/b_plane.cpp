#include "core/navigation/b_plane.hpp"

#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sf::navigation {

using math::Vec3;

double BPlane::angle() const { return std::atan2(b_dot_r, b_dot_t); }

std::string BPlane::describe() const {
    std::ostringstream os;
    os << std::setprecision(9);
    os << "B.T " << b_dot_t / 1000.0 << " km, B.R " << b_dot_r / 1000.0 << " km, |B| "
       << magnitude / 1000.0 << " km, angle " << units::rad_to_deg(angle())
       << " deg, v_inf " << v_infinity << " m/s, e " << eccentricity << ", r_p "
       << periapsis_radius / 1000.0 << " km";
    return os.str();
}

BPlane b_plane_from_state(const Vec3& position_relative, const Vec3& velocity_relative,
                          double gm, const Vec3& pole) {
    if (!(gm > 0.0) || !std::isfinite(gm)) {
        throw std::invalid_argument("b_plane_from_state: gm must be finite and > 0");
    }
    const double r = position_relative.norm();
    const double v = velocity_relative.norm();
    if (!(r > 0.0) || !(v > 0.0)) {
        throw std::invalid_argument("b_plane_from_state: degenerate state");
    }

    // Hyperbolic or nothing.  An elliptical arrival has no asymptote, so it has
    // no B-plane; saying so is better than returning a struct full of NaN and
    // letting it travel (section 8).
    const double energy = 0.5 * v * v - gm / r;
    if (!(energy > 0.0)) {
        throw std::domain_error(
            "b_plane_from_state: arrival is not hyperbolic about this body (specific energy "
            "is not positive), so there is no incoming asymptote and no B-plane");
    }

    BPlane out{};
    out.v_infinity = std::sqrt(2.0 * energy);

    const Vec3 h = cross(position_relative, velocity_relative);
    const double h_norm = h.norm();
    if (!(h_norm > 0.0)) {
        throw std::domain_error("b_plane_from_state: radial trajectory has no B-plane");
    }
    const Vec3 h_hat = h / h_norm;

    // Eccentricity vector: points at periapsis, magnitude e.
    const Vec3 e_vector =
        cross(velocity_relative, h) / gm - position_relative / r;
    out.eccentricity = e_vector.norm();
    if (!(out.eccentricity > 1.0)) {
        throw std::domain_error("b_plane_from_state: eccentricity is not greater than 1");
    }
    const Vec3 e_hat = e_vector / out.eccentricity;

    out.semi_major_axis = -gm / (out.v_infinity * out.v_infinity);
    const double abs_a = -out.semi_major_axis;
    out.periapsis_radius = abs_a * (out.eccentricity - 1.0);
    out.magnitude = abs_a * std::sqrt(out.eccentricity * out.eccentricity - 1.0);

    // The incoming asymptote.  The position direction at arrival infinity is
    // -(1/e) e_hat - sqrt(e^2-1)/e (h x e); the VELOCITY there points the other
    // way, because the spacecraft is coming from there to here.
    const double inv_e = 1.0 / out.eccentricity;
    const double sin_factor =
        std::sqrt(out.eccentricity * out.eccentricity - 1.0) * inv_e;
    out.s_hat = (e_hat * inv_e + cross(h_hat, e_hat) * sin_factor).normalized();

    // B lies in the B-plane by construction: it is perpendicular to S and to the
    // orbit normal, which is what "in the plane of the hyperbola, across the
    // asymptote" means.
    out.b_vector = cross(out.s_hat, h_hat) * out.magnitude;

    const Vec3 t_raw = cross(out.s_hat, pole);
    if (!(t_raw.norm() > 0.0)) {
        throw std::domain_error(
            "b_plane_from_state: the incoming asymptote is parallel to the reference pole, "
            "so T and R are undefined; pass a different pole");
    }
    out.t_hat = t_raw.normalized();
    out.r_hat = cross(out.s_hat, out.t_hat);

    out.b_dot_t = dot(out.b_vector, out.t_hat);
    out.b_dot_r = dot(out.b_vector, out.r_hat);
    return out;
}

double impact_parameter_for_periapsis(double periapsis_radius, double gm, double v_infinity) {
    if (!(periapsis_radius > 0.0) || !(gm > 0.0) || !(v_infinity > 0.0)) {
        throw std::invalid_argument("impact_parameter_for_periapsis: arguments must be > 0");
    }
    // b = r_p sqrt(1 + 2 mu / (r_p v_inf^2)).  For the Moon at v_inf = 843.5 m/s
    // and a 100 km periapsis altitude this is 2.92 -- the whole reason aiming at a
    // point does not work.
    return periapsis_radius *
           std::sqrt(1.0 + 2.0 * gm / (periapsis_radius * v_infinity * v_infinity));
}

double periapsis_for_impact_parameter(double impact_parameter, double gm, double v_infinity) {
    if (!(impact_parameter >= 0.0) || !(gm > 0.0) || !(v_infinity > 0.0)) {
        throw std::invalid_argument("periapsis_for_impact_parameter: invalid arguments");
    }
    // The same relation solved for r_p.  Written as a difference of a hypot and a
    // term rather than by the quadratic formula, because the two roots differ by
    // exactly this cancellation and the other one is negative.
    const double focus = gm / (v_infinity * v_infinity);
    return std::hypot(focus, impact_parameter) - focus;
}

BPlaneTarget aim_for_periapsis(double periapsis_radius, double gm, double v_infinity,
                               double plane_angle) {
    const double b = impact_parameter_for_periapsis(periapsis_radius, gm, v_infinity);
    return BPlaneTarget{b * std::cos(plane_angle), b * std::sin(plane_angle)};
}

InsertionBurn plan_insertion(double periapsis_radius, double gm, double v_infinity,
                             double apoapsis_radius) {
    if (!(periapsis_radius > 0.0) || !(gm > 0.0) || !(v_infinity > 0.0)) {
        throw std::invalid_argument("plan_insertion: arguments must be > 0");
    }

    InsertionBurn burn{};
    burn.periapsis_speed =
        std::sqrt(v_infinity * v_infinity + 2.0 * gm / periapsis_radius);

    double semi_major = periapsis_radius;    // circular
    if (apoapsis_radius > periapsis_radius) {
        semi_major = 0.5 * (periapsis_radius + apoapsis_radius);
    }
    // vis-viva at periapsis of the captured orbit
    burn.target_speed = std::sqrt(gm * (2.0 / periapsis_radius - 1.0 / semi_major));
    burn.delta_v = burn.periapsis_speed - burn.target_speed;
    burn.period = units::two_pi * std::sqrt(semi_major * semi_major * semi_major / gm);
    return burn;
}

}  // namespace sf::navigation
