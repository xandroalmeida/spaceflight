#pragma once

// The cameras, and the two scales (rules 10, 11, 12).
//
// ## Why there are TWO cameras
//
// The scene has 1 unit = 1e6 metres. The Earth is 6.371 units in radius and the
// Moon sits at 358. The world camera's near plane is 0.05 units -- 50 km -- and
// that is kept from the Godot scene, where a smaller near plane lost the stars
// to depth quantisation (`docs/validation/starfield-debug.md` section 6).
//
// The ship is 22 metres. That is 2.2e-5 units: two thousand times INSIDE the
// near plane. No single camera draws the Moon at 358 units and a panel half a
// metre from the face.
//
// So there are two, with the SAME orientation and the SAME field of view:
//
//   world   scene units. Planets, stars, orbits. near 0.05, far 2e5.
//   near    METRES, origin at the ship's centre of mass. Hull, cockpit, plume,
//           jets. near 0.05 m, far 8 km, transparent background, composited on
//           top.
//
// The conversion between them is a multiplication by the render scale and it is
// written in one place, in `commit`. The near camera does not "follow" the
// world one: they are the SAME camera expressed in two units, and that is why
// the parallax between the ship and the planet behind it comes out right
// instead of being tuned.
//
// What this costs, said in full: the near layer is not occluded by the far one.
// A planet never passes in front of the hull. For a camera that is either
// inside the ship or a few tens of metres from it, that does not happen -- and
// it is recorded as VISUAL_DEBT rather than solved with a third pass.

#include "app/presentation/geometry.hpp"

#include <array>
#include <functional>
#include <numbers>
#include <string>

namespace sf::app {

struct CameraPose {
    Vec3 position{};
    Basis basis{};
    double near{0.05};
    double far{1000.0};
    double fov_deg{75.0};    // VERTICAL field of view

    // The camera looks along its local -Z.
    [[nodiscard]] Vec3 forward() const { return basis.z * -1.0; }
};

enum class MouseButton { Left, Right, WheelUp, WheelDown };

class CameraRig {
public:
    enum class Mode { Cockpit, ExternalOrbit, Chase, VelocityReference, TargetReference };
    static constexpr std::array<const char*, 5> MODE_NAMES = {"COCKPIT", "EXTERNAL", "CHASE", "VELOCITY",
                                                              "TARGET"};

    // Where the pilot's head is, in the body frame, in metres (rule 10). Inside
    // the cockpit fairing and behind the front windows, which start at
    // x = 4.0. `CockpitInterior` builds the panels around this point.
    static constexpr Vec3 EYE{3.15, 0.0, 0.36};

    static constexpr double WORLD_NEAR = 0.05;
    static constexpr double WORLD_FAR = 2.0e5;
    static constexpr double NEAR_NEAR = 0.05;     // [m]
    static constexpr double NEAR_FAR = 8000.0;    // [m]
    // 75 degrees VERTICAL. It decides what fits between the window and the
    // panel: at 68 degrees the three displays fell out of frame and the cockpit
    // read as an empty box with stars behind it. The panel positions in
    // cockpit_interior.cpp are at 23, 28 and 43 degrees below the line of sight.
    static constexpr double FOV = 75.0;

    // Where the head looks at rest: eleven degrees below the nose line. It is
    // what a pilot does; at zero the window is centred and the whole panel is
    // out of frame.
    static constexpr double COCKPIT_REST_PITCH = -0.1920;

    // Head limits. INPUT limits on a camera angle, not statements about the
    // world: past 90 degrees the "up" vector flips and the control reverses in
    // the pilot's hands.
    static constexpr double LOOK_YAW_LIMIT = 2.44;       // 140 degrees
    static constexpr double LOOK_PITCH_LIMIT = 1.28;     // 73 degrees
    static constexpr double ORBIT_ELEVATION_LIMIT = 1.5533;   // 89 degrees

    static constexpr double ZOOM_STEP = 1.15;
    static constexpr double ZOOM_MIN = 0.25;
    static constexpr double ZOOM_MAX = 90.0;

    // The external camera's natural distance: the radius that contains the
    // ship, times three.
    static constexpr double EXTERNAL_NATURAL_M = 40.0;

    // The pilot's gaze aligned with the hull. The camera looks along -Z; the
    // ship's nose is +X and its "up" is +Z. So, in body coordinates:
    // cam_Z = -x, cam_Y = +z, and cam_X = cam_Y x cam_Z = -y. Written once,
    // because getting this wrong gives a cockpit that looks sideways and still
    // looks plausible.
    static constexpr Basis COCKPIT_ALIGN{{0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}, {-1.0, 0.0, 0.0}};

    explicit CameraRig(double render_scale = 1.0e-6);

    void set_mode(Mode mode);
    void cycle_mode();
    [[nodiscard]] Mode mode() const { return mode_; }
    [[nodiscard]] std::string mode_name() const { return MODE_NAMES[static_cast<std::size_t>(mode_)]; }
    [[nodiscard]] bool is_cockpit() const { return mode_ == Mode::Cockpit; }

    // Called once per frame, after the simulation advances.
    //
    // `beta` is the barycentric velocity over c and is the axis about which the
    // sky is aberrated -- not prograde. The distinction cost a lying read-out in
    // M5 and is preserved: in low orbit the two point about 30 degrees apart.
    void update(const Vec3& ship_position, const Basis& ship_basis, const Vec3& beta,
                const Vec3& target_direction, const Vec3& focus_position, double focus_natural);

    // --- input ---
    bool handle_mouse_button(MouseButton button, bool pressed);
    bool handle_mouse_motion(double dx, double dy, bool free_look);
    void set_mouse_look(bool enabled);
    void apply_keys(double delta, double yaw_axis, double pitch_axis, bool free_look);
    void zoom(double steps);
    void recentre();

    // Whether the camera is really where its preset puts it. The read-out needs
    // this: a label that keeps saying "prograde" after the pilot has orbited
    // away from prograde is a read-out that lies.
    [[nodiscard]] bool at_preset() const;
    [[nodiscard]] Vec3 look_direction() const { return world_.forward(); }

    [[nodiscard]] const CameraPose& world_camera() const { return world_; }
    [[nodiscard]] const CameraPose& near_camera() const { return near_; }
    [[nodiscard]] bool mouse_captured() const { return mouse_look_ || dragging_; }

    // The cursor capture is the platform's; the rig asks for it.
    std::function<void(bool)> on_capture_changed;

    double mouse_sensitivity{1.0};
    bool invert_look{false};
    double orbit_azimuth{std::numbers::pi};
    double orbit_elevation{0.35};
    double orbit_zoom{1.0};
    double look_yaw{0.0};
    double look_pitch{0.0};
    // Focus on a body, for the technical view inherited from M5. -1 = the ship.
    int focus_index{-1};

private:
    void place_cockpit();
    void place_external(const Vec3& beta, const Vec3& target_direction);
    void place_orbiting_body();
    [[nodiscard]] std::array<Vec3, 2> orbit_frame(const Vec3& beta, const Vec3& target_direction) const;
    [[nodiscard]] static Vec3 stable_up(const Vec3& forward);
    [[nodiscard]] Basis aim(const Vec3& forward, const Vec3& up) const;
    void commit(const Vec3& eye_metres, const Basis& basis);
    void apply_look(double dx, double dy);
    void settle();
    void set_capture(bool enabled);
    [[nodiscard]] bool preset(Mode mode, double& azimuth, double& elevation) const;

    Mode mode_{Mode::Cockpit};
    double render_scale_{1.0e-6};
    Vec3 ship_position_{};
    Basis ship_basis_{};
    Vec3 focus_position_{};
    double focus_natural_{1.0};
    bool dragging_{false};
    bool mouse_look_{false};

    CameraPose world_{};
    CameraPose near_{};
};

}  // namespace sf::app
