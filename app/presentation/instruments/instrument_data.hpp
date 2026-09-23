#pragma once

// One record per frame, shared by every instrument.
//
// One per instrument would be tidier and wrong: two displays that built their
// own data could build it at different instants, and the cockpit would show two
// readings of the same ship. One record, one instant, six drawings.

#include "app/presentation/geometry.hpp"
#include "app/presentation/palette.hpp"
#include "app/session/flight_session.hpp"

#include <string>
#include <vector>

namespace sf::app {

struct InstrumentData {
    // False on the first frame, before the first snapshot arrives -- the state
    // every instrument has to survive.
    bool present{false};

    SnapshotView s{};
    FlightDirections directions{};
    Basis ship_basis{};

    // Scene units, through the floating origin.
    Vec3 ship_position{};
    Vec3 reference_position{};
    double reference_radius{0.0};
    Colour reference_colour{palette::NAV_DIM};
    std::vector<Vec3> orbit_track;
    std::vector<Vec3> target_track;
    std::vector<Vec3> planned_trajectory;
    std::vector<ManeuverView> maneuvers;
    double render_scale{1.0e-6};

    bool has_target{false};
    Vec3 target_position{};
    double target_radius{0.0};

    // Altitudes and not radii: a pilot reads altitude.
    double apoapsis_altitude_m{0.0};
    double periapsis_altitude_m{0.0};
    bool bound{true};
    bool retrograde_orbit{false};

    double propellant_capacity_kg{19000.0};
    bool engine_armed{true};
    bool rcs_enabled{true};
    std::string rcs_activity{"IDLE"};
    std::vector<double> rcs_throttles;
    int rcs_firing{0};
    int rcs_thrusters{12};
    std::string camera_mode;
    bool cockpit_view{false};
    bool paused{false};

    // PROPER acceleration, what an accelerometer on board would read: only the
    // NON-gravitational forces over the mass. Zero in free fall.
    double proper_acceleration_ms2{0.0};
    // The rate at which the distance to the target falls: the relative velocity
    // projected on the line of sight.
    double closing_speed_ms{0.0};

    std::string mission_phase;
    std::string next_event;
    double next_event_seconds{0.0};
    double plan_seconds_to_arrival{-1.0};
};

}  // namespace sf::app
