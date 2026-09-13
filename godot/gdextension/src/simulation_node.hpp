#pragma once

// SpaceflightSimulation: the whole surface Godot gets.
//
// It owns the scientific core, advances it, and hands out SNAPSHOTS -- never
// pointers into the integrator, never an ephemeris call, never a force model
// (rule 22, ADR-0002).  Godot's job starts where this class ends.
//
// Everything crossing into Godot is either a plain scalar, a String, or a
// Vector3 that has already been through RenderTransform: float, camera-relative,
// scene units.  See docs/architecture/rendering.md.

#include "core/attitude/inertia.hpp"
#include "core/attitude/pointing_controller.hpp"
#include "core/attitude/rcs.hpp"
#include "core/celestial/body_catalog.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "core/gravity/composite_force_model.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/propulsion/main_engine_force.hpp"
#include "core/render/render_transform.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/simulation/simulation_clock.hpp"
#include "core/simulation/snapshot.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <memory>
#include <optional>

namespace spaceflight_godot {

class SpaceflightSimulation : public godot::Node {
    GDCLASS(SpaceflightSimulation, godot::Node)

public:
    SpaceflightSimulation();
    ~SpaceflightSimulation() override;

    // --- setup -------------------------------------------------------------
    // Loads SPICE kernels and places the ship in a circular orbit.  Returns
    // false and pushes an error to Godot's log on failure; it never throws
    // across the C ABI.
    bool configure(const godot::String& kernel_directory, const godot::String& epoch_utc);
    bool start_circular_orbit(double altitude_m, double inclination_deg);

    // --- time --------------------------------------------------------------
    void set_time_warp(double warp);
    double get_time_warp() const;

    // Advances by `wall_seconds` of real time, scaled by the warp.  The
    // integrator picks its own steps; the frame rate never reaches it.
    void advance(double wall_seconds);

    // --- rendering ---------------------------------------------------------
    void set_render_scale(double scale);
    double get_render_scale() const;
    void set_body_scale_exaggeration(double factor);
    // Floating origin.  Call once per frame with the camera's absolute focus;
    // `focus_spacecraft` is the common case.
    void focus_on_spacecraft();
    void focus_on_body(int index);

    // --- readouts ----------------------------------------------------------
    int get_body_count() const;
    godot::String get_body_name(int index) const;
    godot::Vector3 get_body_position(int index) const;   // scene units
    double get_body_radius(int index) const;             // scene units
    godot::Vector3 get_spacecraft_position() const;
    godot::Vector3 get_spacecraft_velocity_direction() const;

    // --- relativistic optics (Milestone 5) ---------------------------------
    // The observer's velocity over c, in the coordinate frame.  Everything the
    // sky needs, and the only physics that passes through GDScript -- which
    // carries it and does not touch it
    // (docs/architecture/relativistic-shaders.md section 6).
    godot::Vector3 get_beta_vector() const;

    // Where the body APPEARS: retarded by the light time against the real
    // ephemeris, then aberrated into the ship's frame.  Distinct from
    // get_body_position(), which is geometric, because they are different
    // questions (docs/physics/relativistic-rendering.md section 2).
    godot::Vector3 get_body_apparent_position(int index) const;
    double get_body_light_time(int index) const;

    // D = gamma (1 - beta.n) for the body centre.  The shader turns this into
    // colour and brightness and never learns what beta is.
    double get_body_doppler(int index) const;

    // For the per-vertex retarded time in the vertex shader: the body's velocity
    // relative to the observer and the speed of light, BOTH in scene units per
    // second, so that the ratio survives the conversion to float.
    godot::Vector3 get_body_relative_velocity_scene(int index) const;
    double get_light_speed_scene() const;

    // Turns light-time and aberration off, so that the difference is visible
    // rather than argued about.  The STATE is untouched either way -- this
    // switches which question the renderer asks, not what is true.
    void set_apparent_positions_enabled(bool enabled);
    bool get_apparent_positions_enabled() const;

    // --- attitude ----------------------------------------------------------
    // Godot's Quaternion is scalar LAST; the core's is scalar first (ADR-0008).
    // The reordering happens here and nowhere else.
    godot::Quaternion get_spacecraft_orientation() const;
    godot::Basis get_spacecraft_basis() const;

    // "prograde", "retrograde", "normal", "anti_normal", "radial_in",
    // "radial_out", or "" to hold the current attitude.
    bool set_pointing_mode(const godot::String& mode);
    godot::String get_pointing_mode() const;
    double get_pointing_error_deg() const;

    // Direct torque command in the BODY frame, in newton metres. Non-zero
    // overrides the pointing controller; Vector3.ZERO hands it back.
    void set_manual_torque(const godot::Vector3& torque_body);

    // --- main engine -------------------------------------------------------
    // Thrust goes along the nose, so where the burn goes is decided by where the
    // ship is pointing: aim with 1-6, then open the throttle.
    void set_throttle(double throttle);
    double get_throttle() const;

    // Two operating points of one power plant: IMPULSE trades exhaust velocity
    // for thrust, CRUISE the other way. See config/engines/torch-mk3.json.
    bool set_engine_mode(const godot::String& mode);
    void cycle_engine_mode();
    godot::String get_engine_mode() const;

    // Everything else, as a Dictionary: the cockpit reads this once per frame
    // instead of making twenty calls.
    godot::Dictionary get_snapshot() const;

    [[nodiscard]] bool is_ready() const { return builder_ != nullptr; }
    godot::String get_last_error() const;

protected:
    static void _bind_methods();

private:
    void rebuild_snapshot();

    std::shared_ptr<const sf::ephemeris::SpiceKernelSet> kernels_;
    std::unique_ptr<sf::ephemeris::SpiceEphemerisProvider> provider_;
    std::unique_ptr<sf::ephemeris::SpiceTimeConverter> time_converter_;
    std::unique_ptr<sf::celestial::BodyCatalog> catalog_;
    std::unique_ptr<sf::gravity::CompositeForceModel> forces_;
    std::unique_ptr<sf::propagation::DormandPrince54Propagator> propagator_;
    std::unique_ptr<sf::simulation::SnapshotBuilder> builder_;
    std::unique_ptr<sf::simulation::SimulationClock> clock_;
    std::unique_ptr<sf::attitude::InertiaTensor> inertia_;
    std::unique_ptr<sf::attitude::RcsSystem> rcs_;
    std::unique_ptr<sf::attitude::PointingController> pointing_;
    std::unique_ptr<sf::attitude::RcsForce> rcs_force_;
    std::unique_ptr<sf::spacecraft::Spacecraft> craft_;
    std::unique_ptr<sf::propulsion::MainEngineForce> main_engine_;

    sf::propagation::PropagationState state_{};
    bool apparent_positions_{true};
    sf::simulation::SimulationSnapshot snapshot_{};
    sf::render::RenderTransform transform_{1.0e-6};

    std::string last_error_;
};

}  // namespace spaceflight_godot
