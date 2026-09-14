#pragma once

// Classical orbital elements derived from a state vector.
//
// These are DIAGNOSTICS, not state.  The simulation never propagates elements:
// an osculating ellipse around one body is a description of an instant, not a
// prediction, because every other body is pulling at the same time
// (docs/physics/gravity-model.md).  Apoapsis and periapsis on the cockpit are
// "where this orbit would go if everything else vanished right now".

#include "core/coordinates/state_vector.hpp"
#include "core/units/angle.hpp"

#include <string>

namespace sf::trajectory {

struct OrbitalElements {
    double semi_major_axis{0.0};      // a [m]; negative for hyperbolic orbits
    double eccentricity{0.0};         // e

    // The four angles carry their unit in the TYPE.
    //
    // The units audit of Milestone 6 found the B-plane angle crossing the core /
    // CLI / Godot boundary as radians on one side and degrees on the other, and
    // recorded these four as the same risk still open
    // (docs/physics/units-audit.md).  A `[rad]` comment is checked by nobody;
    // units::Angle is checked by the compiler and costs nothing at run time.
    units::Angle inclination{units::Angle::radians(0.0)};            // i
    units::Angle raan{units::Angle::radians(0.0)};                   // Omega
    units::Angle argument_of_periapsis{units::Angle::radians(0.0)};  // omega
    units::Angle true_anomaly{units::Angle::radians(0.0)};           // nu

    double periapsis_radius{0.0};     // [m] from the centre of the primary
    double apoapsis_radius{0.0};      // [m]; infinity for e >= 1
    double period{0.0};               // [s]; 0 for unbound orbits
    double specific_energy{0.0};      // [J/kg]
    double specific_angular_momentum{0.0};  // [m^2/s]
    // A RATE, not an angle, and left as a double for now: an AngularRate type
    // would be the honest counterpart and nothing yet has two of them to
    // confuse.
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
