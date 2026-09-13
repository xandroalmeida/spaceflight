#pragma once

// Closed-form two-body propagation, for use as the reference solution in
// integrator tests.  Deliberately NOT part of the core: the simulation never
// propagates Kepler orbits, it integrates forces (docs/physics/gravity-model.md).

#include "core/coordinates/state_vector.hpp"
#include "core/math/vec3.hpp"
#include "core/units/constants.hpp"

#include <cmath>
#include <stdexcept>

namespace sft {

// Solves M = E - e sin E by Newton's method to machine precision.
inline double solve_kepler_elliptic(double mean_anomaly, double eccentricity) {
    double e_anomaly = eccentricity < 0.8 ? mean_anomaly : sf::units::pi;
    for (int i = 0; i < 100; ++i) {
        const double f = e_anomaly - eccentricity * std::sin(e_anomaly) - mean_anomaly;
        const double fp = 1.0 - eccentricity * std::cos(e_anomaly);
        const double delta = f / fp;
        e_anomaly -= delta;
        if (std::abs(delta) < 1.0e-15) {
            return e_anomaly;
        }
    }
    throw std::runtime_error("solve_kepler_elliptic: no convergence");
}

// Exact elliptic two-body propagation of a state vector about a fixed point mass
// at the origin, using Lagrange f and g functions.
inline sf::coordinates::StateVector kepler_propagate(const sf::coordinates::StateVector& initial,
                                                     double gm, double dt) {
    using sf::math::Vec3;

    const Vec3& r0 = initial.position;
    const Vec3& v0 = initial.velocity;
    const double r0_mag = r0.norm();
    const double v0_sq = v0.norm_squared();

    const double energy = 0.5 * v0_sq - gm / r0_mag;
    if (energy >= 0.0) {
        throw std::runtime_error("kepler_propagate: only elliptic orbits are supported");
    }
    const double a = -gm / (2.0 * energy);
    const double n = std::sqrt(gm / (a * a * a));

    const double sigma0 = dot(r0, v0) / std::sqrt(gm);
    const double ecos_e0 = 1.0 - r0_mag / a;
    const double esin_e0 = sigma0 / std::sqrt(a);
    const double e0 = std::atan2(esin_e0, ecos_e0);

    const double mean_anomaly = e0 - esin_e0 + n * dt;
    const double eccentricity = std::hypot(ecos_e0, esin_e0);
    const double e_anomaly = solve_kepler_elliptic(mean_anomaly, eccentricity);

    const double de = e_anomaly - e0;
    const double cos_de = std::cos(de);
    const double sin_de = std::sin(de);

    const double r_mag = a * (1.0 - ecos_e0 * cos_de + esin_e0 * sin_de);

    const double f = 1.0 - (a / r0_mag) * (1.0 - cos_de);
    const double g = dt + std::sqrt(a * a * a / gm) * (sin_de - de);
    const double fdot = -std::sqrt(gm * a) * sin_de / (r_mag * r0_mag);
    const double gdot = 1.0 - (a / r_mag) * (1.0 - cos_de);

    sf::coordinates::StateVector out{};
    out.position = r0 * f + v0 * g;
    out.velocity = r0 * fdot + v0 * gdot;
    return out;
}

}  // namespace sft
