#pragma once

// The contract between the simulation and everything that displays it (rule 22).
//
// A snapshot is a VALUE, not a window into the core.  The consumer cannot hold a
// reference to state the propagator is about to overwrite, and cannot reach the
// integrator to ask it for one more step.  Copying is cheap -- tens of bodies --
// and that cheapness is what makes the rule enforceable.
//
// Everything the cockpit of Milestone 3 needs (rule 25) is computed here, once
// per frame, rather than by each instrument on its own.
// See docs/architecture/rendering.md section 4.

#include "core/attitude/attitude_state.hpp"
#include "core/celestial/body_catalog.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/gravity/force_model.hpp"
#include "core/propagation/propagation_state.hpp"
#include "core/trajectory/orbital_elements.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sf::simulation {

struct CelestialBodySnapshot {
    celestial::BodyId id{};
    std::string name;
    math::Vec3 position{};   // [m], integration frame
    math::Vec3 velocity{};   // [m/s]
    double gm{0.0};          // [m^3/s^2]
    double radius{0.0};      // [m], 0 for barycentres
};

struct SpacecraftSnapshot {
    std::string name{"spacecraft"};

    math::Vec3 position{};      // [m], integration frame
    math::Vec3 velocity{};      // [m/s]
    math::Vec3 acceleration{};  // [m/s^2], total from the force model
    double speed{0.0};

    double mass{0.0};           // [kg], current total
    double propellant{0.0};     // [kg]
    double delta_v_budget{0.0}; // [m/s]
    double thrust{0.0};         // [N], current
    double throttle{0.0};

    // Where the thrust is actually going, which is the question a pilot has when
    // the speed moves the wrong way.
    //
    // `thrust_along_track` is cos(angle between thrust and velocity): +1 is a
    // pure prograde burn, 0 does nothing to the orbital energy, -1 is braking.
    // `specific_energy_rate` is the rate at which the burn is adding orbital
    // energy, a . v [W/kg] -- positive raises the orbit, negative lowers it.
    // Speed alone answers neither question, because speed also trades against
    // altitude on every ellipse.
    double thrust_along_track{0.0};
    double specific_energy_rate{0.0};

    // Consumption, and how long it can go on. A high-Isp mode burns so slowly
    // that a mass readout looks frozen -- 298x slower than the high-thrust mode
    // means the third decimal of 19 tonnes moves once every 13 seconds. The rate
    // and the endurance say in one glance what the mass takes minutes to reveal.
    double mass_flow{0.0};       // [kg/s], positive = consuming
    double endurance{0.0};       // [s] until the tank is dry at this setting

    // Relative to the body the display is centred on.
    celestial::BodyId reference{};
    math::Vec3 relative_position{};
    math::Vec3 relative_velocity{};
    double distance_to_reference{0.0};
    double altitude{0.0};       // above the reference body's mean radius
    trajectory::OrbitalElements elements{};

    // Relative to the navigation target, when one is set (rule 25).
    std::optional<celestial::BodyId> target;
    double target_distance{0.0};
    double target_relative_speed{0.0};

    // Attitude (Milestone 3). The Euler angles are NOT here: they are a display
    // projection of the quaternion and are computed where they are displayed
    // (rule 19, ADR-0008).
    math::Quaternion orientation{math::Quaternion::identity()};
    math::Vec3 angular_velocity{};     // body frame [rad/s]
    double rotation_rate{0.0};         // |omega| [rad/s]
    math::Vec3 nose{1.0, 0.0, 0.0};    // body +x, in the integration frame
    double angle_to_prograde{0.0};     // [rad]
    double angle_to_nadir{0.0};        // [rad]

    // Relativity readouts.  Newtonian values today -- present from the start so
    // that Milestone 4 changes the physics, not the contract.
    double beta{0.0};
    double lorentz_factor{1.0};

    // gamma - 1, computed WITHOUT subtracting. At orbital speeds gamma is
    // 1.000000005, and taking the difference from 1 leaves ~5e-9 carrying the
    // absolute error of a number near 1 (2.2e-16), i.e. 4.4e-8 relative: seven
    // digits gone. The identity
    //
    //     gamma - 1 = beta^2 / (s (1 + s)),      s = sqrt(1 - beta^2)
    //
    // has no cancellation anywhere in 0 <= beta < 1, and reduces to beta^2/2 as
    // beta -> 0. This is the low-speed mirror of the trap that makes
    // docs/physics/relativity-roadmap.md section 3.1 store u = gamma*v instead of v.
    double lorentz_factor_minus_one{0.0};
    time::Duration proper_time{};
};

struct SimulationSnapshot {
    time::CoordinateTime time{};              // TDB
    time::Duration elapsed_coordinate{};      // since the scenario epoch
    time::Duration proper_time{};             // spacecraft clock
    time::Duration clock_difference{};        // coordinate - proper
    double time_warp{1.0};

    SpacecraftSnapshot spacecraft;
    std::vector<CelestialBodySnapshot> bodies;

    [[nodiscard]] const CelestialBodySnapshot* find(celestial::BodyId id) const;
    [[nodiscard]] std::string describe() const;
};

// Builds snapshots from the authoritative state.  Holds references to the
// provider, the catalogue and the force model, all of which must outlive it.
//
// It is the ONLY path from the core to a display: Godot never calls spkez_c and
// never sees a ForceModel.
class SnapshotBuilder {
public:
    SnapshotBuilder(const ephemeris::EphemerisProvider& provider,
                    const celestial::BodyCatalog& catalog,
                    const gravity::ForceModel& forces,
                    celestial::BodyId reference,
                    time::CoordinateTime epoch,
                    coordinates::ReferenceFrame frame = coordinates::ReferenceFrame::ssb_j2000());

    void set_reference(celestial::BodyId body) { reference_ = body; }
    void set_target(std::optional<celestial::BodyId> body) { target_ = body; }
    void set_time_warp(double warp) { time_warp_ = warp; }
    void set_spacecraft_name(std::string name) { name_ = std::move(name); }

    // Dry mass is needed to report propellant and budget; leave it unset for a
    // test particle with no engine.
    void set_propulsion(double dry_mass, double effective_exhaust_velocity);

    [[nodiscard]] SimulationSnapshot build(const propagation::PropagationState& state,
                                           time::CoordinateTime t) const;

private:
    const ephemeris::EphemerisProvider& provider_;
    const celestial::BodyCatalog& catalog_;
    const gravity::ForceModel& forces_;
    celestial::BodyId reference_;
    std::optional<celestial::BodyId> target_;
    time::CoordinateTime epoch_;
    coordinates::ReferenceFrame frame_;
    double time_warp_{1.0};
    double dry_mass_{0.0};
    double effective_exhaust_velocity_{0.0};
    std::string name_{"spacecraft"};
};

}  // namespace sf::simulation
