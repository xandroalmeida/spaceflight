#pragma once

// The ship, in metres, in the body frame (rules 5, 6, 7), and the two things
// on it that move: the main engine plume and the RCS jets.
//
// This is ONLY appearance. Nothing here is read by anything: the position, the
// attitude, the mass and the thrust keep coming from core/, and the renderer
// places these parts under the ship's attitude. If this file were deleted the
// simulation would be identical -- which is the test that it is not a source of
// truth (rule 7).
//
// Frame: +x is the nose, and the same +x the main engine pushes along
// (MainEngineForce). +z is the ship's "up". The twelve RCS thrusters are at
// (+-2, 0, 0), (0, +-2, 0), (0, 0, +-2) because that is where
// RcsSystem::couples(2.0, ...) puts them -- the nacelles are drawn WHERE the
// physics uses them, and not where they would look nice.
//
// ## Scale, and the debt it leaves on record
//
// The core's inertia tensor is a solid box of 1000 kg and 8 x 3 x 3 m. The
// pressurised hull drawn here is 9 m by 3 m in diameter, which is that box;
// tanks, radiators and engine are OUTSIDE it, aft. The inertia models the mass
// distribution, not the extent of the vehicle, and "perfect mass distribution"
// is explicitly outside M7 (rule 57). The discrepancy is recorded as
// PHYSICS_DEBT in docs/assets/spacecraft/dimensions.md instead of being hidden.

#include "app/presentation/scene/ship_materials.hpp"
#include "app/session/flight_session.hpp"

#include <vector>

namespace sf::app {

class SpacecraftVisual {
public:
    static constexpr double NOSE_TIP = 5.0;         // [m] tip of the cockpit, along +x
    static constexpr double CORE_FORWARD = 3.6;     // command module's forward bulkhead
    static constexpr double CORE_AFT = -4.0;        // habitat's aft bulkhead
    static constexpr double CORE_RADIUS = 1.5;
    static constexpr double TANK_RADIUS = 1.7;
    static constexpr double TANK_FORWARD = -4.5;
    static constexpr double TANK_AFT = -10.5;
    static constexpr double TANK_OFFSET = 2.6;      // lateral distance of the tanks' axis
    static constexpr double POWER_FORWARD = -10.0;
    static constexpr double POWER_AFT = -12.5;
    static constexpr double ENGINE_THROAT = -13.0;
    static constexpr double ENGINE_EXIT = -17.0;
    static constexpr double BELL_EXIT_RADIUS = 2.0;
    static constexpr double RADIATOR_SPAN = 7.0;    // half-width; 14 m tip to tip
    static constexpr double RCS_ARM = 2.0;          // must match RcsSystem::couples

    SpacecraftVisual(MeshLibrary& meshes, const ShipMaterials& materials);

    // The navigation lights blink at 1 Hz. It is the only motion of the ship
    // that does not come from the core, and it exists because an absolutely
    // static structure in the dark reads as a frozen picture -- including when
    // the simulation has hung, which is exactly what it must not hide.
    void advance(double delta);

    [[nodiscard]] const std::vector<Part>& parts() const { return parts_; }
    // Where the plume hangs: the nozzle exit, in the body frame.
    [[nodiscard]] static Transform3 engine_mount() { return Transform3{Basis{}, Vec3{ENGINE_EXIT, 0.0, 0.0}}; }

    // How much of the ship fits in a sphere around the centre of mass, in metres.
    [[nodiscard]] static double bounding_radius();

private:
    void build_command_module();
    void build_habitat();
    void build_truss();
    void build_tanks();
    void build_power_section();
    void build_engine();
    void build_radiators();
    void build_antennas();
    void build_rcs_pods();
    void build_navigation_lights();

    Part cylinder(double radius_aft, double radius_forward, double x_aft, double x_forward, bool caps,
                  const Material& material);
    Part box(const Vec3& size, const Vec3& at, const Material& material);

    MeshLibrary& meshes_;
    const ShipMaterials& materials_;
    std::vector<Part> parts_;
    std::vector<std::size_t> navigation_lights_;
    double light_phase_{0.0};
};

// The main engine plume (rule 16).
//
// The intensity comes from the REAL THRUST -- the snapshot's `thrust_n`, which
// is what MainEngineForce is applying to the state at this instant -- and not
// from the key the pilot held. The difference is observable: with the tank
// empty the key keeps working and the thrust is zero, and the plume has to go
// out.
//
// It is not plasma physics. It is glowing gas drawn by shaders/plume.frag: a
// soft sheath, a hotter core and the bright exit plane of the nozzle, all
// additive, all without an edge. What it looks like follows the EXHAUST
// VELOCITY of the mode that is running (`Style`), because that is what changes
// between operating points: a chemical flame, a dense hydrogen plasma, a
// relativistic beam. M7 asks for no exhaust physics (rule 16); it asks that a
// running engine look like a running engine -- and that it go out when the
// engine does.
class EnginePlume {
public:
    static constexpr double IMPULSE_REFERENCE_N = 200000.0;   // IMPULSE mode reference thrust
    static constexpr double MAX_LENGTH = 14.0;                // [m] at full power, before the style's factor
    static constexpr double CORE_FRACTION = 0.42;

    // One look per kind of exhaust. Colours sRGB-encoded.
    enum class Kind { Chemical, FusionDense, Relativistic, Annihilation };
    struct Style {
        Kind kind;
        const char* name;
        Colour core;            // the hot inner jet, at the nozzle
        Colour sheath;          // the expanding outer gas
        Colour tail;            // what the sheath cools to downstream
        double length;          // x MAX_LENGTH
        double nozzle_radius;   // [m] where the jet leaves the bell
        double tail_radius;     // [m] at the far end: the divergence of the jet
        double energy;          // brightness of the sheath at full power
        double turbulence;
        double flow_speed;      // streaks, in plume lengths per second
    };
    // The style for an exhaust velocity: below 0.001 c a chemical flame (the
    // Orbital Tug's 9 km/s), up to 0.2 c a dense fusion plasma (IMPULSE and the
    // Mk I/II), up to 0.9 c a relativistic beam (CRUISE, 0.5 c), and above that
    // an annihilation beam (RELATIVISTIC, 0.95 c) whose look is not fixed: it is
    // the jet's own light, Doppler-shifted and beamed towards wherever the camera
    // is (`set_viewer`).
    //
    // The Annihilation beam's rest-frame emission: a black body at this
    // temperature. Its colour and brightness on screen are then NOT chosen --
    // they are T' = D T and D^3 eta(D T)/eta(T), from core/relativity/optics.hpp
    // and core/render/blackbody.hpp (docs/physics/relativistic-rendering.md
    // section 13).
    static constexpr double BEAM_REST_TEMPERATURE_K = 12000.0;
    [[nodiscard]] static const Style& style_for(double exhaust_velocity_c);

    explicit EnginePlume(MeshLibrary& meshes);

    // `thrust_n` is the core's instantaneous thrust, measured against the running
    // mode's full-throttle thrust (`set_mode`; 200 kN until told otherwise).
    // CRUISE at full throttle is a full plume: the jet carries the same power as
    // in IMPULSE (the equal-power invariant of torch-mk3.json), in a thinner,
    // faster stream.
    void set_thrust(double thrust_n);
    void set_mode(double exhaust_velocity_c, double max_thrust_n);
    void set_intensity(double value);
    void advance(double delta);
    // Where the camera is, in the ENGINE MOUNT's frame (metres). Only the
    // Annihilation beam uses it: at 0.95 c the jet's light depends on the angle
    // it is seen from, by a factor of 250 between astern and ahead.
    void set_viewer(const Vec3& camera_in_mount);
    // The Doppler factor of the jet towards the camera, and the colour it gives.
    [[nodiscard]] double doppler() const { return doppler_; }
    [[nodiscard]] Colour beam_colour() const { return beam_colour_; }
    // How bright the beam is relative to its rest frame, through the same
    // detector response as the sky: 1 at D = 1, towards 2 blinding, towards 0 dark.
    [[nodiscard]] double beam_brightness() const;

    [[nodiscard]] bool visible() const { return intensity_ > 0.0005; }
    [[nodiscard]] double intensity() const { return intensity_; }
    [[nodiscard]] const Style& style() const { return *style_; }
    // Parts in the ENGINE MOUNT's frame (the nozzle exit).
    [[nodiscard]] std::vector<Part> parts() const;
    // The sheath's transform, for the test that checks what is drawn.
    [[nodiscard]] Transform3 cone_transform() const;
    [[nodiscard]] const std::string& cone_mesh() const;
    // The plume's light, in the engine mount's frame; energy zero when out.
    [[nodiscard]] PointLight light() const;

private:
    struct Meshes {
        std::string sheath;
        std::string core;
    };
    [[nodiscard]] static Transform3 stretch(double length);
    [[nodiscard]] double length() const;
    [[nodiscard]] const Meshes& meshes() const;

    std::vector<Meshes> meshes_;   // one pair per Kind, in Kind order
    std::string glow_mesh_;
    std::string reactor_mesh_;
    const Style* style_;
    double reference_n_{IMPULSE_REFERENCE_N};
    double intensity_{0.0};
    double time_{0.0};
    double exhaust_beta_{0.03};
    double doppler_{1.0};
    double ln_beam_brightness_{0.0};
    Colour beam_colour_{palette::ENGINE_ANNIHILATION};
};

// The RCS jets, lit by the ACTUATOR (rule 15).
//
// The renderer does not know which key was pressed. It takes the vector of
// openings -- FlightSession::rcs_throttles(), the same RcsSystem::allocate the
// force model flies -- and lights each nozzle in proportion to how open that
// nozzle is. The allocator is greedy and proportional: a diagonal command opens
// four thrusters at different fractions, a saturated one opens two at most.
// Drawing the key would show four equal flames where there are two strong and
// two weak ones.
//
// The geometry comes from the core too: rcs_thrusters() gives the position and
// the direction of the FORCE in the body frame. The exhaust leaves the other
// way, and that flip is done here, once, where it is written.
class RcsVisual {
public:
    static constexpr double JET_LENGTH = 1.35;    // [m] fully open
    static constexpr double JET_RADIUS = 0.20;
    static constexpr double THRESHOLD = 0.002;    // below this there is no flame to see

    explicit RcsVisual(MeshLibrary& meshes);

    void build(const std::vector<ThrusterView>& thrusters);
    void set_throttles(const std::vector<double>& throttles);

    // How many nozzles are open, so the RCS instrument says "4 of 12" instead of
    // "on".
    [[nodiscard]] int firing_count() const;
    [[nodiscard]] double total_demand() const;
    // Parts in the body frame.
    [[nodiscard]] std::vector<Part> parts() const;

private:
    struct Jet {
        Transform3 base;    // the orientation that makes the mesh grow along the exhaust
        double open{0.0};
    };
    std::string jet_mesh_;
    std::vector<Jet> jets_;
    std::vector<double> throttles_;
};

}  // namespace sf::app
