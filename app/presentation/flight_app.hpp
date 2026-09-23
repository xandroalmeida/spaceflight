#pragma once

// The vertical slice: a cockpit, a ship, and the Solar System (Milestones 7, 8).
//
// This file is the ORCHESTRATOR. It draws nothing, computes nothing and knows no
// physics: it builds the pieces, hands each one what it needs, and routes the
// input. The project's architecture rule is the same it was in Milestone 2
// (rule 77):
//
//     CORE  --snapshot-->  PRESENTATION  --data-->  RENDERER
//
// Nothing below integrates an orbit, solves a Lambert problem, invents thrust or
// invents propellant. Every number that appears on screen came out of a call to
// FlightSession, and what this does with it is choose the unit.
//
// It has no window and no GPU. The application owns one and renders what it
// exposes; the headless run and the tests drive it with nothing at all.
//
// ## The scene, in one screen
//
//     world (scene units, 1 = 1e6 m)   stars, Sun, planets          CameraRig world
//     near  (metres, body frame)       hull, cockpit, plume, jets    CameraRig near
//     interface                        HUD, map, mission computer, menus, read-out
//
// Why there are two worlds and two scales is written in camera_rig.hpp, next to
// the conversion between them, which happens on one line.

#include "app/presentation/audio_director.hpp"
#include "app/presentation/camera_rig.hpp"
#include "app/presentation/flight_controls.hpp"
#include "app/presentation/input_actions.hpp"
#include "app/presentation/instruments/displays.hpp"
#include "app/presentation/instruments/nav_display.hpp"
#include "app/presentation/instruments/orbit_map.hpp"
#include "app/presentation/message_log.hpp"
#include "app/presentation/scene/celestial_view.hpp"
#include "app/presentation/scene/cockpit.hpp"
#include "app/presentation/scene/spacecraft_visual.hpp"
#include "app/presentation/ui/mission_panel.hpp"
#include "app/session/flight_session.hpp"
#include "app/session/star_sky.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sf::app {

class HeadlessDriver;
class ShotDirector;

struct FlightConfig {
    std::string kernel_directory;
    std::string catalogue_path;
    // Initial state (rule 65): a 400 km parking orbit at 51.6 degrees -- the ISS
    // inclination, chosen because it gives an honest departure geometry for the
    // Moon and because it is an orbit that exists.
    double altitude_m{400000.0};
    double inclination_deg{51.6};
    std::string epoch_utc{"2026-01-01T00:00:00"};
    // How far the nose starts below prograde. From 400 km the Earth's limb is
    // 19.7 degrees below the local horizontal, so a nose exactly on prograde puts
    // the whole planet below the window sill.
    double nadir_bias_deg{25.0};
    // No keyboard and no screen: the headless driver flies (rule 62).
    bool headless{false};
    std::string headless_destination;   // empty: the slew-and-burn check; else a mission
};

// The settings of the pause menu (rule 70).
struct Settings {
    double mouse_sensitivity{1.0};
    double master_volume{0.8};
    double effects_volume{0.8};
    double ui_scale{1.0};
};

class FlightApp {
public:
    static constexpr double RENDER_SCALE = 1.0e-6;
    static constexpr std::array<double, 4> BODY_SCALES = {1.0, 10.0, 100.0, 1000.0};
    static constexpr std::array<double, 5> EXPOSURE_LEVELS = {0.0158, 0.0501, 0.1585, 0.5012, 1.5849};
    static constexpr std::array<double, 6> VISUAL_TEST_BETAS = {-1.0, 0.0, 0.1, 0.5, 0.9, 0.99};
    static constexpr double MAGNITUDE_LIMIT = 7.96;
    static constexpr int ORBIT_TRACK_SAMPLES = 192;
    static constexpr int TARGET_TRACK_SAMPLES = 128;
    static constexpr std::array<const char*, 3> HUD_MODES = {"cockpit", "minimal", "off"};
    // Which lunar orbit the mission asks for. Note what is NOT a parameter: the
    // flight time. It is what the search decides, and fixing it is precisely the
    // defect that made Milestone 6's campaign fail in 82 % of its epochs.
    static constexpr double MISSION_PERIAPSIS_KM = 100.0;
    static constexpr double MISSION_APOAPSIS_KM = 100.0;
    static constexpr double MISSION_SEARCH_HOURS = 2.0;
    // The seven physical buttons on the panel (rule 24).
    static constexpr std::array<const char*, 7> BUTTONS = {"ENGINE", "RCS", "AP", "NAV", "MAP", "WARP", "MODE"};

    explicit FlightApp(AudioSink& audio);
    ~FlightApp();
    FlightApp(const FlightApp&) = delete;
    FlightApp& operator=(const FlightApp&) = delete;

    // Loads the kernels and the catalogue and builds the scene. False, with the
    // reason in `error()`, when the kernels cannot be read.
    bool initialise(const FlightConfig& config);
    [[nodiscard]] const std::string& error() const { return error_; }

    // One frame: input already delivered, `delta` seconds of wall time.
    void frame(double delta, const KeyboardState& keyboard);

    // --- input -------------------------------------------------------------
    void key_event(const KeyEvent& event);
    // `pixel` in the viewport's pixels, origin top-left.
    void mouse_button(MouseButton button, bool pressed, Vec2 pixel);
    void mouse_motion(Vec2 relative, Vec2 pixel, bool free_look);
    void set_viewport_size(Vec2 size) { viewport_size_ = size; }
    [[nodiscard]] Vec2 viewport_size() const { return viewport_size_; }

    // --- mission -------------------------------------------------------------
    bool plan_mission(const std::string& target = "", double periapsis_km = MISSION_PERIAPSIS_KM,
                      double apoapsis_km = MISSION_APOAPSIS_KM);
    void choose_alternative(int index);
    void cancel_planning();
    bool arm_mission();
    void abort_mission();
    void set_target(const std::string& name);

    // --- panels ------------------------------------------------------------
    enum class Panel { Mission, Pause, Help, Map };
    void show_panel(Panel panel, bool value);
    void toggle_panel(Panel panel);
    [[nodiscard]] bool panel_visible(Panel panel) const;
    void set_map_mode(OrbitMap::Mode mode);
    void cycle_map_mode();
    void set_paused(bool value);
    void apply_setting(const std::string& key, double value);
    void request_quit(int code = 0) { quit_code_ = code; }
    [[nodiscard]] std::optional<int> quit_requested() const { return quit_code_; }

    // --- what the renderer and the interface read -----------------------------
    [[nodiscard]] FlightSession& session() { return session_; }
    [[nodiscard]] const FlightSession& session() const { return session_; }
    [[nodiscard]] StarSky& sky() { return sky_; }
    [[nodiscard]] const StarSky& sky() const { return sky_; }
    [[nodiscard]] FlightControls& controls() { return *controls_; }
    [[nodiscard]] const FlightControls& controls() const { return *controls_; }
    [[nodiscard]] CameraRig& camera() { return camera_; }
    [[nodiscard]] const CameraRig& camera() const { return camera_; }
    [[nodiscard]] CelestialView& celestial() { return celestial_; }
    [[nodiscard]] const CelestialView& celestial() const { return celestial_; }
    [[nodiscard]] StarfieldView& starfield() { return starfield_; }
    [[nodiscard]] const std::vector<BodyDraw>& bodies() const { return bodies_; }
    [[nodiscard]] MeshLibrary& meshes() { return meshes_; }
    [[nodiscard]] const SpacecraftVisual& spacecraft() const { return *spacecraft_; }
    [[nodiscard]] const EnginePlume& plume() const { return *plume_; }
    [[nodiscard]] const RcsVisual& rcs_visual() const { return *rcs_visual_; }
    [[nodiscard]] CockpitInterior& cockpit() { return *cockpit_; }
    [[nodiscard]] const CockpitInterior& cockpit() const { return *cockpit_; }
    [[nodiscard]] Basis ship_basis() const { return ship_basis_; }
    [[nodiscard]] MessageLog& messages() { return messages_; }
    [[nodiscard]] MissionPanel& mission_panel() { return mission_panel_; }
    [[nodiscard]] OrbitMap& orbit_map() { return orbit_map_; }
    [[nodiscard]] const InstrumentData& instrument_data() const { return data_; }
    [[nodiscard]] Settings& settings() { return settings_; }
    [[nodiscard]] AudioDirector& audio() { return audio_; }
    [[nodiscard]] double exposure() const { return EXPOSURE_LEVELS[static_cast<std::size_t>(exposure_index_)]; }

    // Draws one of the cockpit's displays, or the HUD, or the map.
    void draw_display(const std::string& name, Canvas& canvas, Vec2 size);
    void draw_hud(Canvas& canvas, Vec2 size);
    void draw_map(Canvas& canvas, Vec2 size);

    [[nodiscard]] bool hud_visible() const;
    [[nodiscard]] bool debug_visible() const { return debug_visible_; }
    void set_debug_visible(bool value) { debug_visible_ = value; }
    [[nodiscard]] int hud_mode() const { return hud_mode_; }
    void set_hud_mode(int mode) { hud_mode_ = mode; }

    // --- the technical read-out and the headless verification ---------------
    [[nodiscard]] int rcs_firing_count() const;
    [[nodiscard]] int rcs_thruster_count() const { return rcs_thruster_count_; }
    [[nodiscard]] std::string visual_beta_label() const;
    [[nodiscard]] std::string body_scale_label() const;
    [[nodiscard]] bool master_caution() const;
    [[nodiscard]] double fps() const { return fps_; }
    [[nodiscard]] double frame_ms() const { return frame_ms_; }
    [[nodiscard]] bool headless() const { return config_.headless; }

    // A ray from the pilot's eye through `pixel`, in the BODY frame, in metres.
    [[nodiscard]] std::optional<std::pair<Vec3, Vec3>> cockpit_ray(Vec2 pixel) const;

    // Hooks the application fills in.
    std::function<void(const std::string& path)> on_screenshot;
    std::function<void(bool captured)> on_mouse_capture;

    // Evidence capture (M5 ladder and the M7/M8 shot scripts).
    void set_capture_directory(const std::string& directory) { capture_dir_ = directory; }
    void set_shot_director(std::unique_ptr<ShotDirector> director);

    // State the shot director and the tests steer directly.
    void set_warp_index(int index) { controls_->set_warp_index(index); }
    void set_visual_beta_index(int index);

private:
    void build_scene();
    void wire_controls();
    void camera_keys(double delta, const KeyboardState& keyboard);
    void place_camera();
    void update_world(double delta);
    void update_ship(double delta);
    void update_tracks(double delta);
    void update_instruments();
    void update_audio();
    void poll_planning();
    void watch_mission();
    void capture_step();
    void escape();
    void point_at_target(bool anti);
    void cycle_focus();
    void cycle_execution_model();
    void hover_cockpit(Vec2 pixel);
    bool click_cockpit(Vec2 pixel);
    void print_plan(const PlanSummary& plan) const;
    [[nodiscard]] int target_index() const;
    [[nodiscard]] double closing_sign(int target) const;
    [[nodiscard]] std::pair<std::string, double> next_event() const;
    void post(const std::string& text, MessageLevel level = MessageLevel::Info);

    FlightConfig config_{};
    std::string error_;

    FlightSession session_;
    StarSky sky_;
    std::unique_ptr<FlightControls> controls_;
    AudioDirector audio_;
    MessageLog messages_;
    MissionPanel mission_panel_;
    OrbitMap orbit_map_;
    FlightDisplay flight_display_;
    NavDisplay nav_display_;
    TargetDisplay target_display_;
    SystemDisplay system_display_;
    MinimalHud hud_;
    CameraRig camera_{RENDER_SCALE};
    CelestialView celestial_;
    StarfieldView starfield_;
    MeshLibrary meshes_;
    ShipMaterials materials_;
    std::unique_ptr<SpacecraftVisual> spacecraft_;
    std::unique_ptr<EnginePlume> plume_;
    std::unique_ptr<RcsVisual> rcs_visual_;
    std::unique_ptr<CockpitInterior> cockpit_;
    std::unique_ptr<HeadlessDriver> headless_;
    std::unique_ptr<ShotDirector> shots_;
    Settings settings_{};

    // Presentation state.
    int hud_mode_{0};
    int body_scale_index_{0};
    int exposure_index_{2};
    int visual_beta_index_{0};
    bool debug_visible_{false};
    bool map_visible_{false};
    bool help_visible_{false};
    bool pause_visible_{false};
    std::size_t rcs_switch_{1};
    Vec2 viewport_size_{1440.0, 900.0};
    std::optional<int> quit_code_;

    // Per-frame caches.
    Basis ship_basis_{};
    Vec3 ship_position_{};
    FlightDirections directions_{};
    std::vector<double> rcs_throttles_;
    int rcs_thruster_count_{0};
    std::vector<BodyDraw> bodies_;
    std::vector<Vec3> orbit_track_;
    std::vector<Vec3> target_track_;
    std::vector<Vec3> planned_track_;
    std::vector<ManeuverView> maneuvers_;
    InstrumentData data_{};
    double track_timer_{0.0};
    bool search_was_running_{false};
    std::string last_phase_;
    bool warned_low_propellant_{false};
    double fps_{0.0};
    double frame_ms_{0.0};

    // The M5 capture ladder.
    std::string capture_dir_;
    int capture_index_{0};
    int capture_frames_{0};
    bool capture_started_{false};

    friend class HeadlessDriver;
    friend class ShotDirector;
    friend class DebugHud;
};

}  // namespace sf::app
