#pragma once

// The direct transfer: continuous, closed-loop guided thrust from the ship's
// current state to an orbit about the destination, for an engine strong enough
// to push the whole way (docs/physics/direct-transfer-guidance.md).
//
// It answers the same question plan_mission() answers -- and returns the same
// MissionPlanResult -- so that everything downstream of a plan (arming it, the
// mission phases, the cockpit's panel, the map) works without knowing which
// kind of transfer it is. plan_mission() delegates here when the request's
// `kind` is Direct.
//
// Like the Lambert planner it FLIES what it offers before offering it: the
// whole force model, the executor with the rendezvous law, special-relativistic
// kinematics, to one orbit past the arrival. Every number in the result is from
// that flight.

#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/navigation/mission_planner.hpp"

namespace sf::navigation {

// Where and how the ship arrives, relative to the destination (section 6).
struct DirectArrival {
    math::Vec3 offset{};     // [m]   position relative to the destination
    math::Vec3 velocity{};   // [m/s] velocity relative to the destination
    double radius{0.0};      // [m]   from the destination's centre
    // True when the requested orbit lies outside half the destination's Hill
    // radius: then there is no orbit to arrive in, and the arrival is a point
    // at that distance with zero relative velocity.
    bool station{false};
    double hill_radius{0.0};
};

[[nodiscard]] DirectArrival direct_arrival_geometry(const ephemeris::EphemerisProvider& provider,
                                                    celestial::BodyId destination,
                                                    time::CoordinateTime arrival,
                                                    const math::Vec3& ship_position,
                                                    double altitude);

[[nodiscard]] MissionPlanResult plan_direct_mission(const SimulationState& state,
                                                    const MissionRequest& request);

}  // namespace sf::navigation
