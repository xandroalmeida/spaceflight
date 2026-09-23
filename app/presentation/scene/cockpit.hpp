#pragma once

// The flight deck, in metres, in the body frame (rules 8, 9, 23, 24, 49, 51, 52).
//
// Built in code, from primitives, because that is what rule 49 asks and because
// an artistic model does not exist. Aesthetic: a modern capsule -- large smooth
// surfaces, few parts, everything functional. It is not a fighter: there are
// seven physical buttons and each one does one thing (rule 8).
//
// ## The geometry
//
//     x = 4.58  +----------------+   window line, on the chamfered nose
//               |    WINDOWS     |
//     x = 4.32  +----------------+   glare shield
//               |FLIGHT NAV TARGET   main panel, tilted towards the pilot
//               | SYSTEM DISPLAY |
//               | physical keys  |
//     x = 3.15  |       o        |   pilot's eye
//     x = 2.30  +----------------+   aft bulkhead, hatch
//
// The eye is CameraRig::EYE, and the constant is the SAME: the panel is built
// round where the camera is, not next to it.
//
// ## Lighting (rule 51)
//
// Two weak lights and the displays themselves. The interior has to be legible
// AND the exterior has to stay observable, and the way to get both is not to
// light more: it is to light LITTLE. The displays emit, the rest is half-dark,
// and the player reads both because the monitor has dynamic range to spare for
// the difference between a panel at 0.3 and the Earth at 1.0.

#include "app/presentation/camera_rig.hpp"
#include "app/presentation/scene/ship_materials.hpp"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sf::app {

// A physical control on the panel: button, switch, rotary, lamp (rules 23, 24).
//
// The four are the same object with different looks, because they are the same
// thing: a rectangular surface on the panel, with a state, that answers the
// pointer. Splitting them into four classes would duplicate the hit test four
// times, and the hit test is the hard part.
//
// The hit test is a ray against the face's plane in six lines, with no physics
// server behind it -- which is why it works without a window and why the tests
// can check it.
class CockpitControl {
public:
    enum class Kind { Button, Switch, Rotary, Indicator };

    static CockpitControl button(const std::string& label, const Vec3& at, double width,
                                 std::function<void(CockpitControl&)> callback);
    static CockpitControl toggle(const std::string& label, const Vec3& at, double width, bool initial,
                                 std::function<void(CockpitControl&)> callback);
    static CockpitControl indicator(const std::string& label, const Vec3& at, Colour colour);

    // The ray and the control in the SAME frame: `transform` is the control's
    // placement in that frame (the body frame, in the cockpit). A ray hits when
    // it meets the face's plane in front of its origin and inside the face.
    [[nodiscard]] bool hit_test(const Vec3& origin, const Vec3& direction) const;

    // A button fires and comes back; a switch flips `on` BEFORE it calls back,
    // so that whoever receives it reads the NEW state.
    void press();
    void set_on(bool value) { on = value; }
    void set_hovered(bool value) { hovered = value; }
    void set_caption(const std::string& text) { caption = text; }
    // The click feedback (rule 24): the face lights and fades in 180 ms. Short on
    // purpose -- a button that stays lit for half a second looks stuck.
    void advance(double delta);

    // What the face and the label look like now.
    [[nodiscard]] Colour face_colour() const;
    [[nodiscard]] double face_energy() const;
    [[nodiscard]] Colour caption_colour() const { return on ? palette::PRIMARY : palette::SECONDARY; }
    [[nodiscard]] std::string caption_text() const { return caption.empty() ? label : label + "\n" + caption; }

    Kind kind{Kind::Button};
    std::string label;
    std::string caption;
    Vec2 half_size{0.06, 0.02};
    bool on{false};
    bool enabled{true};
    Colour indicator_colour{palette::OK};
    bool hovered{false};
    Transform3 transform{};
    std::function<void(CockpitControl&)> pressed_callback;

private:
    [[nodiscard]] Colour resting_colour() const;
    double flash_{0.0};
};

// One of the four instrument displays on the panel.
struct CockpitDisplay {
    std::string name;             // "flight", "nav", "target", "system"
    Transform3 transform{};       // the screen quad, body frame
    Vec2 physical{};              // metres
    int width{0};                 // pixels
    int height{0};
};

class CockpitInterior {
public:
    static constexpr Vec3 EYE = CameraRig::EYE;

    // Where the panel is, and the number is not a preference.
    //
    // The camera has 75 degrees vertical (half-angle 37.5) and rests 11 degrees
    // below the nose line, so the visible band runs from +26.5 to -48.5 degrees.
    // From here, and from the eye, come the angles of the three rows:
    //
    //     lamps           -23 degrees
    //     FLIGHT/NAV/TGT  -28 degrees
    //     SYSTEMS         -43 degrees
    //     keys            -49 degrees   (the bottom edge: it asks for a glance down)
    static constexpr Vec3 PANEL_CENTRE{3.95, 0.0, -0.30};
    static constexpr double PANEL_HALF_WIDTH = 0.95;
    static constexpr double CABIN_HALF_WIDTH = 1.02;
    static constexpr double FLOOR_Z = -0.92;
    static constexpr double CEILING_Z = 0.98;
    static constexpr double AFT_X = 2.30;

    // The window line, seen from above: four posts, three panes, and the corners
    // are SHARED between neighbouring panes. Everything -- glass, posts, head,
    // sill, floor and ceiling -- is built from it.
    static constexpr double CANOPY_ROOT_X = 4.10;
    static constexpr double CANOPY_NOSE_X = 4.58;
    static constexpr double CANOPY_NOSE_HALF_WIDTH = 0.70;
    static constexpr double SILL_Z = 0.00;
    static constexpr double HEAD_Z = 0.88;

    // The hull's thickness in the window opening. It is not decoration: a real
    // window is a hole in a hull WITH a thickness, and it is the reveal -- the
    // wall of the hole, in shadow on one side and lit on the other -- that reads
    // as "window".
    static constexpr double REVEAL = 0.055;
    static constexpr double REVEAL_DEPTH = 0.09;
    static constexpr double SKIN = 0.10;

    // Display resolutions, chosen by each panel's physical proportion so that a
    // pixel is square.
    static constexpr int SMALL_DISPLAY_W = 480;
    static constexpr int SMALL_DISPLAY_H = 315;
    static constexpr int WIDE_DISPLAY_W = 1280;
    static constexpr int WIDE_DISPLAY_H = 122;

    CockpitInterior(MeshLibrary& meshes, const ShipMaterials& materials);

    [[nodiscard]] const std::vector<Part>& parts() const { return parts_; }
    [[nodiscard]] const std::vector<CockpitDisplay>& displays() const { return displays_; }
    [[nodiscard]] const std::vector<PointLight>& lights() const { return lights_; }
    [[nodiscard]] std::vector<CockpitControl>& controls() { return controls_; }
    [[nodiscard]] const std::vector<CockpitControl>& controls() const { return controls_; }

    // The faces and the captions of the controls, which change every frame.
    [[nodiscard]] std::vector<Part> control_parts() const;
    // The caption's quad for control `index`: in front of its face, sized to it.
    [[nodiscard]] Transform3 caption_transform(std::size_t index) const;

    // A ray in the body frame, in metres. Returns the control under it, or -1.
    [[nodiscard]] int pick(const Vec3& origin, const Vec3& direction) const;
    void set_hover(int index);
    [[nodiscard]] int hovered() const { return hovered_; }

    CockpitControl& configure_button(std::size_t index, const std::string& label,
                                     std::function<void(CockpitControl&)> callback, bool is_switch = false,
                                     bool initial = false);
    void set_indicator(const std::string& name, bool lit);
    void advance(double delta);

    bool visible{true};
    bool displays_visible{true};

private:
    void build_shell();
    void build_windows();
    void build_bay(Vec2 a, Vec2 b, int index);
    void bolt_ring(const Transform3& bay, Vec2 opening, double centre_z, int index);
    void ring(const Transform3& parent, Vec2 opening, double thickness, double depth, double centre_z, double at_z,
              const Material& material);
    void build_post(Vec2 at, Vec2 outward);
    void build_glareshield();
    void build_panel();
    void mount_display(const std::string& name, const Vec3& at, Vec2 physical, int width, int height);
    void build_button_strip();
    void build_indicators();
    void build_side_consoles();
    void build_seat();
    void build_lighting();

    Part box(const Vec3& size, const Transform3& placement, const Material& material);
    Part slab(double z, bool facing_up);

    MeshLibrary& meshes_;
    const ShipMaterials& materials_;
    Transform3 panel_{};
    std::vector<Part> parts_;
    std::vector<CockpitDisplay> displays_;
    std::vector<PointLight> lights_;
    std::vector<CockpitControl> controls_;
    std::map<std::string, std::size_t> indicators_;
    std::string face_mesh_small_;
    int hovered_{-1};
};

}  // namespace sf::app
