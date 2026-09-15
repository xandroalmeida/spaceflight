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
#include "core/celestial/solar_system.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "core/gravity/composite_force_model.hpp"
#include "core/navigation/b_plane.hpp"
#include "core/navigation/maneuver.hpp"
#include "core/navigation/maneuver_executor.hpp"
#include "core/navigation/mission_execution.hpp"
#include "core/navigation/mission_planner.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/propulsion/main_engine_force.hpp"
#include "core/render/render_transform.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/simulation/simulation_clock.hpp"
#include "core/simulation/snapshot.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

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

    // Development-only visual injection. It changes no propagated state; only
    // the observer velocity seen by rendering. A negative beta disables it.
    void set_visual_test_beta(double beta);
    double get_visual_test_beta() const;

    // Where the body APPEARS: retarded by the light time against the real
    // ephemeris, then aberrated into the ship's frame.  Distinct from
    // get_body_position(), which is geometric, because they are different
    // questions (docs/physics/relativistic-rendering.md section 2).
    godot::Vector3 get_body_apparent_position(int index) const;
    godot::Vector3 get_body_observed_position(int index, bool retarded, bool aberration) const;
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

    // Where the body's own frame is pointing, as a Basis whose columns are the
    // body-fixed axes in the integration frame.
    //
    // Rendering only. The dynamics never asks how far a planet has turned --
    // an axially symmetric gravity field does not depend on it -- but a textured
    // Earth that does not turn under a 400 km orbit is wrong within a minute of
    // watching. Returns the identity for a body with no body-fixed frame in the
    // loaded kernels, which draws it unrotated rather than failing.
    godot::Basis get_body_orientation(int index) const;

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

    // Direct translation command in the BODY frame, in newtons. Independent of
    // the torque: the RCS layout can push without turning, and the cockpit's
    // translation keys are that (rule 13).
    void set_manual_translation(const godot::Vector3& force_body);

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

    // The guidance directions, all of them, as unit vectors in the integration
    // frame: prograde, retrograde, normal, anti_normal, radial_out, radial_in,
    // target, anti_target, plus "nose" and "sun".
    //
    // Every one of them comes from PointingController::direction_for, which is
    // the routine the autopilot steers by. That is the whole point: the marker
    // the pilot lines the nose up with has to be the direction the autopilot
    // would have taken it to, and the only way to guarantee that is for them to
    // be the same function call.
    godot::Dictionary get_flight_directions() const;

    // --- navigation target (Milestone 7) ------------------------------------
    // Which body the cockpit measures distance and relative speed against.
    //
    // It used to be wired to the Moon in configure(), which was fine while the
    // Moon was the only destination and became a lie the moment the cockpit grew
    // a target selector: a display that says TARGET and cannot be pointed
    // anywhere else is a label, not an instrument.
    godot::Array get_selectable_targets() const;

    // The Solar System, as the user interface has to show it (Milestone 8 rules
    // 20 and 21): one Dictionary per body, in directory order, each naming its
    // parent and saying how far the planner has been qualified for it.
    //
    // A TREE expressed as a flat list with parents on it, rather than nested
    // Dictionaries, because the consumer is a menu and a menu walks. What must
    // not happen is the user interface keeping a body list of its own: that is
    // the second source of truth rule 19 forbids, and it is how "Mars" ends up
    // selectable in a build whose kernels cannot place it.
    godot::Array get_body_directory() const;
    bool set_target_body(const godot::String& name);
    godot::String get_target_body() const;

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
    godot::Array get_rcs_thrusters() const;
    godot::PackedFloat64Array get_rcs_throttles() const;

    // --- orbit geometry (Milestone 7) ---------------------------------------
    // The osculating ellipse, sampled. In scene units, ready to draw.
    //
    // Here and not in GDScript because it is orbital mechanics (rule 77): the
    // samples come from trajectory::state_from_elements, the same inverse the
    // campaign tool uses to turn "400 km at 51.6 degrees" into a state vector.
    // A Kepler solver in the renderer would be a second source of truth for the
    // shape of the orbit, and the two would disagree the first time the elements
    // meant anything subtle.
    //
    // An unbound orbit is sampled over the true anomalies that are actually
    // reachable rather than over the full circle, because a hyperbola has
    // asymptotes and drawing past them produces a line to nowhere.
    godot::PackedVector3Array get_orbit_track(int samples) const;

    // The same question for a celestial body about the ship's reference body,
    // and the answer comes from the EPHEMERIS rather than from elements: the
    // Moon's orbit is not an ellipse and drawing it as one would be inventing a
    // trajectory the simulation does not fly.
    godot::PackedVector3Array get_body_orbit_track(int index, int samples) const;

    // --- missions ----------------------------------------------------------
    // Plans a transfer to a body and arms it.
    //
    // Every number in the answer comes from core/navigation/mission_planner.hpp:
    // the same entry point, with the same request type, that the 365-epoch
    // campaign goes through (Milestone 6.2 sections 1-5). Nothing in the
    // GDExtension decides anything about a trajectory any more.
    //
    // NOTE what is NOT an argument: the time of flight. It is what the search
    // decides, and fixing it at 4.5 days is precisely the defect that made
    // Milestone 6's campaign fail 82 % of its epochs -- with the departure point
    // and the flight time both pinned, the transfer angle is whatever the
    // calendar says. Offering it as a cockpit dial would put the defect back
    // through the user interface.
    //
    // BLOCKING, and deliberately so: it searches departure opportunities and then
    // inverts the full model twice, which is tens of trajectory propagations and
    // takes of the order of a second. It is a one-off command, not something a
    // frame does. Returns a summary Dictionary; empty on failure, with the reason
    // in get_last_error().
    // Planning does NOT arm. Milestone 7 rule 26 puts an EXECUTE and a CANCEL in
    // front of the pilot, and a plan that is already flying by the time those
    // buttons appear makes CANCEL a lie about what just happened. The result is
    // held, shown, and installed only by arm_plan().
    //
    // Nothing about the trajectory changes: the same search, the same corrector,
    // the same numbers. What changes is when the maneuver list reaches the
    // executor.
    godot::Dictionary plan_transfer(const godot::String& target_body,
                                    double periapsis_altitude_km, double apoapsis_altitude_km,
                                    double search_hours);

    // --- planning without freezing the game (Milestone 8 rules 48, 49, 120) ---
    //
    // `plan_transfer` above blocks, and for a lunar transfer that is a defensible
    // second. An Earth-Mars search is 64 seconds, measured, and a minute of
    // frozen frames is not a keypress -- it is a crash as far as anyone watching
    // is concerned.
    //
    // So: one worker thread, started by `start_planning`, polled by
    // `get_planning_progress`, collected by `collect_plan`.
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
    bool start_planning(const godot::String& target_body, double periapsis_altitude_km,
                        double apoapsis_altitude_km, double search_hours);
    bool is_planning() const;

    // Replan ONE of the geometries the last search flew (rule 42, and step 8 of
    // the vertical slice: "select an alternative").
    //
    // `index` is into get_plan_alternatives(). The search is pinned to that
    // candidate's departure point, flight time and Lambert branch -- so it costs
    // one candidate instead of 768, a few seconds instead of a minute -- and it
    // runs through the SAME path as any other plan. A pinned geometry that
    // violates a hard constraint is still refused; this is a choice of question,
    // not a way round the answer.
    //
    // Refuses an infeasible alternative: the ones the table marks REFUSED were
    // flown and found wanting, and replanning them would spend seconds arriving
    // at the same word.
    bool start_planning_alternative(int index);

    // Counts, and the stage the search is in. Empty when nothing is running.
    godot::Dictionary get_planning_progress() const;

    // Empty until the worker has finished. Calling it once the worker is done
    // joins the thread, installs the result and returns the same summary
    // `plan_transfer` would have. Returns a Dictionary with "cancelled" set when
    // the search was stopped.
    godot::Dictionary collect_plan();

    // Asks the search to stop. It stops between candidates, never inside one.
    void cancel_planning();

    // --- the Solar System map (Milestone 8 rules 26-31) ---------------------
    //
    // A SECOND map, and a second frame, declared rather than assumed.
    //
    // The orbital map of Milestone 7 draws everything relative to the body the
    // ship is orbiting, in scene units, through the floating origin. That is the
    // right frame for a lunar transfer and the wrong one for a heliocentric one:
    // the Earth moves 12.7 million kilometres in the four days a translunar arc
    // spans, which is why get_planned_trajectory() anchors its samples at the
    // origin body -- and over two hundred days it moves 500 million.
    //
    // So this is Sun-centred J2000, in METRES, and it says so in the Dictionary
    // it returns. Metres and not scene units because the floating origin is a
    // rendering device tied to where the camera is, and a map of the Solar System
    // is not drawn from the cockpit. Everything below is in that one frame, which
    // is what rule 31 asks and what Milestone 7's first orbital map got wrong.
    //
    // Float resolution at Neptune's distance is 270 km. That is stated rather
    // than worried about: it is a map, and 270 km at 4.5e12 m is a fifth of a
    // pixel at any zoom that shows Neptune at all.
    godot::Dictionary get_system_map() const;

    // The planet tracks, sampled from the EPHEMERIS rather than drawn as
    // ellipses (rule 29). Separate from get_system_map() because they cost a
    // thousand ephemeris calls and do not change from frame to frame: the caller
    // asks once when the map opens.
    godot::Dictionary get_system_orbit_paths(int samples_per_body) const;

    // --- which body the cockpit measures against (rules 63-65) --------------
    //
    // "AP 402 km" is a lie without the body it is about, and during an
    // interplanetary cruise the right body changes three times: the Earth while
    // the ship is still near it, the Sun for two hundred days, Mars on arrival.
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
    bool set_reference_body(const godot::String& name);
    godot::String get_reference_body() const;

    // Off leaves the reference wherever it was last set, which is what the
    // Milestone 7 scene did (the Earth, forever).
    void set_auto_reference(bool enabled);
    bool get_auto_reference() const;

    // Installs the last planned transfer. False, with a reason in
    // get_last_error(), if there is nothing to arm or if the departure has
    // already passed -- flying a plan whose injection epoch is behind the ship
    // would fire the capture burn in empty space.
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
    //               plan -- every one of the corrector's few hundred probe
    //               flights integrates a quaternion and twelve thrusters.
    //
    // "finite" by default: it is what the scene has always flown and what
    // docs/validation/lunar-navigation-campaign-v2.csv qualifies, and a keypress
    // that freezes the game for several minutes is not a keypress.
    bool set_execution_model(const godot::String& model);
    godot::String get_execution_model() const;
    // The osculating orbit about the mission target, when the ship is close
    // enough to it for that to mean anything. Empty otherwise.
    //
    // The cockpit's own elements are about the REFERENCE body, which stays the
    // Earth: after a lunar insertion it correctly reports a hyperbolic escape from
    // the Earth, which is true and useless. What a pilot in lunar orbit wants is
    // the orbit they are in.
    godot::Dictionary get_orbit_about_target() const;
    bool has_plan() const;
    void clear_plan();
    godot::Dictionary get_plan() const;

    // The geometries the search actually flew, each with the orbit it would have
    // arrived in (section 13). Reported so that the inclination the mission ends
    // up with is a visible consequence of a choice rather than a surprise; the
    // planner does not steer towards an inclination and this is how it says so.
    godot::Array get_plan_alternatives() const;

    // Section 16: which of the mission states the flight is in. A STRING, and
    // the UI's only job is to print it -- the classification is
    // core/navigation/mission_execution.hpp's, because it depends on where the
    // sphere of influence is and where the burns sit.
    godot::String get_mission_phase() const;

    // Section 17: predicted against actual, for the quantities that say whether
    // the simulator predicts its own physics. Empty until the orbit has settled.
    godot::Dictionary get_mission_outcome() const;

    // The burns of the armed plan: label, epochs, delta-v, and where the ship
    // will be when each one lights, in scene units. The orbital map draws a
    // marker per entry; nothing here is recomputed by the renderer.
    godot::Array get_maneuvers() const;

    // The planned arc, in scene units, for drawing. One entry per sample,
    // already through RenderTransform.
    godot::PackedVector3Array get_planned_trajectory() const;

    // Everything else, as a Dictionary: the cockpit reads this once per frame
    // instead of making twenty calls.
    godot::Dictionary get_snapshot() const;

    [[nodiscard]] bool is_ready() const { return builder_ != nullptr; }
    godot::String get_last_error() const;

protected:
    static void _bind_methods();

private:
    void rebuild_snapshot();
    [[nodiscard]] sf::math::Vec3 optics_observer_velocity() const;

    std::shared_ptr<const sf::ephemeris::SpiceKernelSet> kernels_;
    std::unique_ptr<sf::ephemeris::SpiceEphemerisProvider> provider_;
    std::unique_ptr<sf::ephemeris::SpiceTimeConverter> time_converter_;
    std::unique_ptr<sf::celestial::BodyCatalog> catalog_;
    // The directory: what EXISTS, as opposed to whose mass is in the force
    // model. Resolved once at configure() against the loaded kernels, because
    // availability is a property of those and not of the code.
    std::unique_ptr<sf::celestial::SolarSystem> system_;
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
    // Both live for the life of the node, and that is load-bearing: the force
    // model holds a REFERENCE to the executor and the executor holds one to the
    // plan (gravity::CompositeForceModel::add_reference). Rebuilding either when
    // a plan is made would leave the force model pointing at freed memory.
    // Planning replaces the plan's CONTENTS instead.
    std::unique_ptr<sf::navigation::ManeuverPlan> plan_;
    std::unique_ptr<sf::navigation::ManeuverExecutor> executor_;

    // The last plan, whole. Kept rather than flattened into the Dictionary
    // because sections 15 and 17 want it more than once and from more than one
    // angle -- the cockpit readout, the alternatives list, the predicted half of
    // the predicted-versus-actual table -- and re-deriving any of those from a
    // Dictionary would be the same mistake in a smaller costume.
    sf::navigation::MissionPlanResult planned_{};
    sf::navigation::ExecutionModel execution_{sf::navigation::ExecutionModel::FiniteBurn};
    sf::navigation::MissionExecution mission_{};

    // The last value returned by plan_transfer, kept only so that a caller that
    // held on to it sees the same thing get_plan() would build. Never a second
    // source of truth: it is assigned FROM get_plan() and nowhere else.
    godot::Dictionary plan_summary_;

    sf::propagation::PropagationState state_{};
    bool apparent_positions_{true};
    double visual_test_beta_{-1.0};
    sf::simulation::SimulationSnapshot snapshot_{};
    sf::render::RenderTransform transform_{1.0e-6};

    std::string last_error_;

    // --- the planning worker ------------------------------------------------
    struct PlanningJob {
        std::thread worker;
        std::atomic<bool> running{false};
        std::atomic<bool> cancel{false};

        std::mutex mutex;
        sf::navigation::SearchProgress progress{};
        sf::navigation::MissionPlanResult result{};
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
        sf::celestial::BodyId target{};
        double periapsis_altitude_km{0.0};
        double apoapsis_altitude_km{0.0};
        double search_hours{0.0};
    };
    LastRequest last_request_{};
    bool auto_reference_{true};

    [[nodiscard]] sf::celestial::BodyId natural_reference() const;

    void join_worker();

    // The one body of start_planning() and start_planning_alternative(): the
    // only difference between them is the pin.
    bool begin_planning(sf::celestial::BodyId target, double periapsis_altitude_km,
                        double apoapsis_altitude_km, double search_hours,
                        const sf::navigation::TransferConfig::PinnedDeparture& pin);
};

}  // namespace spaceflight_godot
