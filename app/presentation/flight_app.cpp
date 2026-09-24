#include "app/presentation/flight_app.hpp"

#include "app/presentation/format.hpp"
#include "app/presentation/headless_driver.hpp"
#include "app/presentation/shot_director.hpp"
#include "app/presentation/ui/debug_hud.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>

namespace sf::app {
namespace {

Vec3 to_vec(const RenderVec3& v) { return widen(v); }

// The fifteen phase names core/navigation/mission_execution.hpp produces, in
// pilot's language. The list is COMPLETE on purpose and was taken from the enum,
// not from memory: the first version covered "INJECTION" and "CAPTURE", which do
// not exist -- the core says INJECTION_BURN and CAPTURE_BURN -- and the cockpit
// announced the injection burn with an underscore in the middle. The last branch
// returns the raw name, which is the right answer for a phase this file does not
// know: better a technical name than an invented sentence.
std::string phase_message(const std::string& phase) {
    if (phase == "PLANNED") return "TRANSFER PLANNED";
    if (phase == "WAITING_FOR_DEPARTURE") return "AWAITING DEPARTURE";
    if (phase == "ORIENTING") return "ORIENTING FOR INJECTION";
    if (phase == "INJECTION_BURN") return "INJECTION BURN";
    if (phase == "MIDCOURSE_CORRECTION") return "MIDCOURSE CORRECTION";
    if (phase == "COAST") return "COAST PHASE";
    if (phase == "APPROACH") return "TARGET APPROACH";
    if (phase == "CAPTURE_ORIENTING") return "ORIENTING FOR CAPTURE";
    if (phase == "CAPTURE_BURN") return "CAPTURE BURN";
    if (phase == "ORBIT_INSERTION") return "ORBIT INSERTION";
    if (phase == "COMPLETE") return "ORBIT ACHIEVED";
    if (phase == "ABORTED") return "MISSION ABORTED";
    if (phase == "FAILED") return "MISSION FAILED";
    return phase;
}


}  // namespace

FlightApp::FlightApp(AudioSink& audio) : audio_(audio) {}

FlightApp::~FlightApp() = default;

void FlightApp::post(const std::string& text, MessageLevel level) { messages_.post(text, level); }

// =============================================================================
//  construction
// =============================================================================

bool FlightApp::initialise(const FlightConfig& config) {
    config_ = config;
    session_.set_diagnostic_sink([](const std::string& message, bool is_error) {
        std::fprintf(stderr, "%s: %s\n", is_error ? "ERROR" : "WARNING", message.c_str());
    });

    if (!session_.configure(config.kernel_directory, config.epoch_utc)) {
        error_ = "Could not load SPICE kernels from " + config.kernel_directory +
                 " -- run scripts/fetch_kernels.sh. " + session_.last_error();
        return false;
    }
    if (!session_.start_circular_orbit(config.altitude_m, config.inclination_deg)) {
        error_ = session_.last_error();
        return false;
    }
    // Nose on prograde, planet under the floor -- the attitude a parking orbit is
    // actually flown in. Without this the first frame of a new flight shows empty
    // sky: the scenario's starting attitude is the identity, and in it the nose
    // points at the zenith (rule 66).
    session_.align_attitude_to_flight(config.nadir_bias_deg);

    session_.set_render_scale(RENDER_SCALE);
    session_.set_body_scale_exaggeration(BODY_SCALES[static_cast<std::size_t>(body_scale_index_)]);
    controls_ = std::make_unique<FlightControls>(session_);
    session_.set_time_warp(controls_->warp());

    // The catalogue lives next to the kernels, outside the application, for the
    // same reason: it is obtained data (catalogs/MANIFEST.md).
    sky_.set_magnitude_limit(MAGNITUDE_LIMIT);
    sky_.set_half_saturation(exposure());
    if (!sky_.load_catalogue(config.catalogue_path)) {
        // Not fatal: the sky goes dark and says why. The dynamics do not change.
        std::fprintf(stderr, "WARNING: No star catalogue -- run scripts/fetch_star_catalog.sh. %s\n",
                     sky_.last_error().c_str());
    }

    build_scene();
    wire_controls();

    rcs_thruster_count_ = static_cast<int>(session_.rcs_thrusters().size());
    rcs_visual_->build(session_.rcs_thrusters());

    if (config.headless) {
        headless_ = std::make_unique<HeadlessDriver>(*this, config.headless_destination);
    }

    // Rule 66: the screen does not start black nor on a debug page. The first
    // thing that exists is the cockpit, with the Earth outside.
    post("NEW FLIGHT — EARTH ORBIT", MessageLevel::Mission);
    post(input::label("help") + " for controls   " + input::label("nav_panel") + " for the mission computer",
         MessageLevel::Info);
    return true;
}

void FlightApp::build_scene() {
    celestial_.build(session_);
    spacecraft_ = std::make_unique<SpacecraftVisual>(meshes_, materials_);
    plume_ = std::make_unique<EnginePlume>(meshes_);
    rcs_visual_ = std::make_unique<RcsVisual>(meshes_);
    cockpit_ = std::make_unique<CockpitInterior>(meshes_, materials_);
    camera_.set_mode(CameraRig::Mode::Cockpit);
    camera_.on_capture_changed = [this](bool captured) {
        if (on_mouse_capture && !config_.headless) {
            on_mouse_capture(captured);
        }
    };
}

void FlightApp::wire_controls() {
    controls_->on_message = [this](const std::string& text, MessageLevel level) {
        post(text, level);
        if (level == MessageLevel::Warning || level == MessageLevel::Critical) {
            audio_.warning();
        }
    };
    controls_->on_rcs_fired = [this] { audio_.rcs_fired(rcs_firing_count()); };
    controls_->on_engine_changed = [this](bool running) {
        post(std::string{"MAIN ENGINE "} + (running ? "IGNITION" : "CUT-OFF"), MessageLevel::Info);
    };

    mission_panel_.on_search_cancelled = [this] { cancel_planning(); };
    mission_panel_.on_alternative_chosen = [this](int index) { choose_alternative(index); };
    mission_panel_.on_plan_requested = [this](const std::string& target, double pe, double ap) {
        plan_mission(target, pe, ap);
    };
    mission_panel_.on_execute = [this] { arm_mission(); };
    mission_panel_.on_cancel = [this] { abort_mission(); };
    mission_panel_.on_target_changed = [this](const std::string& target) { set_target(target); };
    mission_panel_.on_closed = [this] { show_panel(Panel::Mission, false); };
    mission_panel_.on_point_requested = [this](const std::string& mode) { controls_->point(mode); };

    mission_panel_.set_targets(session_.selectable_targets(), session_.target_body());

    // The seven physical buttons on the panel (rule 24). Each calls exactly the
    // same path as the corresponding key -- there is no second command hidden
    // behind the button, which is why the mouse and the keyboard can never
    // disagree about the ship's state.
    auto& cockpit = *cockpit_;
    cockpit.configure_button(0, "ENGINE", [this](CockpitControl&) {
        audio_.button_pressed();
        controls_->set_throttle(controls_->throttle() > 0.0 ? 0.0 : 1.0);
    });
    // Kept rather than looked up by index at the call site: `controls[1]` works
    // until someone adds a button to its left, and on that day the V key starts
    // lighting the wrong lamp without anything complaining.
    rcs_switch_ = 1;
    cockpit.configure_button(
        1, "RCS",
        [this](CockpitControl& c) {
            audio_.switch_flipped();
            controls_->toggle_rcs();
            c.set_on(controls_->rcs_enabled());
        },
        true, controls_->rcs_enabled());
    cockpit.configure_button(2, "AP", [this](CockpitControl&) {
        audio_.button_pressed();
        controls_->point("prograde");
    });
    cockpit.configure_button(3, "NAV", [this](CockpitControl&) {
        audio_.button_pressed();
        toggle_panel(Panel::Mission);
    });
    cockpit.configure_button(4, "MAP", [this](CockpitControl&) {
        audio_.button_pressed();
        toggle_panel(Panel::Map);
    });
    cockpit.configure_button(5, "WARP", [this](CockpitControl&) {
        audio_.button_pressed();
        controls_->step_warp(1);
    });
    cockpit.configure_button(6, "MODE", [this](CockpitControl&) {
        audio_.button_pressed();
        controls_->cycle_engine_mode();
    });
}

void FlightApp::set_shot_director(std::unique_ptr<ShotDirector> director) { shots_ = std::move(director); }

// =============================================================================
//  frame
// =============================================================================

void FlightApp::frame(double delta, const KeyboardState& keyboard) {
    if (!session_.is_ready()) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();

    controls_->apply(delta, keyboard);
    camera_keys(delta, keyboard);

    // The frame rate decides HOW MUCH coordinate time to ask for. It never
    // reaches the integrator, which picks its own steps (rule 21).
    if (!controls_->paused()) {
        session_.advance(delta);
    }

    // Floating origin: re-centre on what is being looked at, every frame.
    if (camera_.focus_index < 0) {
        session_.focus_on_spacecraft();
    } else {
        session_.focus_on_body(camera_.focus_index);
    }

    ship_position_ = to_vec(session_.spacecraft_position());
    const auto axes = session_.spacecraft_axes();
    ship_basis_ = Basis{axes.x, axes.y, axes.z};
    directions_ = session_.flight_directions();
    rcs_throttles_ = session_.rcs_throttles();

    place_camera();
    update_world(delta);
    update_ship(delta);
    update_tracks(delta);
    update_instruments();
    update_audio();
    mission_panel_.advance();
    poll_planning();
    search_was_running_ = session_.is_planning();
    watch_mission();
    messages_.advance(delta);
    audio_.advance(delta);

    // Without a window nobody presses a key: the headless run flies itself and
    // prints the COMPLETE read-out, whatever the HUD mode.
    if (headless_ != nullptr) {
        headless_->drive(debug_hud::hud_lines(*this, false));
    }
    if (shots_ != nullptr) {
        shots_->step(delta);
    }
    if (!capture_dir_.empty()) {
        capture_step();
    }

    const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    frame_ms_ = spent * 1000.0;
    if (delta > 0.0) {
        fps_ = fps_ <= 0.0 ? 1.0 / delta : fps_ + (1.0 / delta - fps_) * 0.05;
    }
}

void FlightApp::place_camera() {
    Vec3 focus_position = ship_position_;
    double focus_natural = 1.0;
    if (camera_.focus_index >= 0) {
        focus_position = to_vec(session_.body_position(camera_.focus_index));
        focus_natural = std::max(session_.body_radius(camera_.focus_index) * 3.0, 1.0);
    }
    const Vec3 to_target = directions_.target.value_or(Vec3{});
    camera_.update(ship_position_, ship_basis_, session_.beta_vector(), to_target, focus_position, focus_natural);
}

void FlightApp::update_world(double delta) {
    celestial_.exposure = exposure();
    celestial_.advance_clouds(delta);
    bodies_ = celestial_.update(session_, camera_.world_camera().position);
    starfield_.effect_aberration = celestial_.effect_aberration;
    starfield_.effect_doppler = celestial_.effect_doppler;
    starfield_.effect_beaming = celestial_.effect_beaming;
    // The only physics that passes through here: a vector, carried.
    sky_.update(session_.beta_vector(), StarfieldView::SKY_RADIUS, starfield_.effect_aberration,
                starfield_.effect_doppler, starfield_.effect_beaming);
}

void FlightApp::update_ship(double delta) {
    // The hull points where the attitude says it points -- not along the
    // velocity, which is what a simulator without attitude has to pretend.
    const auto s = session_.snapshot();
    plume_->set_mode(s.exhaust_velocity_c, s.max_thrust_n);
    plume_->set_thrust(s.thrust_n);
    plume_->advance(delta);
    rcs_visual_->set_throttles(rcs_throttles_);
    spacecraft_->advance(delta);

    const bool inside = camera_.is_cockpit() && camera_.focus_index < 0;
    cockpit_->visible = inside;
    cockpit_->displays_visible = inside;
    cockpit_->set_indicator("RCS", rcs_firing_count() > 0);
    cockpit_->set_indicator("ENG", s.thrust_n > 0.0);
    cockpit_->set_indicator("AP", session_.has_plan());
    cockpit_->set_indicator("MSTR", master_caution());
    cockpit_->advance(delta);
}

void FlightApp::update_tracks(double delta) {
    // The trajectories are resampled four times a second and not every frame.
    //
    // The cost is the reason and it is concrete: the orbit is 192 calls to
    // state_from_elements; the target's path is 256 calls to the EPHEMERIS; the
    // planned arc is one more ephemeris call PER SAMPLE; and the burn markers
    // sweep that arc again. That is of the order of a thousand SPICE lookups, and
    // doing them sixty times a second cost more than the whole scene.
    track_timer_ -= delta;
    if (track_timer_ > 0.0) {
        return;
    }
    track_timer_ = 0.25;

    // While the search runs, the frame gives way to the ephemeris.
    //
    // ⚠️ The planner runs on a thread and CSPICE has a global mutex: every lookup
    // this frame makes is a lookup the worker waits for. And this block is the
    // scene's largest consumer. What is lost is a map that stands still during
    // the search -- and the plan it is going to draw does not exist yet.
    if (session_.is_planning()) {
        return;
    }
    // The solar-system map is only rebuilt while it is open: a few hundred more
    // ephemeris lookups, and paying for them with the map closed would be paying
    // for a drawing nobody sees.
    if (map_visible_ && orbit_map_.mode == OrbitMap::Mode::System) {
        orbit_map_.system = session_.system_map();
    }
    orbit_track_.clear();
    for (const auto& p : session_.orbit_track(ORBIT_TRACK_SAMPLES)) {
        orbit_track_.push_back(to_vec(p));
    }
    target_track_.clear();
    if (const int target = target_index(); target >= 0) {
        for (const auto& p : session_.body_orbit_track(target, TARGET_TRACK_SAMPLES)) {
            target_track_.push_back(to_vec(p));
        }
    }
    planned_track_.clear();
    for (const auto& p : session_.planned_trajectory()) {
        planned_track_.push_back(to_vec(p));
    }
    maneuvers_ = session_.maneuvers();
}

void FlightApp::update_instruments() {
    // One record per frame, shared by the six instruments. One per instrument
    // would be tidier and wrong: two displays that built their own data could
    // build it at different instants, and the cockpit would show two readings of
    // the same ship.
    const auto s = session_.snapshot();
    if (!s.valid) {
        data_ = InstrumentData{};
        return;
    }
    InstrumentData data{};
    data.present = true;
    data.s = s;
    const int reference = celestial_.index_of(s.reference);
    const int target = target_index();
    const double reference_radius = reference >= 0 ? session_.body_radius(reference) : 0.0;

    data.directions = directions_;
    data.ship_basis = ship_basis_;
    data.ship_position = ship_position_;
    data.reference_position = reference >= 0 ? to_vec(session_.body_position(reference)) : Vec3{};
    data.reference_radius = reference_radius;
    data.reference_colour = CelestialView::colour_for(s.reference);
    data.orbit_track = orbit_track_;
    data.target_track = target_track_;
    data.planned_trajectory = planned_track_;
    data.maneuvers = maneuvers_;
    data.render_scale = RENDER_SCALE;
    data.has_target = target >= 0;
    data.target_position = target >= 0 ? to_vec(session_.body_position(target)) : Vec3{};
    data.target_radius = target >= 0 ? session_.body_radius(target) : 0.0;

    // Altitudes and not radii: a pilot reads altitude. The reference body's
    // radius comes from the body's own snapshot, so the subtraction is between
    // two numbers of the same instant.
    double radius_m = 0.0;
    if (reference >= 0) {
        radius_m = reference_radius / RENDER_SCALE / BODY_SCALES[static_cast<std::size_t>(body_scale_index_)];
    }
    data.apoapsis_altitude_m = s.apoapsis_m - radius_m;
    data.periapsis_altitude_m = s.periapsis_m - radius_m;
    data.bound = s.eccentricity < 1.0;
    data.retrograde_orbit = s.inclination_deg > 90.0;

    data.propellant_capacity_kg = 19000.0;
    data.engine_armed = true;
    data.rcs_enabled = controls_->rcs_enabled();
    data.rcs_activity = controls_->rcs_activity(rcs_firing_count());
    data.rcs_throttles = rcs_throttles_;
    data.rcs_firing = rcs_firing_count();
    data.rcs_thrusters = rcs_thruster_count_;
    data.camera_mode = camera_.mode_name();
    data.cockpit_view = camera_.is_cockpit() && camera_.focus_index < 0;
    data.paused = controls_->paused();

    // ⚠️ PROPER acceleration, which is what an accelerometer on board would read:
    // only the NON-gravitational forces over the mass. In free fall it is zero.
    // NOT the snapshot's acceleration, which is the full model's coordinate
    // acceleration and includes gravity -- 8.7 m/s^2 in a 400 km orbit. Labelling
    // that "proper acceleration" would be saying the crew in orbit feels almost
    // a g, which is exactly the opposite of what happens.
    data.proper_acceleration_ms2 = session_.proper_acceleration_body().norm();

    // Closing with the target: the rate at which the distance falls. The
    // projection of the relative velocity onto the line of sight.
    double closing = 0.0;
    if (target >= 0 && directions_.target.has_value()) {
        closing = s.target_relative_speed_ms * closing_sign(target);
    }
    data.closing_speed_ms = closing;

    data.mission_phase = session_.mission_phase();
    const auto next = next_event();
    data.next_event = next.first;
    // The countdown comes from the PLAN and not from the cached list: a clock
    // that only moves four times a second visibly jumps in the last ten seconds,
    // which is exactly when someone is looking at it.
    const bool injection_next = !next.first.empty() && !maneuvers_.empty() &&
                                session_.snapshot().time_tdb_s < maneuvers_.front().cutoff_tdb_s;
    data.next_event_seconds = injection_next ? session_.plan().seconds_to_ignition : next.second;
    if (session_.has_plan()) {
        data.plan_seconds_to_arrival = session_.plan().seconds_to_insertion;
    }
    data_ = std::move(data);
}

double FlightApp::closing_sign(int target) const {
    // +1 closing, -1 opening. The sign comes from the dot product between the
    // relative velocity and the line of sight, both taken from the SAME snapshot.
    const Vec3 to_target = *directions_.target;
    const Vec3 relative = to_vec(session_.body_position(target)) - ship_position_;
    if (relative.norm() < 1.0e-9) {
        return 0.0;
    }
    // The body's velocity RELATIVE to the observer; closing means it points
    // against the line of sight.
    const Vec3 relative_velocity = to_vec(session_.body_relative_velocity_scene(target));
    const double projection = dot(relative_velocity, to_target.normalized());
    return projection > 0.0 ? -1.0 : (projection < 0.0 ? 1.0 : 0.0);
}

std::pair<std::string, double> FlightApp::next_event() const {
    if (!session_.has_plan()) {
        return {"", 0.0};
    }
    // Against the clock of THIS frame, not the cached `done`: the list is
    // refreshed four times a (frame) second, and at 1e7x a quarter of a second is
    // twenty-nine days -- the M8 cruise was photographed announcing an injection
    // a week past instead of the insertion ahead.
    const double now = session_.snapshot().time_tdb_s;
    for (const auto& burn : maneuvers_) {
        if (now < burn.cutoff_tdb_s) {
            return {fmt::upper(burn.name.empty() ? "BURN" : burn.name), burn.ignition_tdb_s - now};
        }
    }
    return {"", 0.0};
}

void FlightApp::update_audio() {
    const auto& s = session_.snapshot();
    audio_.set_interior(camera_.is_cockpit() && camera_.focus_index < 0);
    audio_.set_engine(s.engine_mode, s.thrust_n, s.max_thrust_n);
    audio_.set_rcs_activity(rcs_firing_count());
}

void FlightApp::watch_mission() {
    // Rule 67: the mission messages. The PHASE comes from the core and this block
    // only notices that it changed -- the classification is not done here,
    // because it depends on where the sphere of influence is and where the burns
    // are.
    const auto phase = session_.mission_phase();
    if (phase != last_phase_) {
        last_phase_ = phase;
        if (!phase.empty() && phase != "IDLE") {
            post(phase_message(phase), MessageLevel::Mission);
            audio_.notify();
        }
    }
    const bool low = session_.snapshot().propellant_kg < 1900.0;
    if (low && !warned_low_propellant_) {
        warned_low_propellant_ = true;
        post("PROPELLANT BELOW 10 %", MessageLevel::Warning);
        audio_.warning();
    } else if (!low) {
        warned_low_propellant_ = false;
    }
}

bool FlightApp::master_caution() const {
    const auto s = session_.snapshot();
    return s.propellant_kg < 1900.0 || s.rotation_rate_deg_s > 6.0;
}

// =============================================================================
//  mission
// =============================================================================

bool FlightApp::plan_mission(const std::string& target, double periapsis_km, double apoapsis_km) {
    // Starts the search. It runs on a thread and the game keeps going.
    //
    // ⚠️ Milestone 7 did this blocking and wrote here that a second of frozen
    // frame was an acceptable cost. It was, for the Moon. An Earth-Mars search
    // takes 64 seconds measured, and a minute of frozen frames is not a keypress
    // -- it is a crash, as far as anyone watching is concerned.
    if (!session_.is_ready()) {
        return false;
    }
    if (session_.is_planning()) {
        post("ALREADY SEARCHING — cancel first", MessageLevel::Warning);
        return false;
    }
    std::string wanted = !target.empty() ? target : session_.target_body();
    if (wanted.empty()) {
        wanted = "Moon";
    }
    if (!session_.start_planning(wanted, periapsis_km, apoapsis_km, MISSION_SEARCH_HOURS)) {
        post(session_.last_error(), MessageLevel::Warning);
        return false;
    }
    std::printf("[mission] searching for a transfer to %s\n", wanted.c_str());
    std::fflush(stdout);
    post("SEARCHING TRAJECTORIES TO " + fmt::upper(wanted), MessageLevel::Mission);
    return true;
}

void FlightApp::set_map_mode(OrbitMap::Mode mode) {
    // Sets the map's mode instead of toggling it. cycle_map_mode() is what the
    // key does and describes a TRANSITION; this describes a STATE, which is what
    // a script or a test needs.
    if (orbit_map_.mode == mode) {
        return;
    }
    cycle_map_mode();
}

void FlightApp::cycle_map_mode() {
    // Toggles LOCAL / SOLAR SYSTEM (rule 26). Opens the map if it is closed:
    // asking for the mode of an invisible display and seeing nothing happen is a
    // key that looks broken.
    if (!map_visible_) {
        toggle_panel(Panel::Map);
    }
    orbit_map_.mode = orbit_map_.mode == OrbitMap::Mode::Local ? OrbitMap::Mode::System : OrbitMap::Mode::Local;
    orbit_map_.zoom = 1.0;
    if (orbit_map_.mode == OrbitMap::Mode::System) {
        // The planetary paths, once. 96 samples per body: rule 29 asks explicitly
        // not to generate thousands of points, and an ellipse drawn with 96
        // segments is indistinguishable from one with 960 at any zoom that shows
        // the whole planet.
        orbit_map_.system_paths = session_.system_orbit_paths(96);
        orbit_map_.system = session_.system_map();
        post("SOLAR SYSTEM MAP", MessageLevel::Info);
    } else {
        post("LOCAL MAP", MessageLevel::Info);
    }
}

void FlightApp::choose_alternative(int index) {
    // Replans ONE of the geometries the search flew, pinned (rule 42). Through
    // the same path as any other plan -- the same thread, the same progress, the
    // same collect_plan() -- and it costs one candidate instead of 768.
    if (!session_.is_ready() || session_.is_planning()) {
        return;
    }
    if (!session_.start_planning_alternative(index)) {
        post(session_.last_error(), MessageLevel::Warning);
        return;
    }
    post("REPLANNING THE CHOSEN TRAJECTORY", MessageLevel::Mission);
}

void FlightApp::cancel_planning() {
    // Rule 120: a long search has to be interruptible, and the worker has to
    // actually stop -- not be left running.
    if (!session_.is_planning()) {
        return;
    }
    session_.cancel_planning();
    post("CANCELLING SEARCH", MessageLevel::Warning);
}

void FlightApp::poll_planning() {
    // Every frame. While the search runs, it feeds the panel with counts; when it
    // ends, it collects the result -- on the FRAME, because the frame is what may
    // install a plan.
    if (session_.is_planning()) {
        mission_panel_.show_progress(session_.planning_progress());
        return;
    }
    if (!search_was_running_) {
        return;
    }
    const auto plan = session_.collect_plan();
    mission_panel_.show_plan(plan, session_.last_error());
    if (plan.cancelled) {
        post("SEARCH CANCELLED", MessageLevel::Warning);
        return;
    }
    if (!plan.valid) {
        post("NO TRANSFER FOUND — " + session_.last_error(), MessageLevel::Warning);
        std::fprintf(stderr, "WARNING: could not plan the transfer: %s\n", session_.last_error().c_str());
        return;
    }
    mission_panel_.show_alternatives(session_.plan_alternatives());
    post(fmt::format("TRANSFER PLANNED — %.0f m/s, ", plan.total_delta_v) +
             fmt::duration(plan.time_of_flight_days * 86400.0),
         MessageLevel::Mission);
    audio_.notify();
    print_plan(plan);
}

void FlightApp::print_plan(const PlanSummary& plan) const {
    std::printf("[mission] %d burns: injection %.1f m/s in %s, insertion %.1f m/s\n", plan.burns,
                plan.injection_delta_v, fmt::duration(plan.seconds_to_ignition).c_str(), plan.insertion_delta_v);
    std::printf("[mission] %s branch, tof %.2f d, transfer angle %.1f deg, total %.1f m/s of %.0f available\n",
                plan.branch.empty() ? "?" : plan.branch.c_str(), plan.time_of_flight_days, plan.transfer_angle_deg,
                plan.total_delta_v, plan.delta_v_available);
    std::printf("[mission] predicted orbit %.1f x %.1f km, e %.4f, i %.2f deg, RAAN %.1f deg\n",
                plan.predicted_periapsis_m / 1000.0, plan.predicted_apoapsis_m / 1000.0,
                plan.predicted_eccentricity, plan.predicted_inclination_deg, plan.predicted_raan_deg);
    // Section 13 of M6.2: the alternatives the search actually flew, each with
    // the orbit it would have arrived in. Printed rather than hidden, because the
    // planner does not aim at an inclination and the spread is the evidence.
    for (const auto& alternative : session_.plan_alternatives()) {
        std::printf("[mission]   alt %-28s %s  %.2f d  %.0f m/s  ->  %.0f x %.0f km, e %.4f, i %.1f deg\n",
                    alternative.label.c_str(), alternative.feasible ? "ok     " : alternative.failure.c_str(),
                    alternative.time_of_flight_days, alternative.total_delta_v,
                    alternative.predicted_periapsis_m / 1000.0, alternative.predicted_apoapsis_m / 1000.0,
                    alternative.predicted_eccentricity, alternative.predicted_inclination_deg);
    }
    std::fflush(stdout);
}

bool FlightApp::arm_mission() {
    // The message is written here and not relayed from the core when the case is
    // "no plan": `arm_plan: no transfer has been planned` is a diagnostic for
    // whoever reads a log, and the pilot wants to know what to do next.
    if (!session_.has_planned_transfer()) {
        post("NO PLAN TO EXECUTE — " + input::label("mission_plan") + " to plan a transfer",
             MessageLevel::Warning);
        return false;
    }
    if (!session_.arm_plan()) {
        post(session_.last_error(), MessageLevel::Warning);
        return false;
    }
    // The throttle is the pilot's and the plan is the computer's; both pushing at
    // once is how a corrected trajectory stops being corrected.
    controls_->set_throttle(0.0);
    mission_panel_.show_plan(session_.plan(), "");
    post("MISSION PLAN ACCEPTED", MessageLevel::Mission);
    audio_.notify();
    return true;
}

void FlightApp::abort_mission() {
    // Rule 68: cancels the autopilot and the plan. The ship does NOT magically
    // go back to the Earth -- it stays exactly in the physical state it is in, and
    // control goes back to the pilot.
    if (!session_.has_plan() && !session_.has_planned_transfer()) {
        return;
    }
    session_.clear_plan();
    session_.set_pointing_mode("");
    mission_panel_.show_plan(PlanSummary{}, "aborted by the pilot");
    post("MISSION ABORTED — manual control", MessageLevel::Warning);
    audio_.warning();
    std::printf("[mission] plan abandoned\n");
    std::fflush(stdout);
}

void FlightApp::set_target(const std::string& name) {
    if (!session_.set_target_body(name)) {
        post(session_.last_error(), MessageLevel::Warning);
        return;
    }
    track_timer_ = 0.0;
    post("TARGET " + fmt::upper(name), MessageLevel::Info);
}

int FlightApp::target_index() const {
    const auto name = session_.target_body();
    return name.empty() ? -1 : celestial_.index_of(name);
}

// =============================================================================
//  input
// =============================================================================

void FlightApp::camera_keys(double delta, const KeyboardState& keyboard) {
    // The arrows move the camera; WASD is the ship's. Two families of keys for
    // two things that are not to be confused -- turning the SHIP burns
    // propellant, turning the CAMERA does not change a single number of the
    // state.
    const double yaw = (keyboard.is_down(Key::Left) ? 1.0 : 0.0) - (keyboard.is_down(Key::Right) ? 1.0 : 0.0);
    const double pitch = (keyboard.is_down(Key::Up) ? 1.0 : 0.0) - (keyboard.is_down(Key::Down) ? 1.0 : 0.0);
    if (yaw != 0.0 || pitch != 0.0) {
        camera_.apply_keys(delta, yaw, pitch, input::held(keyboard, "camera_free_look"));
    }
}

void FlightApp::key_event(const KeyEvent& event) {
    if (!event.pressed || event.echo) {
        return;
    }
    // A command with Shift or Ctrl is a chord, and its modifier is not a throttle
    // request. Said once, here, instead of in every branch.
    if (event.shift || event.ctrl) {
        controls_->block_throttle_trim();
    }
    const auto pressed = [&](const char* action) { return input::pressed_exact(event, action); };

    // --- pointing ---
    if (pressed("point_prograde")) {
        controls_->point("prograde");
    } else if (pressed("point_retrograde")) {
        controls_->point("retrograde");
    } else if (pressed("point_normal")) {
        controls_->point("normal");
    } else if (pressed("point_anti_normal")) {
        controls_->point("anti_normal");
    } else if (pressed("point_radial_out")) {
        controls_->point("radial_out");
    } else if (pressed("point_radial_in")) {
        controls_->point("radial_in");
    } else if (pressed("point_hold")) {
        controls_->point("");
    } else if (pressed("point_target")) {
        point_at_target(false);
    } else if (pressed("point_anti_target")) {
        point_at_target(true);
    }
    // --- engine ---
    else if (pressed("throttle_full")) {
        controls_->set_throttle(1.0);
    } else if (pressed("engine_cutoff")) {
        controls_->set_throttle(0.0);
    } else if (pressed("engine_mode")) {
        controls_->cycle_engine_mode();
    } else if (pressed("rcs_toggle")) {
        controls_->toggle_rcs();
        cockpit_->controls()[rcs_switch_].set_on(controls_->rcs_enabled());
        audio_.switch_flipped();
    } else if (pressed("rcs_mode")) {
        post("RCS " + controls_->rcs_activity(rcs_firing_count()), MessageLevel::Info);
    }
    // --- camera ---
    else if (pressed("camera_cycle")) {
        camera_.cycle_mode();
        camera_.set_mouse_look(false);
        post("CAMERA " + camera_.mode_name(), MessageLevel::Info);
    } else if (pressed("camera_recentre")) {
        camera_.recentre();
    } else if (pressed("camera_zoom_in")) {
        camera_.zoom(-1.0);
    } else if (pressed("camera_zoom_out")) {
        camera_.zoom(1.0);
    } else if (pressed("camera_focus_next")) {
        cycle_focus();
    }
    // --- time ---
    else if (pressed("warp_up")) {
        controls_->step_warp(1);
    } else if (pressed("warp_down")) {
        controls_->step_warp(-1);
    } else if (pressed("pause")) {
        set_paused(!controls_->paused());
    } else if (pressed("menu")) {
        escape();
    }
    // --- mission ---
    else if (pressed("target_next")) {
        mission_panel_.step_target(1);
    } else if (pressed("target_prev")) {
        mission_panel_.step_target(-1);
    } else if (pressed("mission_plan")) {
        plan_mission();
    } else if (pressed("mission_execute")) {
        arm_mission();
    } else if (pressed("mission_abort")) {
        abort_mission();
    } else if (pressed("nav_panel")) {
        toggle_panel(Panel::Mission);
    } else if (pressed("map_mode")) {
        cycle_map_mode();
    } else if (pressed("orbit_map")) {
        toggle_panel(Panel::Map);
    }
    // --- interface ---
    else if (pressed("hud_cycle")) {
        hud_mode_ = (hud_mode_ + 1) % static_cast<int>(HUD_MODES.size());
        post("HUD " + fmt::upper(HUD_MODES[static_cast<std::size_t>(hud_mode_)]), MessageLevel::Info);
    } else if (pressed("debug_hud")) {
        debug_visible_ = !debug_visible_;
    } else if (pressed("help")) {
        toggle_panel(Panel::Help);
    } else if (pressed("exposure_up")) {
        exposure_index_ = std::min(exposure_index_ + 1, static_cast<int>(EXPOSURE_LEVELS.size()) - 1);
        sky_.set_half_saturation(exposure());
    } else if (pressed("exposure_down")) {
        exposure_index_ = std::max(exposure_index_ - 1, 0);
        sky_.set_half_saturation(exposure());
    }
    // --- technical ---
    else if (pressed("restart_orbit")) {
        session_.start_circular_orbit(config_.altitude_m, config_.inclination_deg);
        post("SCENARIO RESET — 400 km, 51.6°", MessageLevel::Info);
    } else if (pressed("execution_model")) {
        cycle_execution_model();
    } else if (pressed("visual_beta")) {
        set_visual_beta_index((visual_beta_index_ + 1) % static_cast<int>(VISUAL_TEST_BETAS.size()));
        post("VISUAL β " + visual_beta_label(), MessageLevel::Info);
    } else if (pressed("body_scale")) {
        body_scale_index_ = (body_scale_index_ + 1) % static_cast<int>(BODY_SCALES.size());
        session_.set_body_scale_exaggeration(BODY_SCALES[static_cast<std::size_t>(body_scale_index_)]);
        // Rule 12: never change a planet's scale silently.
        post("BODY SCALE " + body_scale_label() + " — planets are drawn " + body_scale_label() + " life size",
             MessageLevel::Warning);
        for (const auto& warning :
             celestial_.camera_inside_bodies(session_, camera_.world_camera().position, body_scale_label())) {
            std::fprintf(stderr, "WARNING: %s\n", warning.c_str());
        }
    } else if (pressed("cruise_burn")) {
        // Everything needed to actually go fast, in one key: the effects are
        // invisible below beta ~ 0.1 and the only honest way to see them is to fly
        // there. Eight years of burning, at warp 1e8, is a few minutes of
        // watching.
        session_.set_engine_mode("CRUISE");
        controls_->point("prograde");
        controls_->set_throttle(1.0);
        controls_->set_warp_index(static_cast<int>(FlightControls::WARP_LEVELS.size()) - 1);
        post("CRUISE BURN — prograde, full throttle, warp 1e8", MessageLevel::Mission);
    } else if (pressed("optics_aberration")) {
        celestial_.effect_aberration = !celestial_.effect_aberration;
        post("ABERRATION " + fmt::on_off(celestial_.effect_aberration), MessageLevel::Info);
    } else if (pressed("optics_doppler")) {
        celestial_.effect_doppler = !celestial_.effect_doppler;
        post("DOPPLER " + fmt::on_off(celestial_.effect_doppler), MessageLevel::Info);
    } else if (pressed("optics_beaming")) {
        celestial_.effect_beaming = !celestial_.effect_beaming;
        post("BEAMING " + fmt::on_off(celestial_.effect_beaming), MessageLevel::Info);
    } else if (pressed("optics_light_time")) {
        celestial_.effect_retarded = !celestial_.effect_retarded;
        post("LIGHT TIME " + fmt::on_off(celestial_.effect_retarded), MessageLevel::Info);
    }
}

void FlightApp::mouse_button(MouseButton button, bool pressed, Vec2 pixel) {
    if (button == MouseButton::Left && pressed) {
        if (click_cockpit(pixel)) {
            return;
        }
    }
    if (map_visible_) {
        if (button == MouseButton::WheelUp && pressed) {
            orbit_map_.zoom = std::clamp(orbit_map_.zoom * 1.2, 0.2, 40.0);
            return;
        }
        if (button == MouseButton::WheelDown && pressed) {
            orbit_map_.zoom = std::clamp(orbit_map_.zoom / 1.2, 0.2, 40.0);
            return;
        }
    }
    camera_.handle_mouse_button(button, pressed);
}

void FlightApp::mouse_motion(Vec2 relative, Vec2 pixel, bool free_look) {
    hover_cockpit(pixel);
    camera_.handle_mouse_motion(relative.x, relative.y, free_look);
}

void FlightApp::point_at_target(bool anti) {
    // Pointing at the target is not a mode of the attitude controller -- its
    // modes are guidance laws (prograde, normal, radial), and "where the Moon is"
    // is not a law, it is a direction.
    if (!directions_.target.has_value()) {
        post("NO TARGET SELECTED", MessageLevel::Warning);
        return;
    }
    post(std::string{"POINT "} + (anti ? "ANTI-TARGET" : "TARGET") +
             " NOT AVAILABLE — the core takes guidance laws, not directions",
         MessageLevel::Warning);
}

void FlightApp::cycle_focus() {
    camera_.focus_index += 1;
    if (camera_.focus_index >= session_.body_count()) {
        camera_.focus_index = -1;
    }
    const std::string name =
        camera_.focus_index < 0 ? std::string{"spacecraft"} : session_.body_name(camera_.focus_index);
    post("FOCUS " + fmt::upper(name), MessageLevel::Info);
}

void FlightApp::cycle_execution_model() {
    // Which execution model the planner corrects against. A real choice, offered
    // rather than buried:
    //
    //   FINITE_BURN  ideal guidance; e ~ 0.0017 over 365 epochs, plans in ~7 s
    //   AUTOPILOT    the attitude controller is INSIDE the corrected map, so the
    //                pointing lag is part of the trajectory -- strictly better
    //                physics, and minutes per plan
    const std::string wanted = session_.execution_model() == "FINITE_BURN" ? "autopilot" : "finite";
    if (session_.set_execution_model(wanted)) {
        const std::string note = wanted == "autopilot" ? "  (planning now takes minutes, not seconds)" : "";
        post("PLANNER CORRECTS AGAINST " + session_.execution_model() + note, MessageLevel::Info);
    }
}

void FlightApp::escape() {
    if (help_visible_) {
        show_panel(Panel::Help, false);
    } else if (mission_panel_.visible) {
        show_panel(Panel::Mission, false);
    } else if (map_visible_) {
        show_panel(Panel::Map, false);
    } else {
        set_paused(!controls_->paused());
    }
}

void FlightApp::set_paused(bool value) {
    controls_->set_paused(value);
    show_panel(Panel::Pause, value);
    if (value) {
        post("PAUSED — simulation time stopped", MessageLevel::Info);
    }
}

bool FlightApp::panel_visible(Panel panel) const {
    switch (panel) {
        case Panel::Mission: return mission_panel_.visible;
        case Panel::Pause: return pause_visible_;
        case Panel::Help: return help_visible_;
        case Panel::Map: return map_visible_;
    }
    return false;
}

void FlightApp::toggle_panel(Panel panel) { show_panel(panel, !panel_visible(panel)); }

void FlightApp::show_panel(Panel panel, bool value) {
    // The three centre panels share one spot and stacked on top of each other:
    // pausing and then opening the computer gave two overlapping panels, with the
    // buttons of both answering the click. Opening one closes the others.
    if (value && panel != Panel::Map) {
        if (panel != Panel::Mission) mission_panel_.visible = false;
        if (panel != Panel::Pause) pause_visible_ = false;
        if (panel != Panel::Help) help_visible_ = false;
    }
    switch (panel) {
        case Panel::Mission: mission_panel_.visible = value; break;
        case Panel::Pause: pause_visible_ = value; break;
        case Panel::Help: help_visible_ = value; break;
        case Panel::Map: map_visible_ = value; break;
    }
    if (panel == Panel::Mission && value) {
        mission_panel_.set_targets(session_.selectable_targets(), session_.target_body());
        mission_panel_.show_plan(session_.plan(), session_.last_error());
    }
    // An open panel frees the pointer; no panel open gives it back to the camera.
    // Without this the mouse stays captured under a menu and the player cannot
    // click the very button they just opened.
    if (value) {
        camera_.set_mouse_look(false);
    }
}

void FlightApp::apply_setting(const std::string& key, double value) {
    if (key == "mouse_sensitivity") {
        settings_.mouse_sensitivity = value;
        camera_.mouse_sensitivity = value;
    } else if (key == "master_volume") {
        settings_.master_volume = value;
        audio_.set_volumes(value, audio_.effects_volume());
    } else if (key == "effects_volume") {
        settings_.effects_volume = value;
        audio_.set_volumes(audio_.master_volume(), value);
    } else if (key == "ui_scale") {
        settings_.ui_scale = value;
    }
}

// --- the cockpit, by mouse ----------------------------------------------------

std::optional<std::pair<Vec3, Vec3>> FlightApp::cockpit_ray(Vec2 pixel) const {
    // A ray from the pilot's eye through the pixel under the pointer, in the BODY
    // frame, where the controls are placed. The near camera lives in the near
    // world -- the body frame turned by the attitude -- so the ray is turned back
    // by the transpose, and the ray and the panels are then in the SAME frame. (In
    // the Godot scene the first version tested a body-frame ray against
    // world-frame panels, and with the ship pointed anywhere but along J2000 x a
    // button answered metres from where it was drawn.)
    if (!camera_.is_cockpit() || camera_.focus_index >= 0 || viewport_size_.x <= 0.0 || viewport_size_.y <= 0.0) {
        return std::nullopt;
    }
    const auto& near = camera_.near_camera();
    const double tan_half = std::tan(near.fov_deg * std::numbers::pi / 360.0);
    const double aspect = viewport_size_.x / viewport_size_.y;
    const double x = (2.0 * pixel.x / viewport_size_.x - 1.0) * tan_half * aspect;
    const double y = (1.0 - 2.0 * pixel.y / viewport_size_.y) * tan_half;
    const Vec3 direction_world = (near.basis * Vec3{x, y, -1.0}).normalized();
    const Basis to_body = ship_basis_.transposed();
    return std::make_pair(to_body * near.position, to_body * direction_world);
}

void FlightApp::hover_cockpit(Vec2 pixel) {
    const auto ray = cockpit_ray(pixel);
    if (!ray.has_value()) {
        cockpit_->set_hover(-1);
        return;
    }
    cockpit_->set_hover(cockpit_->pick(ray->first, ray->second));
}

bool FlightApp::click_cockpit(Vec2 pixel) {
    const auto ray = cockpit_ray(pixel);
    if (!ray.has_value()) {
        return false;
    }
    const int index = cockpit_->pick(ray->first, ray->second);
    if (index < 0) {
        return false;
    }
    cockpit_->controls()[static_cast<std::size_t>(index)].press();
    return true;
}

// --- drawing ---------------------------------------------------------------------

void FlightApp::draw_display(const std::string& name, Canvas& canvas, Vec2 size) {
    if (name == "flight") {
        flight_display_.draw(canvas, size, data_);
    } else if (name == "nav") {
        nav_display_.draw(canvas, size, data_);
    } else if (name == "target") {
        target_display_.draw(canvas, size, data_);
    } else if (name == "system") {
        system_display_.draw(canvas, size, data_);
    }
}

bool FlightApp::hud_visible() const {
    return std::string{HUD_MODES[static_cast<std::size_t>(hud_mode_)]} != "off" && !map_visible_;
}

void FlightApp::draw_hud(Canvas& canvas, Vec2 size) { hud_.draw(canvas, size, data_); }

void FlightApp::draw_map(Canvas& canvas, Vec2 size) {
    orbit_map_.draw(canvas, size, data_);
}

// --- read-out helpers ------------------------------------------------------------

int FlightApp::rcs_firing_count() const {
    return static_cast<int>(
        std::count_if(rcs_throttles_.begin(), rcs_throttles_.end(), [](double value) { return value > 0.002; }));
}

std::string FlightApp::visual_beta_label() const {
    return visual_beta_index_ == 0
               ? std::string{"flight"}
               : fmt::format("%.2fc", VISUAL_TEST_BETAS[static_cast<std::size_t>(visual_beta_index_)]);
}

std::string FlightApp::body_scale_label() const {
    return fmt::format("%.0fx", BODY_SCALES[static_cast<std::size_t>(body_scale_index_)]);
}

void FlightApp::set_visual_beta_index(int index) {
    visual_beta_index_ = std::clamp(index, 0, static_cast<int>(VISUAL_TEST_BETAS.size()) - 1);
    session_.set_visual_test_beta(VISUAL_TEST_BETAS[static_cast<std::size_t>(visual_beta_index_)]);
}

// =============================================================================
//  evidence capture
// =============================================================================

void FlightApp::capture_step() {
    // One photograph per (visual beta, direction of gaze), then quit.
    //
    // Both directions, because at beta = 0.9 they are two different claims and
    // only one of them is about the stacking: forward shows the cone and the blue
    // shift, aft shows a sky that has really gone out. A capture that looked one
    // way only would not tell a dark sky from a broken one.
    hud_mode_ = 2;   // "off"
    messages_.set_shown(false);
    if (!capture_started_) {
        capture_started_ = true;
        camera_.set_mode(CameraRig::Mode::VelocityReference);
        // Far off, so the ship is a dot and not an obstacle. These images are the
        // Milestone 5 evidence and their subject is the SKY.
        camera_.orbit_zoom = 20.0;
    }
    capture_frames_ += 1;
    if (capture_frames_ < 30) {
        return;
    }
    capture_frames_ = 0;

    const int beta_index = capture_index_ / 2;
    const std::string looking = capture_index_ % 2 == 0 ? "forward" : "aft";
    const double beta = VISUAL_TEST_BETAS[static_cast<std::size_t>(beta_index)];
    std::string label = "propagated";
    if (beta >= 0.0) {
        // GDScript's str(float): the shortest spelling, and ".0" on a whole
        // number -- the names the evidence in docs/validation/scene carries.
        label = fmt::format("%g", beta);
        if (label.find('.') == std::string::npos) {
            label += ".0";
        }
        std::replace(label.begin(), label.end(), '.', 'p');
    }
    const std::string name = "scene_" + looking + "_beta_" + label;
    std::filesystem::create_directories(capture_dir_);
    if (on_screenshot) {
        on_screenshot(capture_dir_ + "/" + name + ".png");
    }
    std::string joined;
    for (const auto& line : debug_hud::sky_lines(*this)) {
        joined += (joined.empty() ? "" : " | ") + line;
    }
    std::printf("[capture] %s.png | %s\n", name.c_str(), joined.c_str());
    std::fflush(stdout);

    capture_index_ += 1;
    if (capture_index_ >= static_cast<int>(VISUAL_TEST_BETAS.size()) * 2) {
        request_quit(0);
        return;
    }
    set_visual_beta_index(capture_index_ / 2);
    camera_.orbit_azimuth = capture_index_ % 2 == 0 ? std::numbers::pi : 0.0;
}

}  // namespace sf::app
