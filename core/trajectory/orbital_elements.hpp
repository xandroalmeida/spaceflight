#pragma once

// Classical orbital elements derived from a state vector.
//
// These are DIAGNOSTICS, not state.  The simulation never propagates elements:
// an osculating ellipse around one body is a description of an instant, not a
// prediction, because every other body is pulling at the same time
// (docs/physics/gravity-model.md).  Apoapsis and periapsis on the cockpit are
// "where this orbit would go if everything else vanished right now".

#include "core/coordinates/state_vector.hpp"

#include <string>

namespace sf::trajectory {

struct OrbitalElements {
    double semi_major_axis{0.0};      // a [m]; negative for hyperbolic orbits
    double eccentricity{0.0};         // e
    double inclination{0.0};          // i [rad]
    double raan{0.0};                 // right ascension of ascending node [rad]
    double argument_of_periapsis{0.0};// omega [rad]
    double true_anomaly{0.0};         // nu [rad]

    double periapsis_radius{0.0};     // [m] from the centre of the primary
    double apoapsis_radius{0.0};      // [m]; infinity for e >= 1
    double period{0.0};               // [s]; 0 for unbound orbits
    double specific_energy{0.0};      // [J/kg]
    double specific_angular_momentum{0.0};  // [m^2/s]
    double mean_motion{0.0};          // [rad/s]

    bool bound{false};
    bool equatorial{false};           // i below the degeneracy threshold
    bool circular{false};             // e below the degeneracy threshold

    [[nodiscard]] std::string to_string() const;
};

// `state` must be relative to the central body (position and velocity), and `gm`
// its gravitational parameter [m^3/s^2].
OrbitalElements elements_from_state(const coordinates::StateVector& state, double gm);

// Circular orbit speed and period, used by tests and scenario setup.
double circular_speed(double gm, double radius);
double circular_period(double gm, double radius);

}  // namespace sf::trajectory
