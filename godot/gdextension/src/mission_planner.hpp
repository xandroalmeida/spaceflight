#pragma once

// The bridge between the scene and core/navigation/mission_planner.hpp.
//
// ---------------------------------------------------------------------------
// What this file used to be, and why it is not that any more
//
// Until Milestone 6.2 this header declared a `TransferRequest` and a
// `TransferPlan` of its own, and the .cpp beside it contained five hundred lines
// of astrodynamics: a departure scan over a coasted parking orbit, a
// time-of-flight sweep, a Lambert screen with its own degenerate-angle band, a
// two-stage differential corrector, a B-plane aim point, an insertion burn, and
// a cost rule that chose between candidates by flying them.
//
// Every one of those had a counterpart in core/navigation/lunar_transfer.hpp,
// and the two were not the same code.  The counterpart is what the 365-epoch
// campaign measured; this was what the game flew.  A validation report about
// software the player never runs is not a validation report.
//
// So the astrodynamics is gone -- deleted, not moved -- and what is left is a
// translation:
//
//     scene objects  ->  navigation::SimulationState + LunarTransferRequest
//     plan_lunar_transfer(...)
//     navigation::MissionPlanResult  ->  the caller
//
// There is no arithmetic here that decides anything about a trajectory.  If a
// future change needs one, it belongs in the core, and
// tests/scientific/test_planner_equivalence.cpp exists to notice if it does not.
//
// ---------------------------------------------------------------------------
// What the scene is allowed to choose (Milestone 6.2 section 3)
//
// Godot supplies parameters, picks a destination, starts the planning, shows the
// alternatives, presents the result and asks for execution.  It does not choose
// a departure epoch, a time of flight, a Lambert branch, an aim point or a burn.
//
// The time of flight in particular is NOT a parameter here, and its absence is
// the whole lesson of Milestone 6: with the departure point fixed at wherever
// the orbit happened to be and the flight time fixed at 4.5 days, the transfer
// angle is whatever the calendar says, and 82.5 % of the geometries that
// produces are unflyable from a 400 km parking orbit.  The search decides the
// flight time.  Letting the cockpit pin it would reintroduce the defect through
// the user interface.
//
// No Godot types in this header on purpose: it is plain core types, so the
// bridge can be read, reasoned about and exercised without an engine.
// simulation_node.cpp is the only thing that turns the result into a Dictionary.

#include "core/attitude/pointing_controller.hpp"
#include "core/celestial/body_catalog.hpp"
#include "core/celestial/body_id.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/navigation/mission_planner.hpp"
#include "core/propagation/propagation_state.hpp"
#include "core/propagation/spacecraft_propagator.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/time/coordinate_time.hpp"

#include <vector>

namespace spaceflight_godot {

// Everything the scene owns that the planner needs.  Pointers, because the
// simulation node owns the lifetimes and this struct lives for one call.
struct SceneTransferRequest {
    const sf::ephemeris::EphemerisProvider* provider{nullptr};
    const sf::celestial::BodyOrientationProvider* orientation{nullptr};
    const sf::celestial::BodyCatalog* catalog{nullptr};
    const sf::spacecraft::Spacecraft* craft{nullptr};
    std::vector<sf::celestial::BodyId> j2_bodies{};
    sf::propagation::IntegratorConfig integrator{};

    // The ship as the simulation holds it: absolute, in the integration frame.
    // Converting it to a parking orbit relative to `center` is this bridge's
    // job, and one of the very few things it computes.
    sf::propagation::PropagationState initial{};
    sf::time::CoordinateTime epoch{};

    sf::celestial::BodyId center{sf::celestial::bodies::earth};
    sf::celestial::BodyId target{sf::celestial::bodies::moon};

    // The orbit the pilot is asking to end up in.
    double target_periapsis_altitude_m{100.0e3};
    double target_apoapsis_altitude_m{100.0e3};

    // How far ahead to look for a departure.  Most points in a parking orbit are
    // a bad place to leave from, and which ones are good is decided by where the
    // target WILL BE rather than by anything about the orbit.
    double search_window_s{2.0 * 3600.0};
    int departure_samples{16};

    // How the burns are delivered.  Autopilot is the scene's answer -- the ship
    // has an attitude controller and the pointing lag is real -- and it is the
    // model docs/validation/autopilot-hardening.md qualified.
    sf::navigation::ExecutionModel execution{sf::navigation::ExecutionModel::Autopilot};
    sf::attitude::PointingGains pointing{};

    // The cockpit draws the planned arc, so it asks for one.
    bool want_trajectory{true};
    int trajectory_samples{256};
};

// The translation, in two halves, EXPOSED.
//
// Not so that callers can go round the front door -- `plan_transfer` below is
// the front door and it is one line -- but so that the translation itself can be
// checked field by field.  A test that can only compare two flown trajectories
// can tell you that they agree today; a test that compares the two REQUESTS can
// tell you that the bridge is still asking the same question, which is the
// invariant that actually has to hold.
//
// Splitting them also says what the bridge computes: `state_for` subtracts the
// origin body's state, and that is the only piece of geometry in this file.
[[nodiscard]] sf::navigation::SimulationState state_for(const SceneTransferRequest& request);
[[nodiscard]] sf::navigation::LunarTransferRequest request_for(const SceneTransferRequest& request);

// Translates, calls the core, returns what the core returned.
//
// Deliberately returns the CORE's result type rather than a local one: a
// bridge-shaped copy of MissionPlanResult would be one more place for the two
// sides to drift apart, which is the thing this milestone deleted.
[[nodiscard]] sf::navigation::MissionPlanResult plan_transfer(const SceneTransferRequest& request);

}  // namespace spaceflight_godot
