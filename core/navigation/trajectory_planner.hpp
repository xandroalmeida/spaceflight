#pragma once

// Planning: two-body, impulsive, analytic.  It produces DATA -- delta-v values
// and maneuvers -- and never propagates anything.  Whether a plan works is
// answered by executing it and measuring, not by the planner's own confidence.
// See docs/architecture/navigation.md sections 6-8.

#include "core/navigation/maneuver.hpp"
#include "core/spacecraft/spacecraft.hpp"

#include <string>

namespace sf::navigation {

struct HohmannTransfer {
    double delta_v_departure{0.0};       // [m/s]
    double delta_v_arrival{0.0};         // [m/s]
    double total_delta_v{0.0};           // [m/s]
    double transfer_time{0.0};           // [s], half the transfer ellipse period
    double transfer_semi_major_axis{0.0};// [m]
};

// Two-impulse transfer between coplanar circular orbits of radius r1 and r2.
HohmannTransfer plan_hohmann(double gm, double r1, double r2);

// Delta-v of a tangential burn at radius r that moves the opposite apsis to
// `target_apsis`.  Positive raises, negative lowers.
double delta_v_to_change_apsis(double gm, double r, double current_speed, double target_apsis);

// Delta-v of a tangential burn at radius r that just reaches escape energy.
double delta_v_to_escape(double gm, double r, double current_speed);

// Where the finite burn sits relative to the instant the impulsive plan assumed.
enum class BurnCentering {
    CenterOnIgnition,  // burn spans [t - dt/2, t + dt/2]: halves the gravity loss
    StartAtIgnition    // burn spans [t, t + dt]
};

// Turns an impulsive delta-v into an executable maneuver via the rocket equation.
// Throws std::invalid_argument if the spacecraft does not carry enough
// propellant, saying by how much: a plan the ship cannot fly is not a plan
// (docs/architecture/navigation.md section 8).
//
// A negative `delta_v` flips PROGRADE to RETROGRADE (and RADIAL_OUT to RADIAL_IN,
// NORMAL to ANTI_NORMAL) and uses its magnitude.
Maneuver maneuver_for_delta_v(const spacecraft::Spacecraft& craft, double mass_at_ignition,
                              double delta_v, time::CoordinateTime ignition,
                              GuidanceMode guidance, celestial::BodyId reference,
                              double throttle = 1.0, std::string name = "burn",
                              BurnCentering centering = BurnCentering::CenterOnIgnition);

}  // namespace sf::navigation
