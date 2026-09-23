#include "app/presentation/scene/spacecraft_visual.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sf::app {
namespace {

constexpr double kPi = std::numbers::pi;

Transform3 at(const Vec3& position, const Vec3& euler_degrees = Vec3{}, const Vec3& scale = Vec3{1.0, 1.0, 1.0}) {
    return Transform3{Basis::from_euler_degrees(euler_degrees).scaled_local(scale), position};
}

}  // namespace

// --- SpacecraftVisual -----------------------------------------------------------

SpacecraftVisual::SpacecraftVisual(MeshLibrary& meshes, const ShipMaterials& materials)
    : meshes_(meshes), materials_(materials) {
    build_command_module();
    build_habitat();
    build_truss();
    build_tanks();
    build_power_section();
    build_engine();
    build_radiators();
    build_antennas();
    build_rcs_pods();
    build_navigation_lights();
}

void SpacecraftVisual::advance(double delta) {
    light_phase_ = std::fmod(light_phase_ + delta, 2.0);
    const bool on = light_phase_ < 0.12 || (light_phase_ > 0.24 && light_phase_ < 0.36);
    for (const auto index : navigation_lights_) {
        parts_[index].visible = on;
    }
}

double SpacecraftVisual::bounding_radius() { return std::max(std::abs(ENGINE_EXIT), RADIATOR_SPAN) + 1.0; }

Part SpacecraftVisual::cylinder(double radius_aft, double radius_forward, double x_aft, double x_forward, bool caps,
                                const Material& material) {
    // A cylinder or cone lying along +x, between two stations.
    //
    // The mesh stands along +y with its top at +y, so "top" is the +x end after
    // the -90 degree turn about z. Written once, because a frustum drawn the
    // wrong way round still looks like a frustum.
    //
    // ⚠️ `caps` exists for a measured reason: the cockpit fairing's aft cap is a
    // 1.05 m disc 85 cm from the pilot's eye, and at the cockpit's field of view
    // it covers the whole screen. The first capture of Milestone 7 was a uniform
    // brown rectangle, and it was this: the pilot was looking at the inside of a
    // cap. The sections the camera can be INSIDE are built without caps.
    Part part{};
    part.mesh = meshes_.cylinder(radius_forward, radius_aft, std::abs(x_forward - x_aft), 24, 1, caps, caps);
    part.transform = at(Vec3{(x_forward + x_aft) * 0.5, 0.0, 0.0}, Vec3{0.0, 0.0, -90.0});
    part.material = material;
    return part;
}

Part SpacecraftVisual::box(const Vec3& size, const Vec3& position, const Material& material) {
    Part part{};
    part.mesh = meshes_.box(Vec3{std::abs(size.x), std::abs(size.y), std::abs(size.z)});
    part.transform = at(position);
    part.material = material;
    return part;
}

void SpacecraftVisual::build_command_module() {
    // A frustum from the forward bulkhead to the base of the cockpit: the command
    // module narrows forward, which gives the ship a "nose" without giving it any
    // aerodynamics (rule 5).
    parts_.push_back(cylinder(CORE_RADIUS, 1.25, CORE_FORWARD - 1.4, CORE_FORWARD, false, materials_.hull_paint));
    parts_.push_back(cylinder(1.25, 1.05, CORE_FORWARD, NOSE_TIP - 1.0, false, materials_.dark_composite));

    // The cockpit fairing, drawn from outside: inside it is where CockpitInterior
    // builds the flight deck, and both use the same constants so that the window
    // of one is the window of the other.
    parts_.push_back(cylinder(1.05, 0.72, NOSE_TIP - 1.0, NOSE_TIP, false, materials_.hull_paint));

    // The front windows seen from outside: a dark band round the nose.
    Part band{};
    band.mesh = meshes_.cylinder(1.09, 1.19, 0.62, 24, 1, false, false);
    band.transform = at(Vec3{NOSE_TIP - 0.78, 0.0, 0.0}, Vec3{0.0, 0.0, 90.0});
    band.material = materials_.glass;
    parts_.push_back(band);

    // Docking ring on the command module's back (rule 5: docking).
    Part ring{};
    ring.mesh = meshes_.torus(0.52, 0.68, 20, 32);
    ring.transform = at(Vec3{2.0, 0.0, CORE_RADIUS + 0.1});
    ring.material = materials_.bare_metal;
    parts_.push_back(ring);
}

void SpacecraftVisual::build_habitat() {
    parts_.push_back(cylinder(CORE_RADIUS, CORE_RADIUS, CORE_AFT, CORE_FORWARD, false, materials_.hull_paint));

    // Three reinforcing hoops. A smooth nine-metre surface has no scale; the
    // hoops are what make the ship look the size it is.
    for (const double x : {-2.6, 0.0, 2.6}) {
        Part hoop{};
        hoop.mesh = meshes_.cylinder(CORE_RADIUS + 0.06, CORE_RADIUS + 0.06, 0.18, 24, 1, true, true);
        hoop.transform = at(Vec3{x, 0.0, 0.0}, Vec3{0.0, 0.0, 90.0});
        hoop.material = materials_.bare_metal;
        parts_.push_back(hoop);
    }

    // Side hatch and two habitat portholes.
    for (const auto& spec : {Vec2{0.4, 1.0}, Vec2{-1.8, -1.0}}) {
        Part port{};
        port.mesh = meshes_.cylinder(0.26, 0.26, 0.08, 16, 1, true, true);
        port.transform = at(Vec3{spec.x, spec.y * (CORE_RADIUS + 0.02), 0.4}, Vec3{90.0, 0.0, 0.0});
        port.material = materials_.glass;
        parts_.push_back(port);
    }

    // Thermal blanket over the aft third of the habitat.
    parts_.push_back(
        cylinder(CORE_RADIUS + 0.04, CORE_RADIUS + 0.04, CORE_AFT, CORE_AFT + 1.6, false, materials_.thermal_blanket));
}

void SpacecraftVisual::build_truss() {
    // Four longerons joining the pressurised hull to the propulsion section. A
    // truss and not a tube, because that is what is built where there is no air:
    // there is no aerodynamic load to fair.
    for (const auto& offset : {Vec3{0.0, 0.9, 0.9}, Vec3{0.0, -0.9, 0.9}, Vec3{0.0, 0.9, -0.9}, Vec3{0.0, -0.9, -0.9}}) {
        parts_.push_back(box(Vec3{POWER_AFT - CORE_AFT, 0.16, 0.16},
                             Vec3{(CORE_AFT + POWER_AFT) * 0.5, offset.y, offset.z}, materials_.bare_metal));
    }
    for (const double x : {-5.5, -8.0, -10.5}) {
        for (const double z : {0.9, -0.9}) {
            parts_.push_back(box(Vec3{0.10, 1.8, 0.10}, Vec3{x, 0.0, z}, materials_.bare_metal));
        }
    }
}

void SpacecraftVisual::build_tanks() {
    // ⚠️ The Godot version of this function set each barrel's position TWICE --
    // once to the middle of its span, then again to (0, y, 0) -- and the second
    // won: the barrels, their blankets and their feed lines were drawn at the
    // ship's centre, while the domes that close them sat six metres aft where the
    // constants put them. Here every piece is where TANK_FORWARD and TANK_AFT
    // say, which is also where the manual's labels point.
    for (const double side : {1.0, -1.0}) {
        Part barrel = cylinder(TANK_RADIUS, TANK_RADIUS, TANK_AFT + 0.9, TANK_FORWARD - 0.9, true, materials_.hull_paint);
        barrel.transform.origin.y = side * TANK_OFFSET;
        parts_.push_back(barrel);

        // Hemispherical domes: a pressure tank has no flat lid, and the rounded
        // silhouette is half of what makes the part read as a tank.
        for (const double cap_x : {TANK_FORWARD - 0.9, TANK_AFT + 0.9}) {
            Part cap{};
            cap.mesh = meshes_.sphere(TANK_RADIUS, TANK_RADIUS * 2.0, 20, 10);
            cap.transform = at(Vec3{cap_x, side * TANK_OFFSET, 0.0}, Vec3{}, Vec3{0.55, 1.0, 1.0});
            cap.material = materials_.hull_paint;
            parts_.push_back(cap);
        }

        // A band of thermal blanket round the middle of each tank.
        Part wrap = cylinder(TANK_RADIUS + 0.05, TANK_RADIUS + 0.05, -8.4, -6.6, true, materials_.thermal_blanket);
        wrap.transform.origin.y = side * TANK_OFFSET;
        parts_.push_back(wrap);

        // Feed line to the power section.
        Part feed = cylinder(0.14, 0.14, POWER_AFT, TANK_AFT, true, materials_.bare_metal);
        feed.transform.origin.y = side * TANK_OFFSET * 0.45;
        feed.transform.origin.z = -0.7;
        parts_.push_back(feed);
    }
}

void SpacecraftVisual::build_power_section() {
    // The reactor drum, with a shadow shield facing the crew: the disc on its
    // forward face is the reason the habitat is ten metres away, and drawing it
    // makes that reason visible.
    parts_.push_back(cylinder(1.3, 1.3, POWER_AFT, POWER_FORWARD, true, materials_.dark_composite));
    parts_.push_back(cylinder(1.85, 1.85, POWER_FORWARD - 0.05, POWER_FORWARD + 0.25, true, materials_.bare_metal));

    // Six radial fins. ⚠️ The Godot version turned each fin's ORIENTATION about
    // x and left its POSITION at the top of the drum, so all six were stacked in
    // one place; here the whole fin turns, and they stand round the drum.
    for (int angle = 0; angle < 360; angle += 60) {
        const Basis turn = Basis::from_axis_angle(Vec3::unit_x(), angle * kPi / 180.0);
        Part fin = box(Vec3{1.8, 0.08, 0.9}, Vec3{(POWER_AFT + POWER_FORWARD) * 0.5, 0.0, 1.7}, materials_.radiator);
        fin.transform = Transform3{turn, Vec3{}} * fin.transform;
        parts_.push_back(fin);
    }
}

void SpacecraftVisual::build_engine() {
    parts_.push_back(cylinder(0.95, 0.85, ENGINE_THROAT, POWER_AFT, true, materials_.bare_metal));

    // The bell: narrow at the throat, open at the exit. ⚠️ The Godot version
    // passed the two radii the other way round and drew the bell with its mouth
    // at the throat, inside a lip two metres wide.
    parts_.push_back(cylinder(BELL_EXIT_RADIUS, 0.85, ENGINE_EXIT, ENGINE_THROAT, true, materials_.engine_bell));

    Part lip{};
    lip.mesh = meshes_.torus(BELL_EXIT_RADIUS - 0.10, BELL_EXIT_RADIUS + 0.04, 24, 32);
    lip.transform = at(Vec3{ENGINE_EXIT, 0.0, 0.0}, Vec3{0.0, 0.0, 90.0});
    lip.material = materials_.bare_metal;
    parts_.push_back(lip);
}

void SpacecraftVisual::build_radiators() {
    // Two panels, up and down, in the shield's shadow and away from the windows.
    // Thin -- 6 cm -- because a radiator is a surface, and drawing it as a thick
    // plate is the difference between a ship and a toy.
    for (const double side : {1.0, -1.0}) {
        const double z = side * (1.6 + (RADIATOR_SPAN - 1.6) * 0.5);
        parts_.push_back(box(Vec3{6.6, 0.06, RADIATOR_SPAN - 1.6}, Vec3{-7.6, 0.0, z}, materials_.radiator));
        for (const double x : {-10.2, -8.6, -7.0, -5.4}) {
            parts_.push_back(box(Vec3{0.10, 0.10, RADIATOR_SPAN - 1.6}, Vec3{x, 0.0, z}, materials_.bare_metal));
        }
        parts_.push_back(box(Vec3{6.8, 0.14, 0.14}, Vec3{-7.6, 0.0, side * 1.7}, materials_.bare_metal));
    }
}

void SpacecraftVisual::build_antennas() {
    // High-gain antenna: the dish and its mast. Pointed forward and up, and
    // STATIC -- a dish tracking the Earth would be a claim about a communications
    // system that does not exist in the core.
    Part mast{};
    mast.mesh = meshes_.cylinder(0.07, 0.07, 1.5, 24, 1, true, true);
    mast.transform = Transform3{Basis::from_euler_degrees(Vec3{0.0, -55.0, 0.0}) *
                                    Basis::from_euler_degrees(Vec3{0.0, 0.0, -90.0}),
                                Vec3{1.6, 0.0, CORE_RADIUS}};
    mast.material = materials_.bare_metal;
    parts_.push_back(mast);

    Part dish{};
    dish.mesh = meshes_.cylinder(1.15, 0.12, 0.34, 24, 1, true, true);
    dish.transform = at(Vec3{2.55, 0.0, CORE_RADIUS + 1.35}, Vec3{0.0, 0.0, -55.0});
    dish.material = materials_.hull_paint;
    parts_.push_back(dish);

    // Two low-gain antennas, one each side.
    for (const double side : {1.0, -1.0}) {
        Part whip{};
        whip.mesh = meshes_.cylinder(0.02, 0.035, 1.1, 24, 1, true, true);
        whip.transform = at(Vec3{-1.2, side * CORE_RADIUS, 0.6}, Vec3{0.0, 0.0, side * 62.0});
        whip.material = materials_.bare_metal;
        parts_.push_back(whip);
    }

    // Auxiliary solar panel: the ship is nuclear, but the emergency load is not.
    for (const double side : {1.0, -1.0}) {
        parts_.push_back(box(Vec3{2.4, 0.05, 1.3}, Vec3{-2.4, side * 2.6, 0.0}, materials_.solar_cell));
        parts_.push_back(box(Vec3{0.10, 1.2, 0.10}, Vec3{-2.4, side * 1.9, 0.0}, materials_.bare_metal));
    }
}

void SpacecraftVisual::build_rcs_pods() {
    // The nacelles go to the six places RcsSystem::couples(2.0) puts the
    // thrusters. If the arm changes in the core, the ship changes with it -- the
    // constant here is the same constant.
    for (const auto& axis : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0}, Vec3{0, 0, 1}, Vec3{0, 0, -1}}) {
        const Vec3 mount = axis * RCS_ARM;
        // The pod sits on the surface: those on +-y and +-z on the cylindrical
        // hull, those on +-x at the waist, and all stand far enough from the axis
        // for the nozzle to be outside.
        parts_.push_back(box(Vec3{0.62, 0.62, 0.42}, mount + axis * 0.18, materials_.dark_composite));
    }
}

void SpacecraftVisual::build_navigation_lights() {
    struct Spec {
        Vec3 at;
        Colour colour;
    };
    for (const auto& spec : {Spec{{3.2, CORE_RADIUS + 0.1, 0.9}, palette::OK},
                             Spec{{3.2, -CORE_RADIUS - 0.1, 0.9}, palette::CRITICAL},
                             Spec{{-11.0, 0.0, 1.5}, palette::PRIMARY}}) {
        Part light{};
        light.mesh = meshes_.sphere(0.09, 0.18, 8, 4);
        light.transform = at(spec.at);
        light.material = ShipMaterials::emissive(spec.colour, 3.0);
        light.material.casts_shadow = false;
        navigation_lights_.push_back(parts_.size());
        parts_.push_back(light);
    }
}

// --- EnginePlume ------------------------------------------------------------------

EnginePlume::EnginePlume(MeshLibrary& meshes) {
    // One metre of mesh, stretched by `stretch`. A quarter turn about z takes +y
    // to -x, which is where the exhaust goes; the TOP of the mesh is therefore at
    // the far end from the nozzle, and it is the top that has to carry the larger
    // radius -- an exhaust expands as it leaves.
    cone_mesh_ = meshes.cylinder(1.0, 0.35, 1.0, 20, 1, true, true);
    core_mesh_ = meshes.cylinder(0.9, 0.12, 1.0, 20, 1, true, true);
    shock_mesh_ = meshes.sphere(0.75, 1.5, 16, 8);
}

void EnginePlume::set_thrust(double thrust_n) { set_intensity(std::clamp(thrust_n / IMPULSE_REFERENCE_N, 0.0, 1.0)); }

void EnginePlume::set_intensity(double value) { intensity_ = std::clamp(value, 0.0, 1.0); }

void EnginePlume::advance(double delta) {
    if (!visible()) {
        return;
    }
    // Flicker of +-4 %. Without it the plume is a solid cone and reads as
    // geometry; with more it becomes a campfire. The value feeds NOTHING back:
    // it is a drawing scale, and the thrust the simulation uses stays constant.
    flicker_ = std::fmod(flicker_ + delta * 11.0, 2.0 * kPi);
}

Colour EnginePlume::glow(Colour colour, double level) {
    const auto l = static_cast<float>(level);
    return Colour{colour.r * l, colour.g * l, colour.b * l, l};
}

Transform3 EnginePlume::stretch(double length, double width) {
    // ⚠️ A node's scale is applied BEFORE its rotation: basis = R * S. The mesh
    // grows along +y, so what LENGTHENS it is the y scale. Scaling x widened it
    // across instead -- the full plume was a disc 26 m wide by 2.6 m long.
    return Transform3{Basis::from_euler_degrees(Vec3{0.0, 0.0, 90.0}).scaled_local(Vec3{width, length, width}),
                      Vec3{-length * 0.5, 0.0, 0.0}};
}

Transform3 EnginePlume::cone_transform() const {
    const double jitter = 1.0 + 0.04 * std::sin(flicker_) + 0.02 * std::sin(flicker_ * 2.7);
    return stretch(MAX_LENGTH * std::sqrt(intensity_) * jitter, 1.0 + 0.7 * intensity_);
}

std::vector<Part> EnginePlume::parts() const {
    if (!visible()) {
        return {};
    }
    // Square root and not linear: half the thrust does not give half the length
    // in any engine, and the root is what makes a throttle at 10 % still show
    // something rather than nothing.
    const double length = MAX_LENGTH * std::sqrt(intensity_);
    const double width = 1.0 + 0.7 * intensity_;

    Part cone{};
    cone.mesh = cone_mesh_;
    cone.transform = cone_transform();
    cone.material = ShipMaterials::additive(glow(palette::ENGINE, 0.10 + 0.30 * intensity_));

    Part core{};
    core.mesh = core_mesh_;
    core.transform = stretch(length * CORE_FRACTION, width * 0.55);
    core.material = ShipMaterials::additive(glow(Colour{1.0F, 0.97F, 0.92F}, 0.20 + 0.40 * intensity_));

    Part shock{};
    shock.mesh = shock_mesh_;
    shock.transform = Transform3{Basis::diagonal(Vec3{0.5, width * 0.5, width * 0.5}), Vec3{-length * 0.13, 0.0, 0.0}};
    shock.material = ShipMaterials::additive(glow(Colour{0.85F, 0.92F, 1.0F}, 0.10 + 0.24 * intensity_));

    return {cone, core, shock};
}

PointLight EnginePlume::light() const {
    // The light lives in the MIDDLE of the plume and not at the nozzle: that is
    // how it lights the hull from behind, which is what makes the plume read as
    // being behind the ship.
    const double length = MAX_LENGTH * std::sqrt(intensity_);
    return PointLight{Vec3{-length * 0.35, 0.0, 0.0}, palette::ENGINE, visible() ? 3.5 * intensity_ : 0.0, 34.0};
}

// --- RcsVisual --------------------------------------------------------------------

RcsVisual::RcsVisual(MeshLibrary& meshes) {
    jet_mesh_ = meshes.cylinder(JET_RADIUS, JET_RADIUS * 0.22, JET_LENGTH, 10, 1, true, true);
}

void RcsVisual::build(const std::vector<ThrusterView>& thrusters) {
    jets_.clear();
    for (const auto& spec : thrusters) {
        // The exhaust goes where the force does NOT. Third law, and the only line
        // in this file that needs it.
        const Vec3 exhaust = (spec.force_direction * -1.0).normalized();
        Vec3 up{0.0, 1.0, 0.0};
        if (std::abs(dot(exhaust, up)) > 0.98) {
            up = Vec3{1.0, 0.0, 0.0};
        }
        // The mesh grows along +y; the jet has to grow along `exhaust`, from the
        // mouth of the nozzle. Looking along the exhaust puts -z there, and a
        // quarter turn about local x takes +y to -z.
        const Basis look = looking_at(exhaust, up).rotated_local(Vec3::unit_x(), -kPi * 0.5);
        jets_.push_back(Jet{Transform3{look, spec.position + exhaust * 0.3}, 0.0});
    }
}

void RcsVisual::set_throttles(const std::vector<double>& throttles) {
    throttles_ = throttles;
    for (std::size_t i = 0; i < jets_.size(); ++i) {
        jets_[i].open = i < throttles_.size() ? throttles_[i] : 0.0;
    }
}

int RcsVisual::firing_count() const {
    return static_cast<int>(
        std::count_if(throttles_.begin(), throttles_.end(), [](double value) { return value > THRESHOLD; }));
}

double RcsVisual::total_demand() const {
    double sum = 0.0;
    for (const double value : throttles_) {
        sum += value;
    }
    return sum;
}

std::vector<Part> RcsVisual::parts() const {
    std::vector<Part> out;
    for (const auto& jet : jets_) {
        if (!(jet.open > THRESHOLD)) {
            continue;
        }
        // The length follows the opening. A nozzle at 12 % is a pilot light and a
        // nozzle at 100 % is a flame, and the difference has to be legible
        // because it is what says what the allocator did.
        const double open = jet.open;
        Part part{};
        part.mesh = jet_mesh_;
        part.transform = Transform3{jet.base.basis.scaled_local(Vec3{0.55 + 0.45 * open, 0.35 + 0.65 * open,
                                                                     0.55 + 0.45 * open}),
                                    jet.base.origin};
        part.material = ShipMaterials::additive(palette::RCS.with_alpha(static_cast<float>(0.25 + 0.55 * open)));
        out.push_back(part);
    }
    return out;
}

}  // namespace sf::app
