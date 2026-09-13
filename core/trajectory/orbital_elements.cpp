#include "core/trajectory/orbital_elements.hpp"

#include "core/math/vec3.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace sf::trajectory {
namespace {

using math::Vec3;

// Below these thresholds the corresponding angle is undefined (a circular orbit
// has no periapsis; an equatorial orbit has no ascending node).  We report the
// degeneracy instead of returning a meaningless number produced by 0/0.
constexpr double kEccentricityTolerance = 1.0e-11;
constexpr double kInclinationTolerance = 1.0e-11;  // [rad]

double safe_acos(double x) { return std::acos(std::clamp(x, -1.0, 1.0)); }

}  // namespace

OrbitalElements elements_from_state(const coordinates::StateVector& sv, double gm) {
    if (gm <= 0.0) {
        throw std::invalid_argument("elements_from_state: gm must be > 0");
    }

    const Vec3& r = sv.position;
    const Vec3& v = sv.velocity;
    const double r_mag = r.norm();
    if (r_mag <= 0.0) {
        throw std::invalid_argument("elements_from_state: zero radius");
    }
    const double v2 = v.norm_squared();

    OrbitalElements el{};

    const Vec3 h = cross(r, v);
    const double h_mag = h.norm();
    el.specific_angular_momentum = h_mag;

    el.specific_energy = 0.5 * v2 - gm / r_mag;

    // Eccentricity vector (points at periapsis).
    const Vec3 e_vec = (r * (v2 - gm / r_mag) - v * dot(r, v)) / gm;
    el.eccentricity = e_vec.norm();
    el.circular = el.eccentricity < kEccentricityTolerance;

    el.inclination = h_mag > 0.0 ? safe_acos(h.z / h_mag) : 0.0;
    el.equatorial = el.inclination < kInclinationTolerance ||
                    std::abs(el.inclination - units::pi) < kInclinationTolerance;

    // Node vector: z_hat x h.
    const Vec3 n{-h.y, h.x, 0.0};
    const double n_mag = n.norm();

    if (!el.equatorial && n_mag > 0.0) {
        el.raan = safe_acos(n.x / n_mag);
        if (n.y < 0.0) {
            el.raan = units::two_pi - el.raan;
        }
    }

    if (!el.circular && !el.equatorial && n_mag > 0.0) {
        el.argument_of_periapsis = safe_acos(dot(n, e_vec) / (n_mag * el.eccentricity));
        if (e_vec.z < 0.0) {
            el.argument_of_periapsis = units::two_pi - el.argument_of_periapsis;
        }
    } else if (!el.circular && el.equatorial) {
        // Longitude of periapsis, measured from the x axis.
        el.argument_of_periapsis = safe_acos(e_vec.x / el.eccentricity);
        if (e_vec.y < 0.0) {
            el.argument_of_periapsis = units::two_pi - el.argument_of_periapsis;
        }
    }

    if (!el.circular) {
        el.true_anomaly = safe_acos(dot(e_vec, r) / (el.eccentricity * r_mag));
        if (dot(r, v) < 0.0) {
            el.true_anomaly = units::two_pi - el.true_anomaly;
        }
    } else {
        // Argument of latitude for a circular orbit.
        if (n_mag > 0.0) {
            el.true_anomaly = safe_acos(dot(n, r) / (n_mag * r_mag));
            if (r.z < 0.0) {
                el.true_anomaly = units::two_pi - el.true_anomaly;
            }
        } else {
            el.true_anomaly = std::atan2(r.y, r.x);
        }
    }

    // Semi-major axis from energy: a = -gm/(2*eps).  Parabolic orbits (eps == 0)
    // have no finite a; they are reported as unbound with a == infinity.
    if (std::abs(el.specific_energy) > 0.0) {
        el.semi_major_axis = -gm / (2.0 * el.specific_energy);
    } else {
        el.semi_major_axis = std::numeric_limits<double>::infinity();
    }

    el.bound = el.specific_energy < 0.0;

    // p = h^2/gm is valid for every conic, including the parabolic case.
    const double p = h_mag * h_mag / gm;
    el.periapsis_radius = p / (1.0 + el.eccentricity);
    el.apoapsis_radius = el.eccentricity < 1.0 ? p / (1.0 - el.eccentricity)
                                               : std::numeric_limits<double>::infinity();

    if (el.bound && std::isfinite(el.semi_major_axis) && el.semi_major_axis > 0.0) {
        el.mean_motion = std::sqrt(gm / (el.semi_major_axis * el.semi_major_axis * el.semi_major_axis));
        el.period = units::two_pi / el.mean_motion;
    }

    return el;
}

double circular_speed(double gm, double radius) {
    if (gm <= 0.0 || radius <= 0.0) {
        throw std::invalid_argument("circular_speed: gm and radius must be > 0");
    }
    return std::sqrt(gm / radius);
}

double circular_period(double gm, double radius) {
    if (gm <= 0.0 || radius <= 0.0) {
        throw std::invalid_argument("circular_period: gm and radius must be > 0");
    }
    return units::two_pi * std::sqrt(radius * radius * radius / gm);
}

std::string OrbitalElements::to_string() const {
    std::ostringstream os;
    os << std::setprecision(12);
    os << "a     = " << semi_major_axis << " m\n"
       << "e     = " << eccentricity << "\n"
       << "i     = " << units::rad_to_deg(inclination) << " deg\n"
       << "RAAN  = " << units::rad_to_deg(raan) << " deg\n"
       << "argp  = " << units::rad_to_deg(argument_of_periapsis) << " deg\n"
       << "nu    = " << units::rad_to_deg(true_anomaly) << " deg\n"
       << "rp    = " << periapsis_radius << " m\n"
       << "ra    = " << apoapsis_radius << " m\n"
       << "T     = " << period << " s\n"
       << "energy= " << specific_energy << " J/kg\n"
       << "h     = " << specific_angular_momentum << " m^2/s";
    return os.str();
}

}  // namespace sf::trajectory
