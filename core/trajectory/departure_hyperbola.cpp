#include "core/trajectory/departure_hyperbola.hpp"

#include "core/units/constants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sf::trajectory {
namespace {

using math::Vec3;

// The distance at which a hyperbola of eccentricity `e` crosses the direction
// that sits `theta` radians before its outgoing asymptote.
//
// This is the function whose root is the answer.  It runs from 0 to infinity as
// e runs from 1 to infinity, which is why a bracket always exists:
//
//   e -> 1+   the hyperbola degenerates to a parabola of zero semi-latus rectum,
//             p = a(1-e^2) -> 0, and the crossing distance goes to zero;
//   e -> inf  p grows like e^2 while the denominator grows like e, so the
//             crossing distance grows without bound.
[[nodiscard]] double radius_at(double e, double theta, double gm, double v_infinity_squared) {
    const double p = (gm / v_infinity_squared) * (e * e - 1.0);   // = a(1-e^2) > 0
    const double nu_infinity = std::acos(-1.0 / e);
    const double nu = nu_infinity - theta;
    const double denominator = 1.0 + e * std::cos(nu);
    if (!(denominator > 0.0)) {
        return std::numeric_limits<double>::infinity();
    }
    return p / denominator;
}

}  // namespace

double speed_for_asymptote(double radius, double v_infinity, double gm) {
    if (!(radius > 0.0) || !(gm > 0.0)) {
        return 0.0;
    }
    return std::sqrt(v_infinity * v_infinity + 2.0 * gm / radius);
}

DepartureHyperbola departure_onto_asymptote(const Vec3& position, const Vec3& v_infinity,
                                            double gm) {
    DepartureHyperbola out{};

    const double r = position.norm();
    const double v_inf = v_infinity.norm();
    if (!(r > 0.0) || !(gm > 0.0)) {
        out.message = "departure hyperbola: a position and a GM are required";
        return out;
    }
    if (!(v_inf > 0.0)) {
        // A zero excess velocity is a parabolic escape, whose "asymptote" is a
        // direction the trajectory approaches and never has a finite speed along.
        // Refused rather than approximated: a transfer that arrives at the edge
        // of the body's influence with no speed left never goes anywhere.
        out.message = "departure hyperbola: the excess velocity is zero (parabolic escape)";
        return out;
    }

    const Vec3 r_hat = position / r;
    const Vec3 s_hat = v_infinity / v_inf;

    // The orbital plane contains BOTH the departure point and the asymptote, so
    // its normal is their cross product -- and when they are parallel there is no
    // such plane. That is not a numerical edge case to be nudged past: it says the
    // ship is standing exactly along the direction it needs to leave in, where
    // every plane is equally good and the answer is a radial escape rather than a
    // transfer. The search simply skips that departure point.
    Vec3 h_hat = math::cross(r_hat, s_hat);
    const double sin_theta = h_hat.norm();
    if (!(sin_theta > 1.0e-9)) {
        out.message = "departure hyperbola: the departure point lies along the asymptote; "
                      "the orbital plane is undefined there";
        return out;
    }
    h_hat = h_hat / sin_theta;

    const double theta = math::angle_between(position, v_infinity);
    const double v_inf_squared = v_inf * v_inf;

    // Bracket, then bisect.  Bisection and not Newton: the derivative involves
    // dnu_inf/de, the function is only piecewise well behaved near e = 1, and the
    // whole call costs less than one force evaluation of the propagator it is
    // feeding. Robustness is worth more here than iteration count.
    double lo = 1.0 + 1.0e-9;
    double hi = 2.0;
    const double target = r;
    int expansions = 0;
    while (radius_at(hi, theta, gm, v_inf_squared) < target && expansions < 200) {
        hi *= 2.0;
        ++expansions;
    }
    if (radius_at(hi, theta, gm, v_inf_squared) < target) {
        out.message = "departure hyperbola: no eccentricity reaches this departure radius";
        return out;
    }

    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (radius_at(mid, theta, gm, v_inf_squared) < target) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const double e = 0.5 * (lo + hi);
    if (!(e > 1.0) || !std::isfinite(e)) {
        out.message = "departure hyperbola: the eccentricity solve did not converge";
        return out;
    }

    const double p = (gm / v_inf_squared) * (e * e - 1.0);
    const double nu_infinity = std::acos(-1.0 / e);
    const double nu = nu_infinity - theta;
    const double h = std::sqrt(gm * p);

    // The velocity, in the orbital frame: radial from the eccentricity, transverse
    // from the angular momentum. `t_hat` points the way the true anomaly grows,
    // which is from the departure point TOWARDS the asymptote -- that is what
    // makes this a departure and not an arrival.
    const Vec3 t_hat = math::cross(h_hat, r_hat);
    const double v_radial = (gm / h) * e * std::sin(nu);
    const double v_transverse = h / r;

    out.velocity = r_hat * v_radial + t_hat * v_transverse;
    out.speed = out.velocity.norm();
    out.eccentricity = e;
    out.semi_major_axis = -gm / v_inf_squared;
    out.periapsis_radius = (gm / v_inf_squared) * (e - 1.0);
    out.true_anomaly = nu;
    out.flight_path_angle = std::atan2(v_radial, v_transverse);
    out.ok = true;
    return out;
}

}  // namespace sf::trajectory
