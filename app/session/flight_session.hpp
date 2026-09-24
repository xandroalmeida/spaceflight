#pragma once

// FlightSession: the whole surface the presentation gets.
//
// It owns the scientific core, advances it, and hands out SNAPSHOTS -- never
// pointers into the integrator, never an ephemeris call, never a force model
// (rule 22, ADR-0002).  Rendering starts where this class ends.
//
// Everything crossing into the presentation is either a plain scalar, a string,
// a struct of those, or a RenderVec3 that has already been through
// RenderTransform: float, camera-relative, scene units.  See
// docs/architecture/rendering.md.
//
// Until ADR-0009 this was the GDExtension node `SpaceflightSimulation`, and the
// answers crossed into GDScript as Dictionaries.  The Dictionaries are now
// structs -- a misspelt key used to read back as a silent zero, and a misspelt
// member does not compile -- and every method kept its meaning and its name.

#include "core/attitude/inertia.hpp"
#include "core/attitude/pointing_controller.hpp"
#include "core/attitude/rcs.hpp"
#include "core/celestial/body_catalog.hpp"
#include "core/celestial/solar_system.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "core/gravity/composite_force_model.hpp"
#include "core/math/mat3.hpp"
#include "core/math/quaternion.hpp"
#include "core/navigation/b_plane.hpp"
#include "core/navigation/maneuver.hpp"
#include "core/navigation/maneuver_executor.hpp"
#include "core/navigation/mission_execution.hpp"
#include "core/navigation/mission_planner.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/propulsion/main_engine_force.hpp"
#include "core/render/render_transform.hpp"
#include "core/simulation/simulation_clock.hpp"
#include "core/simulation/snapshot.hpp"
#include "core/spacecraft/spacecraft.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace sf::app {

using render::RenderVec3;

// --- what the readouts are made of -----------------------------------------

// Everything the cockpit reads once per frame instead of making twenty calls.
struct SnapshotView {
    bool valid{false};

    double time_tdb_s{0.0};
    double elapsed_s{0.0};
    double proper_time_s{0.0};
    double clock_difference_s{0.0};
    double time_warp{1.0};

    std::string reference;
    double altitude_m{0.0};
    double distance_m{0.0};
    double speed_ms{0.0};
    double barycentric_speed_ms{0.0};
    double acceleration_ms2{0.0};

    double mass_kg{0.0};
    double propellant_kg{0.0};
    double delta_v_budget_ms{0.0};
    double thrust_n{0.0};
    double throttle{0.0};
    std::string engine_mode;
    double mass_flow_kg_s{0.0};
    double endurance_s{0.0};
    double thrust_along_track{0.0};
    double specific_energy_rate{0.0};
    double exhaust_velocity_c{0.0};
    double max_thrust_n{0.0};          // the current mode's thrust at full throttle

    double apoapsis_m{0.0};
    double periapsis_m{0.0};
    double semi_major_axis_m{0.0};
    double eccentricity{0.0};
    double inclination_deg{0.0};
    double period_s{0.0};

    std::string target;
    double target_distance_m{0.0};
    double target_relative_speed_ms{0.0};

    double rotation_rate_deg_s{0.0};
    double angle_to_prograde_deg{0.0};
    double angle_to_nadir_deg{0.0};
    std::string pointing_mode;
    double pointing_error_deg{0.0};

    double beta{0.0};
    double lorentz_factor{1.0};
    // gamma - 1, NOT computed by subtracting: at orbital speeds that throws
    // away seven digits (core/simulation/snapshot.hpp).
    double lorentz_factor_minus_one{0.0};

    double render_resolution_m{0.0};
    double render_resolution_ship_m{0.0};
};

// The guidance directions, as unit vectors in the integration frame.  Absent
// members are directions the controller could not define at this instant.
struct FlightDirections {
    std::optional<math::Vec3> prograde;
    std::optional<math::Vec3> retrograde;
    std::optional<math::Vec3> normal;
    std::optional<math::Vec3> anti_normal;
    std::optional<math::Vec3> radial_out;
    std::optional<math::Vec3> radial_in;
    std::optional<math::Vec3> nose;
    std::optional<math::Vec3> target;
    std::optional<math::Vec3> anti_target;
    std::optional<math::Vec3> sun;

    // By the name the pointing modes use ("prograde", "anti_normal", ...).
    [[nodiscard]] std::optional<math::Vec3> by_name(const std::string& key) const;
};

// Section 15 of Milestone 6.2: everything the computer shows BEFORE execution,
// all of it out of the core's MissionMetrics.  Nothing here computes; it
// renames.
struct PlanSummary {
    bool valid{false};
    bool cancelled{false};

    std::string target;
    std::string origin;
    double departure_tdb_s{0.0};
    double arrival_tdb_s{0.0};
    double seconds_to_ignition{0.0};
    double seconds_to_insertion{0.0};
    bool armed{false};
    double time_of_flight_s{0.0};
    double time_of_flight_days{0.0};
    double transfer_angle_deg{0.0};
    std::string branch;

    double injection_delta_v{0.0};
    double midcourse_delta_v{0.0};
    double insertion_delta_v{0.0};
    double total_delta_v{0.0};
    double delta_v_available{0.0};
    double injection_duration_s{0.0};
    double insertion_duration_s{0.0};

    double v_infinity{0.0};
    double flyby_periapsis_m{0.0};
    double predicted_periapsis_m{0.0};
    double predicted_apoapsis_m{0.0};
    double predicted_eccentricity{0.0};
    double predicted_inclination_deg{0.0};
    double predicted_raan_deg{0.0};

    double propellant_required_kg{0.0};
    double propellant_remaining_kg{0.0};

    double pointing_error_mean_deg{0.0};
    double pointing_error_peak_deg{0.0};
    double rcs_propellant_kg{0.0};
    double rcs_duty_cycle{0.0};
    double torque_saturation{0.0};

    std::string phase;
    int burns{0};
    bool burning{false};
    bool done{false};
};

// One geometry the search actually flew (section 13).
struct PlanAlternative {
    std::string label;
    bool feasible{false};
    double departure_tdb_s{0.0};
    double time_of_flight_days{0.0};
    double time_of_flight_s{0.0};
    double departure_coast_s{0.0};
    std::string branch;
    double injection_delta_v{0.0};
    double insertion_delta_v{0.0};
    double capture_delta_v{0.0};
    double total_delta_v{0.0};
    double predicted_periapsis_m{0.0};
    double predicted_apoapsis_m{0.0};
    double predicted_eccentricity{0.0};
    double predicted_inclination_deg{0.0};
    double predicted_raan_deg{0.0};
    std::string failure;
};

struct PredictedActual {
    double predicted{0.0};
    double actual{0.0};
    double difference{0.0};
    bool recorded{false};
};

// Section 17: predicted against actual.  `recorded` is false until the orbit
// has settled.
struct MissionOutcomeView {
    bool recorded{false};
    PredictedActual periapsis_m;
    PredictedActual apoapsis_m;
    PredictedActual eccentricity;
    PredictedActual inclination_deg;
    PredictedActual propellant_kg;
    PredictedActual arrival_tdb_s;
    PredictedActual capture_delta_v;
    std::string note;
};

// The osculating orbit about the mission target, when the ship is close enough
// to it for that to mean anything.
struct OrbitAboutTarget {
    bool valid{false};
    std::string body;
    double radius_m{0.0};
    double periapsis_m{0.0};
    double apoapsis_m{0.0};
    double eccentricity{0.0};
    double inclination_deg{0.0};
    double period_s{0.0};
    double distance_m{0.0};
    double speed_ms{0.0};
    bool captured{false};
};

struct ManeuverView {
    std::string name;
    std::string guidance;
    double ignition_tdb_s{0.0};
    double cutoff_tdb_s{0.0};
    double duration_s{0.0};
    double throttle{0.0};
    double seconds_to_ignition{0.0};
    bool active{false};
    bool done{false};
    bool located{false};
    RenderVec3 position{};       // scene units, on the planned arc
};

struct PlanningProgress {
    bool present{false};         // false when nothing is running or pending
    bool running{false};
    bool cancelled{false};
    std::string stage;
    long long candidates_considered{0};
    long long candidates_screened{0};
    long long candidates_flown{0};
    long long candidates_succeeded{0};
    double best_total_delta_v{0.0};
    long long integrator_steps{0};
    double wall_seconds{0.0};
};

struct BodyDirectoryEntry {
    std::string name;
    int naif_id{0};
    std::string type;
    int parent{0};
    std::string parent_name;
    double radius_m{0.0};
    double gm{0.0};
    bool has_ephemeris{false};
    bool position_substituted{false};
    bool can_be_destination{false};
    std::string mission_support;
};

struct ThrusterView {
    std::string name;
    math::Vec3 position{};          // body frame, metres, NOT through RenderTransform
    math::Vec3 force_direction{};   // the force ON THE SHIP; the exhaust leaves the other way
};

// The heliocentric map: Sun-centred J2000, in METRES, and it says so.
struct SystemMapBody {
    std::string name;
    int naif_id{0};
    math::Vec3 position{};
    double radius_m{0.0};
    bool is_sun{false};
    bool is_origin{false};
    bool is_destination{false};
};

struct SystemMapManeuver {
    std::string name;
    double seconds_to_ignition{0.0};
    bool done{false};
    bool active{false};
    bool located{false};
    math::Vec3 position{};
};

struct SystemMap {
    bool valid{false};
    std::string frame;
    std::string units;
    std::string centre;
    std::vector<SystemMapBody> bodies;
    math::Vec3 ship_position{};
    math::Vec3 ship_velocity{};
    std::vector<math::Vec3> planned_trajectory;
    std::optional<math::Vec3> origin_at_departure;
    std::optional<math::Vec3> destination_at_arrival;
    std::optional<math::Vec3> destination_at_trajectory_end;
    std::vector<SystemMapManeuver> maneuvers;
    std::string destination;
    std::string phase;
};

struct SystemOrbitPath {
    std::vector<math::Vec3> path;
    double period_s{0.0};
    bool clipped{false};
};

// A column-major 3x3: columns are the body axes in the integration frame.
struct BodyAxes {
    math::Vec3 x{1.0, 0.0, 0.0};
    math::Vec3 y{0.0, 1.0, 0.0};
    math::Vec3 z{0.0, 0.0, 1.0};
};

// Where errors are reported.  Defaults to stderr; the application routes them to
// its log.
using DiagnosticSink = std::function<void(const std::string& message, bool is_error)>;

class FlightSession {
public:
    FlightSession();
    ~FlightSession();
    FlightSession(const FlightSession&) = delete;
    FlightSession& operator=(const FlightSession&) = delete;

    void set_diagnostic_sink(DiagnosticSink sink) { sink_ = std::move(sink); }

    // --- setup -------------------------------------------------------------
    // Loads SPICE kernels and places the ship in a circular orbit.  Returns
    // false with the reason in last_error() on failure; it never throws.
    bool configure(const std::string& kernel_directory, const std::string& epoch_utc);
    bool start_circular_orbit(double altitude_m, double inclination_deg);

    // Points the nose along the velocity with the planet under the floor: the
    // local-vertical/local-horizontal attitude a parking orbit is actually flown
    // in.
    //
    // Separate from start_circular_orbit, and not folded into it, because the
    // two answer to different things. The scenario setter is what the campaign
    // tools and the headless verification use, and it leaves the attitude at
    // identity so that a slew can be commanded and MEASURED from a known start.
    // This is a cockpit convenience: the first frame of a new flight has to show
    // the Earth (Milestone 7 rule 66), and at identity the nose points at the
    // zenith and the window is full of empty sky.
    //
    // It writes the quaternion directly. That is legitimate here and nowhere
    // else: it is scenario setup, before anything is integrated -- the same
    // licence start_circular_orbit takes when it writes a position and a
    // velocity. Nothing during flight may do this (rule 28).
    bool align_attitude_to_flight(double nadir_bias_deg);

    // --- time --------------------------------------------------------------
    void set_time_warp(double warp);
    [[nodiscard]] double time_warp() const;

    // Advances by `wall_seconds` of real time, scaled by the warp.  The
    // integrator picks its own steps; the frame rate never reaches it.
    void advance(double wall_seconds);

    // --- rendering ---------------------------------------------------------
    void set_render_scale(double scale);
    [[nodiscard]] double render_scale() const;
    void set_body_scale_exaggeration(double factor);
    // Floating origin.  Call once per frame with the camera's absolute focus;
    // `focus_on_spacecraft` is the common case.
    void focus_on_spacecraft();
    void focus_on_body(int index);

    // --- readouts ----------------------------------------------------------
    [[nodiscard]] int body_count() const;
    [[nodiscard]] std::string body_name(int index) const;
    [[nodiscard]] int body_index(const std::string& name) const;   // -1 if absent
    [[nodiscard]] RenderVec3 body_position(int index) const;         // scene units
    [[nodiscard]] double body_radius(int index) const;               // scene units
    [[nodiscard]] RenderVec3 spacecraft_position() const;
    [[nodiscard]] math::Vec3 spacecraft_velocity_direction() const;

    // --- relativistic optics (Milestone 5) ---------------------------------
    // The observer's velocity over c, in the coordinate frame.  Everything the
    // sky needs, and the only physics that passes through the presentation --
    // which carries it and does not touch it
    // (docs/architecture/relativistic-shaders.md section 6).
    [[nodiscard]] math::Vec3 beta_vector() const;

    // Development-only visual injection. It changes no propagated state; only
    // the observer velocity seen by rendering. A negative beta disables it.
    void set_visual_test_beta(double beta);
    [[nodiscard]] double visual_test_beta() const;

    // Where the body APPEARS: retarded by the light time against the real
    // ephemeris, then aberrated into the ship's frame.  Distinct from
    // body_position(), which is geometric, because they are different
    // questions (docs/physics/relativistic-rendering.md section 2).
    [[nodiscard]] RenderVec3 body_apparent_position(int index) const;
    [[nodiscard]] RenderVec3 body_observed_position(int index, bool retarded, bool aberration) const;
    [[nodiscard]] double body_light_time(int index) const;

    // D = gamma (1 - beta.n) for the body centre.  The shader turns this into
    // colour and brightness and never learns what beta is.
    [[nodiscard]] double body_doppler(int index) const;

    // For the per-vertex retarded time in the vertex shader: the body's velocity
    // relative to the observer and the speed of light, BOTH in scene units per
    // second, so that the ratio survives the conversion to float.
    [[nodiscard]] RenderVec3 body_relative_velocity_scene(int index) const;
    [[nodiscard]] double light_speed_scene() const;

    // Turns light-time and aberration off, so that the difference is visible
    // rather than argued about.  The STATE is untouched either way -- this
    // switches which question the renderer asks, not what is true.
    void set_apparent_positions_enabled(bool enabled);
    [[nodiscard]] bool apparent_positions_enabled() const;

    // Where the body's own frame is pointing, as three columns: the body-fixed
    // axes in the integration frame.
    //
    // Rendering only. The dynamics never asks how far a planet has turned --
    // an axially symmetric gravity field does not depend on it -- but a textured
    // Earth that does not turn under a 400 km orbit is wrong within a minute of
    // watching. Returns the identity for a body with no body-fixed frame in the
    // loaded kernels, which draws it unrotated rather than failing.
    [[nodiscard]] BodyAxes body_orientation(int index) const;

    // --- attitude ----------------------------------------------------------
    // Scalar first (ADR-0008), and body -> inertial.
    [[nodiscard]] math::Quaternion spacecraft_orientation() const;
    [[nodiscard]] BodyAxes spacecraft_axes() const;

    // "prograde", "retrograde", "normal", "anti_normal", "radial_in",
    // "radial_out", or "" to hold the current attitude.
    bool set_pointing_mode(const std::string& mode);
    [[nodiscard]] std::string pointing_mode() const;
    [[nodiscard]] double pointing_error_deg() const;

    // Direct torque command in the BODY frame, in newton metres. Non-zero
    // overrides the pointing controller; zero hands it back.
    void set_manual_torque(const math::Vec3& torque_body);

    // Direct translation command in the BODY frame, in newtons. Independent of
    // the torque: the RCS layout can push without turning, and the cockpit's
    // translation keys are that (rule 13).
    void set_manual_translation(const math::Vec3& force_body);

    // --- main engine -------------------------------------------------------
    // Thrust goes along the nose, so where the burn goes is decided by where the
    // ship is pointing: aim with 1-6, then open the throttle.
    void set_throttle(double throttle);
    [[nodiscard]] double throttle() const;

    // Two operating points of one power plant: IMPULSE trades exhaust velocity
    // for thrust, CRUISE the other way. See config/engines/torch-mk3.json.
    bool set_engine_mode(const std::string& mode);
    void cycle_engine_mode();
    [[nodiscard]] std::string engine_mode() const;

    // The guidance directions, all of them, as unit vectors in the integration
    // frame: prograde, retrograde, normal, anti_normal, radial_out, radial_in,
    // target, anti_target, plus "nose" and "sun".
    //
    // Every one of them comes from PointingController::direction_for, which is
    // the routine the autopilot steers by. That is the whole point: the marker
    // the pilot lines the nose up with has to be the direction the autopilot
    // would have taken it to, and the only way to guarantee that is for them to
    // be the same function call.
    [[nodiscard]] FlightDirections flight_directions() const;

    // --- navigation target (Milestone 7) ------------------------------------
    // Which body the cockpit measures distance and relative speed against.
    //
    // It used to be wired to the Moon in configure(), which was fine while the
    // Moon was the only destination and became a lie the moment the cockpit grew
    // a target selector: a display that says TARGET and cannot be pointed
    // anywhere else is a label, not an instrument.
    [[nodiscard]] std::vector<std::string> selectable_targets() const;

    // The Solar System, as the user interface has to show it (Milestone 8 rules
    // 20 and 21): one entry per body, in directory order, each naming its parent
    // and saying how far the planner has been qualified for it.
    //
    // A TREE expressed as a flat list with parents on it, because the consumer
    // is a menu and a menu walks. What must not happen is the user interface
    // keeping a body list of its own: that is the second source of truth rule
    // 19 forbids, and it is how "Mars" ends up selectable in a build whose
    // kernels cannot place it.
    [[nodiscard]] std::vector<BodyDirectoryEntry> body_directory() const;
    bool set_target_body(const std::string& name);
    [[nodiscard]] std::string target_body() const;

    // --- attitude actuators (Milestone 7) -----------------------------------
    // Where the twelve thrusters ARE, in the body frame, and what each one is
    // doing right now.
    //
    // Rule 15 of the milestone: the renderer lights the thruster the ACTUATOR
    // lit, not the one the key asked for. The two are different -- the greedy
    // allocator opens each thruster in proportion to how much its own torque
    // direction agrees with the demand, so a diagonal command fires four
    // thrusters at fractional throttle and a saturated one fires two wide open.
    // Drawing the key press instead would show a plume where no propellant is
    // leaving, which is exactly the kind of quiet lie this project avoids.
    //
    // The allocation is recomputed here from the SAME RcsSystem::allocate the
    // force model flies, against the state and epoch of the current snapshot.
    // Not cached from the last integration stage: a Dormand-Prince step
    // evaluates the force model seven times at seven different times, and "the
    // last one" is an arbitrary one of those. This is the actuator state at the
    // instant the snapshot describes, which is the instant being drawn.
    [[nodiscard]] std::vector<ThrusterView> rcs_thrusters() const;
    [[nodiscard]] std::vector<double> rcs_throttles() const;

    // PROPER acceleration, body frame [m/s^2]: what an accelerometer bolted to
    // the ship reads, and what the crew feels. Only the NON-gravitational forces
    // -- the main engine along the nose (+x) and the RCS's net force -- over the
    // mass. In free fall it is zero, whatever the orbit's coordinate
    // acceleration. The RCS term is zero for a pure rotation (the couples
    // cancel) and is the whole reading during a translation.
    [[nodiscard]] math::Vec3 proper_acceleration_body() const;

    // --- orbit geometry (Milestone 7) ---------------------------------------
    // The osculating ellipse, sampled. In scene units, ready to draw.
    //
    // Here and not in the presentation because it is orbital mechanics (rule
    // 77): the samples come from trajectory::state_from_elements, the same
    // inverse the campaign tool uses to turn "400 km at 51.6 degrees" into a
    // state vector. A Kepler solver in the renderer would be a second source of
    // truth for the shape of the orbit, and the two would disagree the first
    // time the elements meant anything subtle.
    //
    // An unbound orbit is sampled over the true anomalies that are actually
    // reachable rather than over the full circle, because a hyperbola has
    // asymptotes and drawing past them produces a line to nowhere.
    [[nodiscard]] std::vector<RenderVec3> orbit_track(int samples) const;

    // The same question for a celestial body about the ship's reference body,
    // and the answer comes from the EPHEMERIS rather than from elements: the
    // Moon's orbit is not an ellipse and drawing it as one would be inventing a
    // trajectory the simulation does not fly.
    [[nodiscard]] std::vector<RenderVec3> body_orbit_track(int index, int samples) const;

    // --- missions ----------------------------------------------------------
    // Plans a transfer to a body -- BLOCKING -- and holds the result.
    //
    // Every number in the answer comes from core/navigation/mission_planner.hpp:
    // the same entry point, with the same request type, that the 365-epoch
    // campaign goes through (Milestone 6.2 sections 1-5). Nothing in the
    // application decides anything about a trajectory.
    //
    // NOTE what is NOT an argument: the time of flight. It is what the search
    // decides, and fixing it at 4.5 days is precisely the defect that made
    // Milestone 6's campaign fail 82 % of its epochs.
    //
    // Planning does NOT arm. Milestone 7 rule 26 puts an EXECUTE and a CANCEL in
    // front of the pilot, and a plan that is already flying by the time those
    // buttons appear makes CANCEL a lie about what just happened. The result is
    // held, shown, and installed only by arm_plan().
    PlanSummary plan_transfer(const std::string& target_body, double periapsis_altitude_km,
                              double apoapsis_altitude_km, double search_hours);

    // --- planning without freezing the game (Milestone 8 rules 48, 49, 120) ---
    //
    // `plan_transfer` above blocks, and for a lunar transfer that is a defensible
    // second. An Earth-Mars search is 64 seconds, measured, and a minute of
    // frozen frames is not a keypress -- it is a crash as far as anyone watching
    // is concerned.
    //
    // So: one worker thread, started by `start_planning`, polled by
    // `planning_progress`, collected by `collect_plan`.
    //
    // ---------------------------------------------------------------------
    // Why ONE thread, and why the core does not know about it
    //
    // CSPICE has a global kernel pool and a global error state and is not thread
    // safe. Every toolkit call in this project is already made while holding
    // core/ephemeris/spice_internal.hpp's mutex, so a worker and the frame can
    // both ask the ephemeris questions and the toolkit sees them one at a time.
    // That makes ONE worker correct and it is why there is not a pool: N workers
    // would serialise on that mutex anyway, and the contention would be paid by
    // the frame.
    //
    // The worker touches nothing the frame mutates. The request is COPIED before
    // it starts -- the ship's state and the epoch by value, the provider,
    // catalogue and vehicle by pointer to objects that are not written during
    // flight -- and the result is handed back through a mutex. The planner itself
    // knows none of this: it takes a `cancelled` callback and a progress
    // callback, and whether they are backed by a thread is the caller's business.
    bool start_planning(const std::string& target_body, double periapsis_altitude_km,
                        double apoapsis_altitude_km, double search_hours);
    [[nodiscard]] bool is_planning() const;

    // Replan ONE of the geometries the last search flew (rule 42, and step 8 of
    // the vertical slice: "select an alternative").
    //
    // `index` is into plan_alternatives(). The search is pinned to that
    // candidate's departure point, flight time and Lambert branch -- so it costs
    // one candidate instead of 768, a few seconds instead of a minute -- and it
    // runs through the SAME path as any other plan. A pinned geometry that
    // violates a hard constraint is still refused; this is a choice of question,
    // not a way round the answer.
    bool start_planning_alternative(int index);

    // Counts, and the stage the search is in. `present` is false when nothing
    // is running.
    [[nodiscard]] PlanningProgress planning_progress() const;

    // Empty (valid == false) until the worker has finished. Calling it once the
    // worker is done joins the thread, installs the result and returns the same
    // summary `plan_transfer` would have; `cancelled` is set when the search was
    // stopped.
    PlanSummary collect_plan();

    // Asks the search to stop. It stops between candidates, never inside one.
    void cancel_planning();

    // --- the Solar System map (Milestone 8 rules 26-31) ---------------------
    //
    // A SECOND map, and a second frame, declared rather than assumed: Sun-centred
    // J2000, in METRES. Metres and not scene units because the floating origin
    // is a rendering device tied to where the camera is, and a map of the Solar
    // System is not drawn from the cockpit.
    //
    // Float resolution at Neptune's distance is 270 km. That is stated rather
    // than worried about: it is a map, and 270 km at 4.5e12 m is a fifth of a
    // pixel at any zoom that shows Neptune at all.
    [[nodiscard]] SystemMap system_map() const;

    // The planet tracks, sampled from the EPHEMERIS rather than drawn as
    // ellipses (rule 29). Separate from system_map() because they cost a
    // thousand ephemeris calls and do not change from frame to frame: the caller
    // asks once when the map opens.
    [[nodiscard]] std::map<std::string, SystemOrbitPath> system_orbit_paths(int samples_per_body) const;

    // --- which body the cockpit measures against (rules 63-65) --------------
    //
    // ⚠️ This is NAVIGATION CONTEXT and not physics. Rule 65 is explicit: no
    // sphere of influence appears anywhere in the dynamics, gravity stays
    // multibody at every instant, and nothing below changes a single force. What
    // it changes is which body the readouts are relative to -- which is a
    // question about a display, and is answered here so that six instruments
    // cannot answer it six different ways.
    //
    // The rule: the DEEPEST body in the directory whose gravitational
    // neighbourhood contains the ship, walking down from the Sun. Neighbourhood
    // meaning r = R (m/M)^(2/5) against the body's own parent -- the same
    // arithmetic the planner uses to size a corrector tolerance, and used here
    // for the same reason: it is a length scale, not a boundary.
    bool set_reference_body(const std::string& name);
    [[nodiscard]] std::string reference_body() const;

    // Off leaves the reference wherever it was last set, which is what the
    // Milestone 7 scene did (the Earth, forever).
    void set_auto_reference(bool enabled);
    [[nodiscard]] bool auto_reference() const;

    // Installs the last planned transfer. False, with a reason in
    // last_error(), if there is nothing to arm or if the departure has already
    // passed -- flying a plan whose injection epoch is behind the ship would
    // fire the capture burn in empty space.
    bool arm_plan();
    [[nodiscard]] bool has_planned_transfer() const { return planned_.ok(); }

    // Which execution model the planner corrects against: "finite" or
    // "autopilot". The choice is a real one and it is offered rather than
    // buried, because the two differ in both accuracy and cost:
    //
    //   finite      ideal guidance, the engine points where the plan says.
    //               e ~ 0.0017 over the 365-epoch campaign, plans in ~7 s.
    //   autopilot   the attitude controller is inside the corrected map, so the
    //               pointing lag is part of the trajectory rather than assumed
    //               away. Strictly the better physics, and it costs minutes per
    //               plan.
    //
    // "finite" by default: it is what the scene has always flown and what
    // docs/validation/lunar-navigation-campaign-v2.csv qualifies.
    bool set_execution_model(const std::string& model);
    [[nodiscard]] std::string execution_model() const;

    // The osculating orbit about the mission target, when the ship is close
    // enough to it for that to mean anything. `valid` is false otherwise.
    [[nodiscard]] OrbitAboutTarget orbit_about_target() const;
    [[nodiscard]] bool has_plan() const;
    void clear_plan();
    [[nodiscard]] PlanSummary plan() const;

    // The geometries the search actually flew, each with the orbit it would have
    // arrived in (section 13).
    [[nodiscard]] std::vector<PlanAlternative> plan_alternatives() const;

    // Section 16: which of the mission states the flight is in. The
    // classification is core/navigation/mission_execution.hpp's.
    [[nodiscard]] std::string mission_phase() const;

    // Section 17: predicted against actual.
    [[nodiscard]] MissionOutcomeView mission_outcome() const;

    // The burns of the armed plan: label, epochs, delta-v, and where the ship
    // will be when each one lights, in scene units.
    [[nodiscard]] std::vector<ManeuverView> maneuvers() const;

    // The planned arc, in scene units, for drawing, anchored at the origin body.
    [[nodiscard]] std::vector<RenderVec3> planned_trajectory() const;

    // Everything else, in one struct: the cockpit reads this once per frame.
    [[nodiscard]] SnapshotView snapshot() const;

    // The raw snapshot, for the headless read-out and the tests.
    [[nodiscard]] const simulation::SimulationSnapshot& core_snapshot() const { return snapshot_; }

    [[nodiscard]] bool is_ready() const { return builder_ != nullptr; }
    [[nodiscard]] const std::string& last_error() const { return last_error_; }

private:
    void rebuild_snapshot();
    void report(const std::string& message, bool is_error) const;
    [[nodiscard]] math::Vec3 optics_observer_velocity() const;
    template <typename Fn>
    bool guarded(const char* what, Fn&& fn);

    std::shared_ptr<const ephemeris::SpiceKernelSet> kernels_;
    std::unique_ptr<ephemeris::SpiceEphemerisProvider> provider_;
    std::unique_ptr<ephemeris::SpiceTimeConverter> time_converter_;
    std::unique_ptr<celestial::BodyCatalog> catalog_;
    // The directory: what EXISTS, as opposed to whose mass is in the force
    // model. Resolved once at configure() against the loaded kernels, because
    // availability is a property of those and not of the code.
    std::unique_ptr<celestial::SolarSystem> system_;
    std::unique_ptr<gravity::CompositeForceModel> forces_;
    std::unique_ptr<propagation::DormandPrince54Propagator> propagator_;
    std::unique_ptr<simulation::SnapshotBuilder> builder_;
    std::unique_ptr<simulation::SimulationClock> clock_;
    std::unique_ptr<attitude::InertiaTensor> inertia_;
    std::unique_ptr<attitude::RcsSystem> rcs_;
    std::unique_ptr<attitude::PointingController> pointing_;
    std::unique_ptr<attitude::RcsForce> rcs_force_;
    std::unique_ptr<spacecraft::Spacecraft> craft_;
    std::unique_ptr<propulsion::MainEngineForce> main_engine_;
    // Both live for the life of the session, and that is load-bearing: the force
    // model holds a REFERENCE to the executor and the executor holds one to the
    // plan (gravity::CompositeForceModel::add_reference). Rebuilding either when
    // a plan is made would leave the force model pointing at freed memory.
    // Planning replaces the plan's CONTENTS instead.
    std::unique_ptr<navigation::ManeuverPlan> plan_;
    std::unique_ptr<navigation::ManeuverExecutor> executor_;

    // The last plan, whole. Kept rather than flattened because sections 15 and
    // 17 want it more than once and from more than one angle.
    navigation::MissionPlanResult planned_{};
    navigation::ExecutionModel execution_{navigation::ExecutionModel::FiniteBurn};
    navigation::MissionExecution mission_{};

    propagation::PropagationState state_{};
    bool apparent_positions_{true};
    double visual_test_beta_{-1.0};
    simulation::SimulationSnapshot snapshot_{};
    render::RenderTransform transform_{1.0e-6};

    std::string last_error_;
    DiagnosticSink sink_{};

    // --- the planning worker ------------------------------------------------
    struct PlanningJob {
        std::thread worker;
        std::atomic<bool> running{false};
        std::atomic<bool> cancel{false};

        mutable std::mutex mutex;
        navigation::SearchProgress progress{};
        navigation::MissionPlanResult result{};
        std::string error;
        bool have_result{false};
        double wall_seconds{0.0};
    };
    std::unique_ptr<PlanningJob> job_;

    // What the last search was ASKED for, so that replanning one of its
    // alternatives asks the same question about the same orbit. Not read from
    // planned_.metrics: those are what the planner ACHIEVED, and a replan that
    // silently retargeted itself at what it happened to hit would be a different
    // mission wearing the same label.
    struct LastRequest {
        bool valid{false};
        celestial::BodyId target{};
        double periapsis_altitude_km{0.0};
        double apoapsis_altitude_km{0.0};
        double search_hours{0.0};
    };
    LastRequest last_request_{};
    bool auto_reference_{true};

    [[nodiscard]] celestial::BodyId natural_reference() const;

    void join_worker();

    // The one body of start_planning() and start_planning_alternative(): the
    // only difference between them is the pin.
    bool begin_planning(celestial::BodyId target, double periapsis_altitude_km,
                        double apoapsis_altitude_km, double search_hours,
                        const navigation::TransferConfig::PinnedDeparture& pin);
};

}  // namespace sf::app
