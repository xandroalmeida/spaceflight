// The bridge, and nothing but the bridge.  See transfer_bridge.hpp for what
// used to be here and why it is not.

#include "app/session/transfer_bridge.hpp"

#include "core/coordinates/reference_frame.hpp"

#include <stdexcept>

namespace sf::app {
namespace {

void require_complete(const SceneTransferRequest& request) {
    if (request.provider == nullptr || request.craft == nullptr ||
        request.catalog == nullptr) {
        throw std::invalid_argument(
            "plan_transfer: the bridge needs a provider, a catalogue and a ship");
    }
}

}  // namespace

sf::navigation::SimulationState state_for(const SceneTransferRequest& request) {
    require_complete(request);

    // The one piece of geometry this file is allowed to do: the simulation
    // carries the ship's state in the barycentric integration frame, and a
    // parking orbit is only a description of anything relative to the body it is
    // about.
    const auto frame = sf::coordinates::ReferenceFrame::ssb_j2000();
    const auto center = request.provider->state(request.center, request.epoch, frame);

    sf::navigation::SimulationState state{};
    state.provider = request.provider;
    state.orientation = request.orientation;
    state.catalog = *request.catalog;
    state.j2_bodies = request.j2_bodies;
    state.vehicle.position = request.initial.state.position - center.state.position;
    state.vehicle.velocity = request.initial.state.velocity - center.state.velocity;
    state.epoch = request.epoch;
    // The tank as it is, not as it left the factory: anything the pilot burnt
    // before asking -- a test of the engine, an RCS slew -- is propellant the
    // plan must not count on (core/navigation/transfer_planner.hpp).
    state.mass = request.initial.mass;
    state.integrator = request.integrator;
    return state;
}

sf::navigation::MissionRequest request_for(const SceneTransferRequest& request) {
    require_complete(request);

    sf::navigation::MissionRequest plan{};
    plan.origin = request.center;
    plan.destination = request.target;
    plan.target_orbit.periapsis_altitude = request.target_periapsis_altitude_m;
    plan.target_orbit.apoapsis_altitude = request.target_apoapsis_altitude_m;
    // The band the capture has to land in, derived from what was asked for
    // rather than fixed at the Moon's numbers: a pilot who asks for a 300 km
    // orbit should not be told the mission failed because 300 is not 100.
    // +-20 km and e <= 0.01 are the campaign's tolerances, carried across.
    plan.target_orbit.minimum_periapsis_altitude =
        request.target_periapsis_altitude_m - 20.0e3;
    plan.target_orbit.maximum_apoapsis_altitude = request.target_apoapsis_altitude_m + 20.0e3;

    plan.departure_window.span = sf::time::Duration::seconds(request.search_window_s);
    plan.departure_window.samples = request.departure_samples;

    // The time-of-flight grid, the objective, the cost weights and every search
    // tolerance are left at the request type's defaults -- which ARE the values
    // the 365/365 campaign was qualified with.  The cockpit does not get to
    // override them and must not grow a dial for any of them; see the header.
    plan.kind = request.kind;
    plan.spacecraft.vehicle = request.craft;
    plan.spacecraft.execution = request.execution;
    plan.spacecraft.pointing = request.pointing;

    plan.want_trajectory = request.want_trajectory;
    plan.trajectory_samples = request.trajectory_samples;

    // The search space the two bodies actually need. Not a constant and not a
    // cockpit dial: derived from their own orbits, so the same call gives a
    // lunar grid for the Moon and a two-hundred-day sweep for Mars
    // (core/navigation/mission_planner.hpp, rules 37-39).
    const auto space = sf::navigation::default_search_space(*request.provider, request.center,
                                                            request.target, request.epoch);
    plan.time_of_flight = space.time_of_flight;
    // The window's LENGTH is the scene's, because it is the one search parameter
    // the pilot legitimately owns -- how far ahead to look for a departure. Its
    // sampling is the search space's, because how finely a parking orbit has to
    // be walked is a property of the geometry and not of the pilot's patience.
    plan.departure_window.samples = space.departure_window.samples;

    plan.effort.cancelled = request.cancelled;
    plan.effort.on_progress = request.on_progress;
    plan.pinned = request.pinned;
    return plan;
}

sf::navigation::MissionPlanResult plan_transfer(const SceneTransferRequest& request) {
    return sf::navigation::plan_mission(state_for(request), request_for(request));
}

}  // namespace sf::app
