#include "app/presentation/scene/cockpit.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sf::app {
namespace {

Transform3 translate(const Vec3& at) { return Transform3{Basis{}, at}; }

// The outward normal of a pane, seen from above.
Vec2 outward(Vec2 a, Vec2 b) {
    const Vec2 along = (b - a).normalized();
    return Vec2{-along.y, along.x};
}

// A pane's frame: local Z is the normal (outwards), local Y is the cabin's
// "up", local X runs along the pane. Written as columns because three Euler
// rotations composed in some order are an invitation to a window turned
// sideways -- and a window turned sideways still looks like a window.
Basis facing(Vec2 normal) {
    return Basis{Vec3{-normal.y, normal.x, 0.0}, Vec3{0.0, 0.0, 1.0}, Vec3{normal.x, normal.y, 0.0}};
}

}  // namespace

// --- CockpitControl ---------------------------------------------------------------

CockpitControl CockpitControl::button(const std::string& label, const Vec3& at, double width,
                                      std::function<void(CockpitControl&)> callback) {
    CockpitControl c{};
    c.kind = Kind::Button;
    c.label = label;
    c.half_size = Vec2{width * 0.5, 0.019};
    c.transform = translate(at);
    c.pressed_callback = std::move(callback);
    return c;
}

CockpitControl CockpitControl::toggle(const std::string& label, const Vec3& at, double width, bool initial,
                                      std::function<void(CockpitControl&)> callback) {
    CockpitControl c = button(label, at, width, std::move(callback));
    c.kind = Kind::Switch;
    c.on = initial;
    return c;
}

CockpitControl CockpitControl::indicator(const std::string& label, const Vec3& at, Colour colour) {
    CockpitControl c{};
    c.kind = Kind::Indicator;
    c.label = label;
    c.half_size = Vec2{0.026, 0.012};
    c.transform = translate(at);
    c.indicator_colour = colour;
    return c;
}

bool CockpitControl::hit_test(const Vec3& origin, const Vec3& direction) const {
    if (!enabled || kind == Kind::Indicator) {
        return false;
    }
    const Vec3 normal = transform.basis.z.normalized();
    const double denominator = dot(normal, direction);
    if (std::abs(denominator) < 1.0e-6) {
        return false;   // the ray runs parallel to the panel
    }
    const double distance = dot(normal, transform.origin - origin) / denominator;
    if (distance <= 0.0) {
        return false;   // the panel is behind the observer
    }
    const Vec3 local = transform.affine_inverse() * (origin + direction * distance);
    return std::abs(local.x) <= half_size.x && std::abs(local.y) <= half_size.y;
}

void CockpitControl::press() {
    if (!enabled) {
        return;
    }
    if (kind == Kind::Switch) {
        on = !on;
    }
    flash_ = 1.0;
    if (pressed_callback) {
        pressed_callback(*this);
    }
}

void CockpitControl::advance(double delta) {
    if (flash_ <= 0.0) {
        return;
    }
    flash_ = std::max(flash_ - delta / 0.18, 0.0);
}

Colour CockpitControl::resting_colour() const {
    if (!enabled) {
        return palette::PANEL.darkened(0.3F);
    }
    switch (kind) {
        case Kind::Indicator: return on ? indicator_colour : palette::PANEL_EDGE.darkened(0.4F);
        case Kind::Switch: return on ? palette::NAV_DIM : palette::PANEL_EDGE;
        default: return palette::PANEL_EDGE;
    }
}

Colour CockpitControl::face_colour() const {
    Colour colour = resting_colour();
    // A DISCREET hover highlight (rule 24): 18 % lighter, enough to say "this
    // answers" and little enough not to flicker as the pointer crosses the panel.
    if (hovered) {
        colour = colour.lightened(0.18F);
    }
    if (flash_ > 0.0) {
        colour = colour.lerp(palette::PRIMARY, static_cast<float>(flash_ * 0.75));
    }
    return colour;
}

double CockpitControl::face_energy() const { return 0.25 + (on ? 0.9 : 0.0) + flash_ * 0.8; }

// --- CockpitInterior --------------------------------------------------------------

CockpitInterior::CockpitInterior(MeshLibrary& meshes, const ShipMaterials& materials)
    : meshes_(meshes), materials_(materials) {
    build_shell();
    build_windows();
    build_panel();
    build_side_consoles();
    build_seat();
    build_lighting();
}

Part CockpitInterior::box(const Vec3& size, const Transform3& placement, const Material& material) {
    Part part{};
    part.mesh = meshes_.box(size);
    part.transform = placement;
    part.material = material;
    return part;
}

// The cabin's plan, seen from above: the pressurised rectangle aft and the
// chamfered nose forward, ending exactly on the window line.
//
// Floor and ceiling are POLYGONS and not boxes. With boxes, the forward corners
// stuck out past the window line -- two slabs floating outside, visible through
// the side panes -- and shortening them opened a gap of space between the end
// of the wall and the head. A polygon of the right shape has neither.
Part CockpitInterior::slab(double z, bool facing_up) {
    const std::array<Vec2, 6> outline = {
        Vec2{AFT_X, CABIN_HALF_WIDTH},
        Vec2{CANOPY_ROOT_X, CABIN_HALF_WIDTH},
        Vec2{CANOPY_NOSE_X, CANOPY_NOSE_HALF_WIDTH},
        Vec2{CANOPY_NOSE_X, -CANOPY_NOSE_HALF_WIDTH},
        Vec2{CANOPY_ROOT_X, -CABIN_HALF_WIDTH},
        Vec2{AFT_X, -CABIN_HALF_WIDTH},
    };
    // The normals are written by hand instead of deduced from the vertex order:
    // the material has culling off, so the order no longer decides whether the
    // face exists, and letting it decide the lighting would keep the same trap
    // where it cannot be seen -- a ceiling lit by its top face is a black
    // ceiling.
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    const Vec3 normal{0.0, 0.0, facing_up ? 1.0 : -1.0};
    for (std::size_t i = 1; i + 1 < outline.size(); ++i) {
        for (const auto& point : {outline[0], outline[i], outline[i + 1]}) {
            positions.push_back(Vec3{point.x, point.y, z});
            normals.push_back(normal);
            // In world metres: the floor's tile lines up with the wall's, which is
            // what is expected of a panel bolted to a structure.
            uvs.push_back(point * 0.5);
        }
    }
    Part part{};
    part.mesh = meshes_.add(facing_up ? "cabin:floor" : "cabin:ceiling", mesh::triangles(positions, normals, uvs));
    part.material = materials_.cabin_shell;
    return part;
}

void CockpitInterior::build_shell() {
    parts_.push_back(slab(FLOOR_Z, true));
    // The ceiling runs to the window line and not half a metre short of it. The
    // first capture had a triangle of space in the top left corner: the gap
    // between the ceiling's front edge and the window, and the transparent near
    // layer shows the world wherever there is no geometry.
    parts_.push_back(slab(CEILING_Z, false));

    // The walls end WHERE the window starts (x = 4.10). When they ran past the
    // line, the aft half of the side panes looked into the wall itself.
    for (const double side : {1.0, -1.0}) {
        parts_.push_back(box(Vec3{CANOPY_ROOT_X - AFT_X, 0.06, CEILING_Z - FLOOR_Z},
                             translate(Vec3{(CANOPY_ROOT_X + AFT_X) * 0.5, side * CABIN_HALF_WIDTH,
                                            (CEILING_Z + FLOOR_Z) * 0.5}),
                             materials_.cockpit_panel));
    }

    build_aft_bulkhead();

    // Overhead panel: a few breakers and nothing to press. A cabin without a
    // ceiling reads as an open set, and when the pilot looks up there has to be
    // ship and not void.
    parts_.push_back(box(Vec3{0.62, 1.40, 0.05}, translate(Vec3{3.48, 0.0, CEILING_Z - 0.10}),
                         materials_.cockpit_panel));
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 8; ++column) {
            parts_.push_back(box(Vec3{0.045, 0.045, 0.018},
                                 translate(Vec3{3.30 + row * 0.13, -0.56 + column * 0.16, CEILING_Z - 0.135}),
                                 materials_.bare_metal));
        }
    }
}

void CockpitInterior::build_aft_bulkhead() {
    // The aft bulkhead closes the whole end of the cabin, with a closed door to
    // the habitat and two auxiliary panels. Everything is built from boxes: the
    // first attempt used capped cylinders for a round hatch, and from the seat
    // their faces did not show -- the pilot saw the wall through the door.
    constexpr double face = AFT_X + 0.03;   // the bulkhead's cabin-side face
    parts_.push_back(box(Vec3{0.06, CABIN_HALF_WIDTH * 2.0, CEILING_Z - FLOOR_Z},
                         translate(Vec3{AFT_X, 0.0, (CEILING_Z + FLOOR_Z) * 0.5}), materials_.cabin_shell));

    // Structural rails where the bulkhead meets the side walls, floor and
    // ceiling. Their seams give the wall depth instead of one flat tile.
    for (const double side : {-1.0, 1.0}) {
        parts_.push_back(box(Vec3{0.11, 0.075, CEILING_Z - FLOOR_Z},
                             translate(Vec3{face + 0.035, side * (CABIN_HALF_WIDTH - 0.035), 0.03}),
                             materials_.frame_alloy));
    }
    for (const double z : {FLOOR_Z + 0.035, CEILING_Z - 0.035}) {
        parts_.push_back(box(Vec3{0.11, CABIN_HALF_WIDTH * 2.0, 0.07}, translate(Vec3{face + 0.035, 0.0, z}),
                             materials_.frame_alloy));
    }

    build_aft_door(face);
    build_aft_instruments(face);
}

void CockpitInterior::build_aft_door(double face) {
    // A pressure door, closed: 0.78 x 1.52 m over a 10 cm threshold, a person's
    // width and as tall as leaves room for the seal lamp under the ceiling rail.
    // The seat back stands in front of its middle, as it would in a two-metre
    // cabin.
    constexpr double width = 0.78;
    constexpr double height = 1.52;
    constexpr double bottom = FLOOR_Z + 0.10;
    constexpr double centre_z = bottom + height * 0.5;
    constexpr double jamb = 0.07;
    const double top = bottom + height;

    // The dark gap between leaf and frame: without it the leaf reads as a panel
    // painted on the wall and not as something that opens.
    parts_.push_back(box(Vec3{0.02, width, height}, translate(Vec3{face + 0.01, 0.0, centre_z}),
                         materials_.window_seal));
    for (const double side : {-1.0, 1.0}) {
        parts_.push_back(box(Vec3{0.06, jamb, height + jamb * 2.0},
                             translate(Vec3{face + 0.03, side * (width + jamb) * 0.5, centre_z}),
                             materials_.frame_alloy));
    }
    for (const double z : {bottom - jamb * 0.5, top + jamb * 0.5}) {
        parts_.push_back(box(Vec3{0.06, width + jamb * 2.0, jamb}, translate(Vec3{face + 0.03, 0.0, z}),
                             materials_.frame_alloy));
    }

    // The leaf, 12 mm inside the frame all round, and two raised panels split by
    // a stiffener: the proportions of a real hatch leaf.
    parts_.push_back(box(Vec3{0.045, width - 0.024, height - 0.024}, translate(Vec3{face + 0.035, 0.0, centre_z}),
                         materials_.frame_alloy));
    for (const double z : {centre_z + 0.39, centre_z - 0.39}) {
        parts_.push_back(box(Vec3{0.012, width - 0.16, 0.66}, translate(Vec3{face + 0.063, 0.0, z}),
                             materials_.cockpit_panel));
    }
    parts_.push_back(box(Vec3{0.02, width - 0.08, 0.05}, translate(Vec3{face + 0.068, 0.0, centre_z}),
                         materials_.machined_trim));

    // Hinges on one side, the lever and three dogs on the other. The lever lies
    // horizontal and the dogs bridge leaf and frame: both say "closed and locked"
    // without a word written on the door.
    const double hinge_y = width * 0.5 - 0.01;
    for (const double z : {centre_z - 0.60, centre_z, centre_z + 0.60}) {
        parts_.push_back(box(Vec3{0.05, 0.035, 0.13}, translate(Vec3{face + 0.07, hinge_y, z}),
                             materials_.bare_metal));
    }
    const double latch_y = -width * 0.5;
    for (const double z : {centre_z - 0.55, centre_z, centre_z + 0.55}) {
        parts_.push_back(box(Vec3{0.03, 0.09, 0.05}, translate(Vec3{face + 0.07, latch_y, z}),
                             materials_.bare_metal));
    }
    // The lever at chest height: lower, the seat back hides it from the pilot.
    const double pivot_y = latch_y + 0.13;
    const double lever_z = centre_z + 0.42;
    parts_.push_back(box(Vec3{0.05, 0.07, 0.07}, translate(Vec3{face + 0.08, pivot_y, lever_z}),
                         materials_.bare_metal));
    parts_.push_back(box(Vec3{0.035, 0.24, 0.04}, translate(Vec3{face + 0.11, pivot_y + 0.10, lever_z}),
                         materials_.machined_trim));

    // Over the door, the seal state: green lit, red dark. Scenery like the rest
    // of this wall -- the door never opens, so there is no state to read.
    parts_.push_back(box(Vec3{0.03, 0.30, 0.09}, translate(Vec3{face + 0.015, 0.0, top + jamb + 0.08}),
                         materials_.dark_composite));
    parts_.push_back(box(Vec3{0.012, 0.10, 0.04}, translate(Vec3{face + 0.035, 0.07, top + jamb + 0.08}),
                         ShipMaterials::emissive(palette::OK, 0.8)));
    parts_.push_back(box(Vec3{0.012, 0.10, 0.04}, translate(Vec3{face + 0.035, -0.07, top + jamb + 0.08}),
                         materials_.indicator_off));
}

void CockpitInterior::build_aft_instruments(double face) {
    // Two auxiliary panels between the door frame and the side rails. They are
    // passive: the flight is flown from the front panel (rule 24), and these
    // only make the back of the cabin read as part of a working ship.
    constexpr double panel_y = 0.71;
    constexpr double panel_width = 0.42;
    constexpr double panel_z = 0.18;
    for (const double side : {-1.0, 1.0}) {
        parts_.push_back(box(Vec3{0.04, panel_width, 0.86}, translate(Vec3{face + 0.02, side * panel_y, panel_z}),
                             materials_.dark_composite));
    }
    const double front = face + 0.04;

    // Environment panel: four square gauges, each a bezel, a dim face, a zero
    // mark and a needle.
    for (int i = 0; i < 4; ++i) {
        const double y = panel_y + (i % 2 == 0 ? -0.095 : 0.095);
        const double z = panel_z + (i < 2 ? 0.25 : 0.06);
        parts_.push_back(box(Vec3{0.015, 0.16, 0.16}, translate(Vec3{front + 0.0075, y, z}),
                             materials_.display_glass));
        parts_.push_back(box(Vec3{0.006, 0.13, 0.13}, translate(Vec3{front + 0.018, y, z}),
                             ShipMaterials::emissive(palette::BACKGROUND.lightened(0.08F), 0.35)));
        parts_.push_back(box(Vec3{0.004, 0.008, 0.02}, translate(Vec3{front + 0.022, y, z + 0.052}),
                             ShipMaterials::emissive(palette::NAV, 0.6)));
        const double angle = -55.0 + 38.0 * i;
        parts_.push_back(box(Vec3{0.004, 0.007, 0.05},
                             Transform3{Basis::from_euler_degrees(Vec3{angle, 0.0, 0.0}), Vec3{front + 0.024, y, z}} *
                                 translate(Vec3{0.0, 0.0, 0.022}),
                             ShipMaterials::emissive(palette::PRIMARY, 0.7)));
    }
    for (int i = 0; i < 3; ++i) {
        parts_.push_back(box(Vec3{0.012, 0.06, 0.03},
                             translate(Vec3{front + 0.006, panel_y - 0.12 + i * 0.12, panel_z - 0.13}),
                             i == 0 ? ShipMaterials::emissive(palette::OK, 0.6) : materials_.indicator_off));
    }
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 5; ++column) {
            parts_.push_back(box(Vec3{0.02, 0.04, 0.04},
                                 translate(Vec3{front + 0.01, panel_y - 0.16 + column * 0.08,
                                                panel_z - 0.24 - row * 0.08}),
                                 materials_.bare_metal));
        }
    }

    // Power and communications panel: a status screen with a few lines of
    // "text", then a row of toggle switches and one of breakers.
    const double y = -panel_y;
    parts_.push_back(box(Vec3{0.015, 0.34, 0.22}, translate(Vec3{front + 0.0075, y, panel_z + 0.22}),
                         materials_.display_glass));
    parts_.push_back(box(Vec3{0.006, 0.30, 0.18}, translate(Vec3{front + 0.018, y, panel_z + 0.22}),
                         ShipMaterials::emissive(palette::DISPLAY_GLOW, 0.5)));
    const std::array<double, 5> lines = {0.22, 0.16, 0.24, 0.12, 0.19};
    for (std::size_t i = 0; i < lines.size(); ++i) {
        parts_.push_back(box(Vec3{0.004, lines[i], 0.012},
                             translate(Vec3{front + 0.022, y - 0.12 + lines[i] * 0.5,
                                            panel_z + 0.29 - static_cast<double>(i) * 0.032}),
                             ShipMaterials::emissive(i == 0 ? palette::OK : palette::NAV, 0.7)));
    }
    for (int column = 0; column < 4; ++column) {
        const double sy = y - 0.135 + column * 0.09;
        parts_.push_back(box(Vec3{0.015, 0.05, 0.07}, translate(Vec3{front + 0.0075, sy, panel_z - 0.02}),
                             materials_.bare_metal));
        parts_.push_back(box(Vec3{0.045, 0.014, 0.014},
                             Transform3{Basis::from_euler_degrees(Vec3{0.0, column == 2 ? 25.0 : -25.0, 0.0}),
                                        Vec3{front + 0.03, sy, panel_z - 0.02}},
                             materials_.machined_trim));
        parts_.push_back(box(Vec3{0.012, 0.035, 0.018}, translate(Vec3{front + 0.006, sy, panel_z + 0.04}),
                             column == 2 ? materials_.indicator_off : ShipMaterials::emissive(palette::OK, 0.5)));
    }
    for (int column = 0; column < 5; ++column) {
        parts_.push_back(box(Vec3{0.02, 0.04, 0.04},
                             translate(Vec3{front + 0.01, y - 0.16 + column * 0.08, panel_z - 0.18}),
                             materials_.bare_metal));
    }
}

void CockpitInterior::build_windows() {
    // Three openings between four posts, and the posts stand on the SHARED
    // corners: the difference between a window and a set of bars.
    const std::array<Vec2, 4> line = {
        Vec2{CANOPY_ROOT_X, CABIN_HALF_WIDTH},
        Vec2{CANOPY_NOSE_X, CANOPY_NOSE_HALF_WIDTH},
        Vec2{CANOPY_NOSE_X, -CANOPY_NOSE_HALF_WIDTH},
        Vec2{CANOPY_ROOT_X, -CABIN_HALF_WIDTH},
    };
    std::vector<Vec2> normals;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        normals.push_back(outward(line[i], line[i + 1]));
        build_bay(line[i], line[i + 1], static_cast<int>(i));
    }
    // Each post stands on the BISECTOR of the two panes it separates -- what a
    // mitred profile joining two sheets at an angle does. At the ends, where
    // there is only one pane, the bisector is that pane's normal.
    for (std::size_t i = 0; i < line.size(); ++i) {
        const Vec2 before = normals[i == 0 ? 0 : i - 1];
        const Vec2 after = normals[std::min(i, normals.size() - 1)];
        build_post(line[i], (before + after).normalized());
    }
    build_glareshield();
}

void CockpitInterior::build_bay(Vec2 a, Vec2 b, int index) {
    // An opening: the skin above and below, the reveal of the hole, the seal and
    // the glass. All in the opening's OWN coordinates -- the front and the two
    // side openings run the same code, which is why all three end at the same
    // height.
    const Vec2 mid = (a + b) * 0.5;
    const double span = a.distance_to(b);
    const Vec2 opening{span, HEAD_Z - SILL_Z};
    const double centre_z = (HEAD_Z + SILL_Z) * 0.5;
    const Transform3 bay{facing(outward(a, b)), Vec3{mid.x, mid.y, 0.0}};

    // Head (from the top of the opening to the ceiling) and sill (from the floor
    // to the opening). They are the nose's skin, and it is what closes the cabin.
    for (const auto& band : {Vec2{HEAD_Z, CEILING_Z}, Vec2{FLOOR_Z, SILL_Z}}) {
        parts_.push_back(box(Vec3{span, band.y - band.x, SKIN},
                             bay * translate(Vec3{0.0, (band.x + band.y) * 0.5, -SKIN * 0.5}),
                             materials_.cockpit_panel));
    }

    // The wall of the hole, set back into the cabin: what a hull with a thickness
    // shows, and the only piece here that produces a face in shadow next to a lit
    // one -- which is what the eye reads as depth.
    ring(bay, opening, REVEAL, REVEAL_DEPTH, centre_z, 0.015 - REVEAL_DEPTH * 0.5, materials_.frame_alloy);

    const Vec2 pane{span - REVEAL * 2.0, HEAD_Z - SILL_Z - REVEAL * 2.0};
    Part glass{};
    glass.mesh = meshes_.quad(pane);
    glass.transform = bay * translate(Vec3{0.0, centre_z, 0.010});
    glass.material = materials_.glass;
    parts_.push_back(glass);

    // The seal over the glass's edge: 28 mm of black elastomer between glass and
    // metal. The smallest piece in the cabin and the one that most decides
    // whether it reads as a pane seated in a hull or a rectangle painted on the
    // frame.
    ring(bay, pane, 0.028, 0.018, centre_z, 0.016, materials_.window_seal);

    bolt_ring(bay, opening, centre_z, index);
}

void CockpitInterior::bolt_ring(const Transform3& bay, Vec2 opening, double centre_z, int index) {
    // The retaining ring's bolts, round the opening, merged into one mesh:
    // eighty draws for something the eye reads as a texture would be eighty
    // draws too many. They are worth it: the detail that says "this was
    // assembled".
    const double inset = REVEAL * 0.5;
    const Vec2 half{opening.x * 0.5 - inset, opening.y * 0.5 - inset};
    const double spacing = 0.125;
    std::vector<Vec2> places;
    const int across = static_cast<int>(std::round(opening.x / spacing));
    for (int i = 0; i <= across; ++i) {
        const double x = -half.x + half.x * 2.0 * i / across;
        places.push_back(Vec2{x, half.y});
        places.push_back(Vec2{x, -half.y});
    }
    const int down = static_cast<int>(std::round(opening.y / spacing));
    for (int i = 1; i < down; ++i) {
        const double y = -half.y + half.y * 2.0 * i / down;
        places.push_back(Vec2{half.x, y});
        places.push_back(Vec2{-half.x, y});
    }

    // The head grows along its local Y; here it has to come out of the ring
    // towards the PILOT, along the opening's local -Z.
    //
    // ⚠️ The first version put the heads on the outer face (+Z) and from the
    // pilot's seat nothing showed: a twelve-millimetre bolt seen edge-on, a metre
    // and a half away and against the Earth, is zero pixels. A retaining ring is
    // bolted from inside -- which is also where anyone would reach it.
    const Basis facing_in{Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 0.0, 1.0}, Vec3{0.0, -1.0, 0.0}};
    std::vector<Transform3> transforms;
    for (const auto& place : places) {
        transforms.push_back(bay * Transform3{facing_in, Vec3{place.x, centre_z + place.y,
                                                              -REVEAL_DEPTH * 0.5 - 0.035}});
    }
    const MeshData head = mesh::cylinder(0.009, 0.011, 0.010, 8, 1, true, true);
    Part bolts{};
    bolts.mesh = meshes_.add("cabin:bolts:" + std::to_string(index), mesh::instanced(head, transforms));
    bolts.material = materials_.machined_trim;
    parts_.push_back(bolts);
}

void CockpitInterior::ring(const Transform3& parent, Vec2 opening, double thickness, double depth, double centre_z,
                           double at_z, const Material& material) {
    // Four pieces forming a hollow rectangle `thickness` wide, in the parent's
    // local plane, with the outer opening `opening`.
    const Vec2 inner{opening.x - thickness * 2.0, opening.y - thickness * 2.0};
    struct Piece {
        Vec3 at;
        Vec3 size;
    };
    const Piece pieces[] = {
        {{0.0, (opening.y - thickness) * 0.5, 0.0}, {opening.x, thickness, depth}},
        {{0.0, -(opening.y - thickness) * 0.5, 0.0}, {opening.x, thickness, depth}},
        {{(opening.x - thickness) * 0.5, 0.0, 0.0}, {thickness, inner.y, depth}},
        {{-(opening.x - thickness) * 0.5, 0.0, 0.0}, {thickness, inner.y, depth}},
    };
    for (const auto& piece : pieces) {
        parts_.push_back(box(piece.size, parent * translate(piece.at + Vec3{0.0, centre_z, at_z}), material));
    }
}

void CockpitInterior::build_post(Vec2 at, Vec2 outward_normal) {
    // A post: the structural profile between two openings, sill to head, with a
    // centimetre of slack at each end so no line of light shows where the parts
    // meet.
    const Transform3 post{facing(outward_normal), Vec3{at.x, at.y, 0.0}};
    const double height = HEAD_Z - SILL_Z + 0.09;
    const double centre = (HEAD_Z + SILL_Z) * 0.5;
    parts_.push_back(box(Vec3{0.085, height, 0.15}, post * translate(Vec3{0.0, centre, -0.055}),
                         materials_.frame_alloy));
    // A sharp edge facing into the cabin. A smooth box lit from the front reads
    // as a plank; a narrow, more metallic corner catches the light in a thin
    // line, and that line is what makes the part read as a machined profile.
    parts_.push_back(box(Vec3{0.024, height, 0.024}, post * translate(Vec3{0.0, centre, -0.128}),
                         materials_.machined_trim));
}

void CockpitInterior::build_glareshield() {
    // The glare shield: the dark lip between the window and the panel. It keeps
    // the lit panel from reflecting in the glass, which is why it exists in real
    // capsules. It has to stay BELOW the line of sight from the eye to the bottom
    // edge of the front pane -- measured, not estimated.
    parts_.push_back(box(Vec3{0.32, CANOPY_NOSE_HALF_WIDTH * 2.4, 0.028},
                         Transform3{Basis::from_euler_degrees(Vec3{0.0, -22.0, 0.0}), Vec3{4.32, 0.0, -0.03}},
                         materials_.dark_composite));
    // A light strip on the front edge: where the panel's light dies.
    parts_.push_back(box(Vec3{0.022, CANOPY_NOSE_HALF_WIDTH * 2.4, 0.022}, translate(Vec3{4.465, 0.0, -0.088}),
                         materials_.machined_trim));
}

void CockpitInterior::build_panel() {
    // The panel's normal points at the pilot's eye, computed and not estimated: a
    // panel tilted "by taste" reads sideways exactly from where it is read.
    const Vec3 normal = (EYE - PANEL_CENTRE).normalized();
    const Vec3 across{0.0, -1.0, 0.0};   // right, from the pilot's point of view
    const Vec3 up = cross(normal, across).normalized();
    panel_ = Transform3{Basis{across, up, normal}, PANEL_CENTRE};

    parts_.push_back(box(Vec3{PANEL_HALF_WIDTH * 2.0 + 0.08, 1.16, 0.05}, panel_ * translate(Vec3{0.0, -0.09, -0.028}),
                         materials_.cockpit_panel));

    mount_display("flight", Vec3{-0.620, 0.210, 0.0}, Vec2{0.58, 0.380}, SMALL_DISPLAY_W, SMALL_DISPLAY_H);
    mount_display("nav", Vec3{0.0, 0.210, 0.0}, Vec2{0.58, 0.380}, SMALL_DISPLAY_W, SMALL_DISPLAY_H);
    mount_display("target", Vec3{0.620, 0.210, 0.0}, Vec2{0.58, 0.380}, SMALL_DISPLAY_W, SMALL_DISPLAY_H);
    mount_display("system", Vec3{0.0, -0.050, 0.0}, Vec2{1.84, 0.175}, WIDE_DISPLAY_W, WIDE_DISPLAY_H);

    build_button_strip();
    build_indicators();
}

void CockpitInterior::mount_display(const std::string& name, const Vec3& at, Vec2 physical, int width, int height) {
    // Ten millimetres in front of the panel, and the number matters: the bezel
    // below is a BOX 12 mm thick centred on z = 0, so it occupies -6 to +6 mm. A
    // screen at +4 mm is INSIDE the box and the bezel draws over it.
    const auto index = displays_.size();
    CockpitDisplay display{name, panel_ * translate(at + Vec3{0.0, 0.0, 0.010}), physical, width, height};
    displays_.push_back(display);

    Part screen{};
    screen.mesh = meshes_.quad(physical);
    screen.transform = display.transform;
    // Unshaded, with the display's own image: a display lights, and that light is
    // what rule 51 asks to exist without washing out the rest of the cabin.
    screen.material.unshaded = true;
    screen.material.dynamic_texture = "display:" + std::to_string(index);
    screen.material.casts_shadow = false;
    parts_.push_back(screen);

    // The bezel of the display's glass: gives the panel an edge, and is what tells
    // "a display" from "a picture stuck on".
    parts_.push_back(box(Vec3{physical.x + 0.045, physical.y + 0.045, 0.012}, panel_ * translate(at),
                         materials_.display_glass));
}

void CockpitInterior::build_button_strip() {
    // Seven buttons, and each does exactly one thing (rule 24). The list is
    // filled by the orchestrator, which knows what each one commands; here there
    // are only the positions.
    const double width = 0.24;
    const double gap = 0.265;
    for (int i = 0; i < 7; ++i) {
        auto placeholder = CockpitControl::button("", Vec3{-gap * 3.0 + gap * i, -0.152, 0.010}, width, {});
        placeholder.transform = panel_ * placeholder.transform;
        placeholder.enabled = false;
        controls_.push_back(std::move(placeholder));
    }
}

void CockpitInterior::build_indicators() {
    // Four lamps on the glare shield's lip, in the field of view without being in
    // the way. Short names, because a lamp that needs a sentence is not a lamp.
    const std::array<const char*, 4> names = {"MSTR", "RCS", "ENG", "AP"};
    const std::array<Colour, 4> colours = {palette::CRITICAL, palette::NAV, palette::ENGINE, palette::PLAN};
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto lamp = CockpitControl::indicator(names[i], Vec3{-0.42 + static_cast<double>(i) * 0.28, 0.425, 0.008},
                                              colours[i]);
        lamp.transform = panel_ * lamp.transform;
        indicators_[names[i]] = controls_.size();
        controls_.push_back(std::move(lamp));
    }
}

void CockpitInterior::build_side_consoles() {
    for (const double side : {1.0, -1.0}) {
        parts_.push_back(box(Vec3{0.80, 0.30, 0.09},
                             Transform3{Basis::from_euler_degrees(Vec3{side * 16.0, 0.0, 0.0}),
                                        Vec3{3.62, side * 0.80, -0.62}},
                             materials_.cockpit_panel));
        for (int i = 0; i < 4; ++i) {
            Part knob{};
            knob.mesh = meshes_.cylinder(0.028, 0.034, 0.035, 12, 1, true, true);
            knob.transform = translate(Vec3{3.40 + i * 0.16, side * 0.80, -0.575});
            knob.material = materials_.bare_metal;
            parts_.push_back(knob);
        }
    }
    // Right-hand side stick. Not interactive: the keyboard flies, and a stick
    // that moved without commanding anything would be decoration pretending to
    // be an instrument.
    parts_.push_back(box(Vec3{0.05, 0.05, 0.18}, translate(Vec3{3.30, -0.76, -0.47}), materials_.dark_composite));
    Part grip{};
    grip.mesh = meshes_.sphere(0.048, 0.096, 16, 8);
    grip.transform = translate(Vec3{3.30, -0.76, -0.38});
    grip.material = materials_.dark_composite;
    parts_.push_back(grip);
}

void CockpitInterior::build_seat() {
    // Only what is seen of the seat from where one sits: the armrests and the
    // edge of the back. Modelling the whole chair would draw geometry inside the
    // camera.
    for (const double side : {1.0, -1.0}) {
        parts_.push_back(box(Vec3{0.52, 0.09, 0.06}, translate(Vec3{3.02, side * 0.42, -0.14}),
                             materials_.dark_composite));
    }
    parts_.push_back(box(Vec3{0.07, 0.62, 0.50}, translate(Vec3{AFT_X + 0.24, 0.0, 0.02}), materials_.dark_composite));
}

void CockpitInterior::build_lighting() {
    // A weak flood from behind the pilot, and a grazing white light over the
    // panel. Two, not five: what makes the cabin legible is the contrast between
    // the lit panel and the half-dark, and more light destroys exactly that
    // (rule 51).
    //
    // ⚠️ Almost white, and not the colour of an incandescent lamp. At a 2700 K
    // amber this light painted everything it reached light brown, and the capture
    // showed WOODEN posts. The colour of an interior light is not a detail of
    // ambience: it decides what material the player thinks the cabin is made of.
    lights_.push_back(PointLight{Vec3{2.85, 0.0, CEILING_Z - 0.16}, Colour{0.98F, 0.96F, 0.93F}, 0.75, 3.6});
    // Under the glare shield and aimed at the panel, as in real capsules. Wide
    // range and low energy: a strong, close light makes a halo in the middle of
    // the panel, which is what the first capture showed.
    lights_.push_back(PointLight{Vec3{CANOPY_NOSE_X - 0.24, 0.0, 0.02}, Colour{0.82F, 0.89F, 1.0F}, 0.30, 4.5});
}

std::vector<Part> CockpitInterior::control_parts() const {
    std::vector<Part> out;
    for (std::size_t i = 0; i < controls_.size(); ++i) {
        const auto& control = controls_[i];
        Part face{};
        face.mesh = meshes_.quad(control.half_size * 2.0);
        face.transform = control.transform;
        face.material = ShipMaterials::emissive(control.face_colour(), control.face_energy());
        face.material.casts_shadow = false;
        out.push_back(face);

        // The label, drawn as text and not baked into a texture file (rule 44):
        // the text stays sharp at any resolution and can be translated.
        if (!control.caption_text().empty()) {
            Part caption{};
            caption.mesh = meshes_.quad(control.half_size * 2.0);
            caption.transform = caption_transform(i);
            caption.material.unshaded = true;
            caption.material.blend = Blend::Alpha;
            caption.material.dynamic_texture = "caption:" + std::to_string(i);
            caption.material.casts_shadow = false;
            out.push_back(caption);
        }
    }
    return out;
}

Transform3 CockpitInterior::caption_transform(std::size_t index) const {
    return controls_[index].transform * translate(Vec3{0.0, 0.0, 0.002});
}

int CockpitInterior::pick(const Vec3& origin, const Vec3& direction) const {
    for (std::size_t i = 0; i < controls_.size(); ++i) {
        if (controls_[i].hit_test(origin, direction)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void CockpitInterior::set_hover(int index) {
    if (hovered_ == index) {
        return;
    }
    if (hovered_ >= 0 && hovered_ < static_cast<int>(controls_.size())) {
        controls_[static_cast<std::size_t>(hovered_)].set_hovered(false);
    }
    hovered_ = index;
    if (hovered_ >= 0 && hovered_ < static_cast<int>(controls_.size())) {
        controls_[static_cast<std::size_t>(hovered_)].set_hovered(true);
    }
}

CockpitControl& CockpitInterior::configure_button(std::size_t index, const std::string& label,
                                                  std::function<void(CockpitControl&)> callback, bool is_switch,
                                                  bool initial) {
    auto& control = controls_[index];
    control.label = label;
    control.kind = is_switch ? CockpitControl::Kind::Switch : CockpitControl::Kind::Button;
    control.on = initial;
    control.enabled = true;
    control.pressed_callback = std::move(callback);
    control.set_caption("");
    return control;
}

void CockpitInterior::set_indicator(const std::string& name, bool lit) {
    if (const auto it = indicators_.find(name); it != indicators_.end()) {
        controls_[it->second].set_on(lit);
    }
}

void CockpitInterior::advance(double delta) {
    for (auto& control : controls_) {
        control.advance(delta);
    }
}

}  // namespace sf::app
