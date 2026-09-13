#pragma once

// Planning a transfer to another body, for the scene.
//
// No Godot in this header on purpose: it is plain core types, so the planner can
// be read, reasoned about and (if it ever earns a test) exercised without an
// engine. simulation_node.cpp is the only thing that turns the result into a
// Dictionary.
//
// See docs/physics/b-plane.md.

#include "core/celestial/body_id.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/navigation/maneuver.hpp"
#include "core/navigation/maneuver_executor.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/time/coordinate_time.hpp"
#include "core/units/angle.hpp"

#include <string>

namespace spaceflight_godot {

struct TransferRequest {
    const sf::ephemeris::EphemerisProvider* provider{nullptr};
    sf::propagation::DormandPrince54Propagator* propagator{nullptr};
    const sf::spacecraft::Spacecraft* craft{nullptr};

    // The plan the propagator's force model already points at, and the executor
    // that reads it. The planner writes trial burns into `plan` and flies them,
    // so that what it corrects is the FINITE burn the scene will actually fly --
    // not an impulse. The difference is not small: a trans-lunar injection loses
    // about 600 m/s to gravity while the engine is running, and a plan that
    // ignores that arrives somewhere else entirely.
    sf::navigation::ManeuverPlan* plan{nullptr};
    sf::navigation::ManeuverExecutor* executor{nullptr};

    sf::propagation::PropagationState initial{};
    sf::time::CoordinateTime epoch{};

    sf::celestial::BodyId center{sf::celestial::bodies::earth};
    sf::celestial::BodyId target{sf::celestial::bodies::moon};

    double time_of_flight_s{4.5 * 86400.0};
    double flyby_altitude_m{100.0e3};
    sf::units::Angle b_plane_angle{sf::units::Angle::radians(0.0)};

    // How far ahead to look for a departure, and how finely. Most points in a
    // parking orbit are a bad place to leave from.
    double search_window_s{6.0 * 3600.0};
    int departure_samples{48};

    double position_tolerance_m{1.0e4};
    double periapsis_tolerance_m{500.0};

    // How many of the cheapest Lambert candidates to actually FLY before choosing.
    int trial_count{6};

    bool insert{true};
    double insert_apoapsis_m{0.0};   // 0 = circularise
};

struct TransferPlan {
    bool valid{false};
    std::string message;

    sf::time::CoordinateTime departure{};
    sf::time::CoordinateTime arrival{};
    sf::time::CoordinateTime insertion{};
    double time_of_flight_s{0.0};

    double lambert_delta_v{0.0};      // [m/s] the two-body plan
    double injection_delta_v{0.0};    // [m/s] after correction
    double insertion_delta_v{0.0};    // [m/s]

    double reach_miss_initial{0.0};   // [m] stage 1
    double reach_miss_final{0.0};     // [m]
    double b_plane_miss{0.0};         // [m] stage 2
    int passes{0};
    std::string reach_message;
    std::string shape_message;
    int reach_iterations{0};
    int reach_evaluations{0};
    double transfer_angle{0.0};   // [rad]
    double candidate_miss{0.0};   // [m] the flown miss of the chosen departure
    int candidates_tried{0};
    int attempts{0};

    double v_infinity{0.0};           // [m/s] at the target
    double periapsis_radius{0.0};     // [m]
    double flyby_altitude_m{0.0};
    double orbit_period_s{0.0};

    sf::navigation::ManeuverPlan maneuvers;
};

[[nodiscard]] TransferPlan plan_transfer(const TransferRequest& request);

}  // namespace spaceflight_godot
