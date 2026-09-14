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

    // The four angles are computed in radians, because that is what the
    // trigonometry below speaks, and bound to units::Angle once at the end.
    // Converting at every assignment would put a constructor call inside every
    // quadrant test and say nothing extra: the unit is a property of the FIELD,
    // and the field is where it is declared.
    double inclination = h_mag > 0.0 ? safe_acos(h.z / h_mag) : 0.0;
    double raan = 0.0;
    double argument_of_periapsis = 0.0;
    double true_anomaly = 0.0;

    el.equatorial = inclination < kInclinationTolerance ||
                    std::abs(inclination - units::pi) < kInclinationTolerance;

    // Node vector: z_hat x h.
    const Vec3 n{-h.y, h.x, 0.0};
    const double n_mag = n.norm();

    if (!el.equatorial && n_mag > 0.0) {
        raan = safe_acos(n.x / n_mag);
        if (n.y < 0.0) {
            raan = units::two_pi - raan;
        }
    }

    if (!el.circular && !el.equatorial && n_mag > 0.0) {
        argument_of_periapsis = safe_acos(dot(n, e_vec) / (n_mag * el.eccentricity));
        if (e_vec.z < 0.0) {
            argument_of_periapsis = units::two_pi - argument_of_periapsis;
        }
    } else if (!el.circular && el.equatorial) {
        // Longitude of periapsis, measured from the x axis.
        argument_of_periapsis = safe_acos(e_vec.x / el.eccentricity);
        if (e_vec.y < 0.0) {
            argument_of_periapsis = units::two_pi - argument_of_periapsis;
        }
    }

    if (!el.circular) {
        true_anomaly = safe_acos(dot(e_vec, r) / (el.eccentricity * r_mag));
        if (dot(r, v) < 0.0) {
            true_anomaly = units::two_pi - true_anomaly;
        }
    } else {
        // Argument of latitude for a circular orbit.
        if (n_mag > 0.0) {
            true_anomaly = safe_acos(dot(n, r) / (n_mag * r_mag));
            if (r.z < 0.0) {
                true_anomaly = units::two_pi - true_anomaly;
            }
        } else {
            true_anomaly = std::atan2(r.y, r.x);
        }
    }

    el.inclination = units::Angle::radians(inclination);
    el.raan = units::Angle::radians(raan);
    el.argument_of_periapsis = units::Angle::radians(argument_of_periapsis);
    el.true_anomaly = units::Angle::radians(true_anomaly);

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

coordinates::StateVector state_from_elements(const OrbitalElements& el, double gm) {
    if (gm <= 0.0) {
        throw std::invalid_argument("state_from_elements: gm must be > 0");
    }
    const double e = el.eccentricity;
    if (!(e >= 0.0) || !std::isfinite(e)) {
        throw std::invalid_argument("state_from_elements: eccentricity must be finite and >= 0");
    }

    // The semi-latus rectum is the element the conic equation is actually
    // written in, and it is finite for every conic including the parabola --
    // which is why it, and not the semi-major axis, is what gets reconstructed.
    const double a = el.semi_major_axis;
    double p = 0.0;
    if (std::abs(e - 1.0) < kEccentricityTolerance) {
        // Parabolic: a is infinite and p has to come from somewhere else.  The
        // periapsis radius is the only element that still means anything.
        p = 2.0 * el.periapsis_radius;
    } else {
        if (!std::isfinite(a) || a == 0.0) {
            throw std::invalid_argument(
                "state_from_elements: a non-parabolic orbit needs a finite, non-zero "
                "semi-major axis");
        }
        p = a * (1.0 - e * e);
    }
    if (!(p > 0.0)) {
        throw std::invalid_argument(
            "state_from_elements: the elements describe no conic (semi-latus rectum <= 0); "
            "a < 0 with e < 1, or a > 0 with e > 1, is not an orbit");
    }

    const double nu = el.true_anomaly.radians();
    const double denominator = 1.0 + e * std::cos(nu);
    if (!(denominator > 0.0)) {
        throw std::invalid_argument(
            "state_from_elements: this true anomaly is past the asymptote of this hyperbola");
    }
    const double r = p / denominator;

    // Perifocal frame: x towards periapsis, y along the motion at periapsis.
    const double mu_over_h = std::sqrt(gm / p);
    const Vec3 r_pqw{r * std::cos(nu), r * std::sin(nu), 0.0};
    const Vec3 v_pqw{-mu_over_h * std::sin(nu), mu_over_h * (e + std::cos(nu)), 0.0};

    // 3-1-3 rotation, RAAN then inclination then argument of periapsis, applied
    // in the order that takes perifocal to inertial.
    const double cos_raan = std::cos(el.raan.radians());
    const double sin_raan = std::sin(el.raan.radians());
    const double cos_i = std::cos(el.inclination.radians());
    const double sin_i = std::sin(el.inclination.radians());
    const double cos_argp = std::cos(el.argument_of_periapsis.radians());
    const double sin_argp = std::sin(el.argument_of_periapsis.radians());

    const auto rotate = [&](const Vec3& pqw) {
        return Vec3{
            pqw.x * (cos_raan * cos_argp - sin_raan * sin_argp * cos_i) -
                pqw.y * (cos_raan * sin_argp + sin_raan * cos_argp * cos_i),
            pqw.x * (sin_raan * cos_argp + cos_raan * sin_argp * cos_i) -
                pqw.y * (sin_raan * sin_argp - cos_raan * cos_argp * cos_i),
            pqw.x * (sin_argp * sin_i) + pqw.y * (cos_argp * sin_i)};
    };

    coordinates::StateVector state{};
    state.position = rotate(r_pqw);
    state.velocity = rotate(v_pqw);
    return state;
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
       << "i     = " << inclination.degrees() << " deg\n"
       << "RAAN  = " << raan.degrees() << " deg\n"
       << "argp  = " << argument_of_periapsis.degrees() << " deg\n"
       << "nu    = " << true_anomaly.degrees() << " deg\n"
       << "rp    = " << periapsis_radius << " m\n"
       << "ra    = " << apoapsis_radius << " m\n"
       << "T     = " << period << " s\n"
       << "energy= " << specific_energy << " J/kg\n"
       << "h     = " << specific_angular_momentum << " m^2/s";
    return os.str();
}

}  // namespace sf::trajectory
