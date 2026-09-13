#include "simulation_node.hpp"

#include "core/gravity/oblateness_gravity.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/conversions.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <exception>

namespace spaceflight_godot {
namespace {

using godot::D_METHOD;
using godot::Vector3;

// Every Godot-facing entry point funnels through this: exceptions must not cross
// the C ABI that GDExtension is built on.  A failure becomes a logged error and a
// false return, which is what a script can actually handle.
template <typename Fn>
bool guarded(std::string& error_slot, const char* what, Fn&& fn) {
    try {
        fn();
        return true;
    } catch (const std::exception& e) {
        error_slot = std::string{what} + ": " + e.what();
        godot::UtilityFunctions::push_error(godot::String{error_slot.c_str()});
        return false;
    } catch (...) {
        error_slot = std::string{what} + ": unknown error";
        godot::UtilityFunctions::push_error(godot::String{error_slot.c_str()});
        return false;
    }
}

Vector3 to_godot(const sf::render::RenderVec3& v) { return Vector3{v.x, v.y, v.z}; }

}  // namespace

SpaceflightSimulation::SpaceflightSimulation() = default;
SpaceflightSimulation::~SpaceflightSimulation() = default;

void SpaceflightSimulation::_bind_methods() {
    godot::ClassDB::bind_method(D_METHOD("configure", "kernel_directory", "epoch_utc"),
                                &SpaceflightSimulation::configure);
    godot::ClassDB::bind_method(D_METHOD("start_circular_orbit", "altitude_m", "inclination_deg"),
                                &SpaceflightSimulation::start_circular_orbit);

    godot::ClassDB::bind_method(D_METHOD("set_time_warp", "warp"),
                                &SpaceflightSimulation::set_time_warp);
    godot::ClassDB::bind_method(D_METHOD("get_time_warp"), &SpaceflightSimulation::get_time_warp);
    godot::ClassDB::bind_method(D_METHOD("advance", "wall_seconds"),
                                &SpaceflightSimulation::advance);

    godot::ClassDB::bind_method(D_METHOD("set_render_scale", "scale"),
                                &SpaceflightSimulation::set_render_scale);
    godot::ClassDB::bind_method(D_METHOD("get_render_scale"),
                                &SpaceflightSimulation::get_render_scale);
    godot::ClassDB::bind_method(D_METHOD("set_body_scale_exaggeration", "factor"),
                                &SpaceflightSimulation::set_body_scale_exaggeration);
    godot::ClassDB::bind_method(D_METHOD("focus_on_spacecraft"),
                                &SpaceflightSimulation::focus_on_spacecraft);
    godot::ClassDB::bind_method(D_METHOD("focus_on_body", "index"),
                                &SpaceflightSimulation::focus_on_body);

    godot::ClassDB::bind_method(D_METHOD("get_body_count"), &SpaceflightSimulation::get_body_count);
    godot::ClassDB::bind_method(D_METHOD("get_body_name", "index"),
                                &SpaceflightSimulation::get_body_name);
    godot::ClassDB::bind_method(D_METHOD("get_body_position", "index"),
                                &SpaceflightSimulation::get_body_position);
    godot::ClassDB::bind_method(D_METHOD("get_body_radius", "index"),
                                &SpaceflightSimulation::get_body_radius);
    godot::ClassDB::bind_method(D_METHOD("get_spacecraft_position"),
                                &SpaceflightSimulation::get_spacecraft_position);
    godot::ClassDB::bind_method(D_METHOD("get_spacecraft_velocity_direction"),
                                &SpaceflightSimulation::get_spacecraft_velocity_direction);
    godot::ClassDB::bind_method(D_METHOD("get_spacecraft_orientation"),
                                &SpaceflightSimulation::get_spacecraft_orientation);
    godot::ClassDB::bind_method(D_METHOD("get_spacecraft_basis"),
                                &SpaceflightSimulation::get_spacecraft_basis);
    godot::ClassDB::bind_method(D_METHOD("set_pointing_mode", "mode"),
                                &SpaceflightSimulation::set_pointing_mode);
    godot::ClassDB::bind_method(D_METHOD("get_pointing_mode"),
                                &SpaceflightSimulation::get_pointing_mode);
    godot::ClassDB::bind_method(D_METHOD("get_pointing_error_deg"),
                                &SpaceflightSimulation::get_pointing_error_deg);
    godot::ClassDB::bind_method(D_METHOD("set_manual_torque", "torque_body"),
                                &SpaceflightSimulation::set_manual_torque);
    godot::ClassDB::bind_method(D_METHOD("set_throttle", "throttle"),
                                &SpaceflightSimulation::set_throttle);
    godot::ClassDB::bind_method(D_METHOD("get_throttle"), &SpaceflightSimulation::get_throttle);
    godot::ClassDB::bind_method(D_METHOD("get_snapshot"), &SpaceflightSimulation::get_snapshot);
    godot::ClassDB::bind_method(D_METHOD("is_ready"), &SpaceflightSimulation::is_ready);
    godot::ClassDB::bind_method(D_METHOD("get_last_error"),
                                &SpaceflightSimulation::get_last_error);
}

bool SpaceflightSimulation::configure(const godot::String& kernel_directory,
                                      const godot::String& epoch_utc) {
    return guarded(last_error_, "configure", [&] {
        const std::string directory{kernel_directory.utf8().get_data()};
        const std::string epoch{epoch_utc.utf8().get_data()};

        kernels_ = std::make_shared<const sf::ephemeris::SpiceKernelSet>(
            sf::ephemeris::SpiceKernelSet::from_directory(directory));
        provider_ = std::make_unique<sf::ephemeris::SpiceEphemerisProvider>(kernels_);
        time_converter_ = std::make_unique<sf::ephemeris::SpiceTimeConverter>(kernels_);

        catalog_ = std::make_unique<sf::celestial::BodyCatalog>(
            sf::celestial::BodyCatalog::default_solar_system(*provider_));

        const auto frame = sf::coordinates::ReferenceFrame::ssb_j2000();
        forces_ = std::make_unique<sf::gravity::CompositeForceModel>();
        forces_->add(std::make_unique<sf::gravity::PointMassGravity>(*provider_, *catalog_, frame));
        forces_->add(sf::gravity::OblatenessGravity::for_body(
            *provider_, *provider_, sf::celestial::bodies::earth, frame));

        // Attitude: a 1000 kg box 8 x 3 x 3 m, with twelve RCS thrusters in six
        // couples on a 2 m arm. Numbers chosen to be plausible, not fitted.
        inertia_ = std::make_unique<sf::attitude::InertiaTensor>(
            sf::attitude::InertiaTensor::solid_box(1000.0, sf::math::Vec3{8.0, 3.0, 3.0}));
        // RCS on the same torch technology as the main engine: a thousand times
        // the exhaust velocity and a thousandth of the flow, so each thruster
        // still pushes with 180 N and a slew costs a thousandth of the propellant.
        const sf::propulsion::EngineSpec rcs_thruster{"RCS", 2.0e-5, 3.0e-2, 1.0};
        rcs_ = std::make_unique<sf::attitude::RcsSystem>(
            sf::attitude::RcsSystem::couples(2.0, rcs_thruster));
        pointing_ = std::make_unique<sf::attitude::PointingController>(*provider_, *inertia_, frame);
        rcs_force_ = std::make_unique<sf::attitude::RcsForce>(*rcs_, *pointing_);
        forces_->add_reference(*rcs_force_);

        // Main engine: fusion torch, v_eff = 8 993 800 m/s (0.03 c), still 135 kN
        // at full throttle because the mass flow came down by the same factor the
        // exhaust velocity went up. 600 kg dry + 400 kg of propellant is a budget
        // of 4 594 km/s -- 0.0153 c -- which is enough to leave the Earth-Moon
        // system and start caring about the relativistic kinematics of Milestone 4.
        // See config/engines/torch-mk2.json for what the model charges for it.
        const sf::propulsion::EngineSpec main{"Fusion Torch Mk II", 0.015, 3.0e-2, 1.0};
        craft_ = std::make_unique<sf::spacecraft::Spacecraft>("Tug", 600.0, 400.0, main);
        main_engine_ = std::make_unique<sf::propulsion::MainEngineForce>(*craft_);
        forces_->add_reference(*main_engine_);

        const auto epoch_time = time_converter_->parse(epoch);

        sf::propagation::IntegratorConfig config{};
        config.relative_tolerance = 1.0e-11;
        config.absolute_tolerance_position = 1.0e-3;
        config.absolute_tolerance_velocity = 1.0e-6;
        config.max_step = sf::time::Duration::seconds(3600.0);
        propagator_ = std::make_unique<sf::propagation::DormandPrince54Propagator>(*forces_, config);
        propagator_->set_inertia(inertia_.get());

        clock_ = std::make_unique<sf::simulation::SimulationClock>(epoch_time);

        builder_ = std::make_unique<sf::simulation::SnapshotBuilder>(
            *provider_, *catalog_, *forces_, sf::celestial::bodies::earth, epoch_time, frame);
        builder_->set_target(sf::celestial::bodies::moon);
        // 900 kg dry + 100 kg of RCS propellant, burnt through the thruster's own
        // v_eff. Without this the propellant readout would sit at zero while the
        // thrusters fired, which is the sort of quiet lie this project exists to
        // avoid.
        builder_->set_propulsion(craft_->dry_mass(), main.effective_exhaust_velocity());

        state_ = sf::propagation::PropagationState{};
        state_.mass = craft_->initial_mass();
    });
}

bool SpaceflightSimulation::start_circular_orbit(double altitude_m, double inclination_deg) {
    if (builder_ == nullptr) {
        last_error_ = "start_circular_orbit: configure() first";
        godot::UtilityFunctions::push_error(godot::String{last_error_.c_str()});
        return false;
    }

    return guarded(last_error_, "start_circular_orbit", [&] {
        const auto frame = sf::coordinates::ReferenceFrame::ssb_j2000();
        const auto earth = sf::celestial::bodies::earth;
        const auto t = clock_->coordinate_time();

        const double radius = provider_->mean_radius(earth) + altitude_m;
        const double gm = provider_->gravitational_parameter(earth);
        const double speed = sf::trajectory::circular_speed(gm, radius);
        const double inclination = sf::units::deg_to_rad(inclination_deg);

        const auto earth_state = provider_->state(earth, t, frame);
        state_.state.position = earth_state.state.position + sf::math::Vec3{radius, 0.0, 0.0};
        state_.state.velocity =
            earth_state.state.velocity +
            sf::math::Vec3{0.0, speed * std::cos(inclination), speed * std::sin(inclination)};
        state_.proper_time = sf::time::Duration::zero();
        state_.mass = craft_->initial_mass();
        state_.attitude = sf::attitude::AttitudeState{};

        rebuild_snapshot();
        focus_on_spacecraft();
    });
}

void SpaceflightSimulation::set_time_warp(double warp) {
    if (clock_ == nullptr) {
        return;
    }
    guarded(last_error_, "set_time_warp", [&] { clock_->set_time_warp(warp); });
}

double SpaceflightSimulation::get_time_warp() const {
    return clock_ != nullptr ? clock_->time_warp() : 1.0;
}

void SpaceflightSimulation::advance(double wall_seconds) {
    if (builder_ == nullptr || wall_seconds <= 0.0) {
        return;
    }

    guarded(last_error_, "advance", [&] {
        const auto wall = sf::time::Duration::seconds(wall_seconds);
        const auto target = clock_->target_for(wall);

        // The frame rate decides how far to go, never how to get there: the
        // propagator picks its own steps (rule 21, ADR-0005).
        const auto result = propagator_->propagate(state_, clock_->coordinate_time(), target);
        state_ = result.state;
        clock_->commit(result.time, state_.proper_time - clock_->proper_time(), wall);

        if (!result.ok()) {
            last_error_ = "advance: " + sf::propagation::to_string(result.status) + " -- " +
                          result.message;
            godot::UtilityFunctions::push_warning(godot::String{last_error_.c_str()});
        }

        rebuild_snapshot();
    });
}

void SpaceflightSimulation::rebuild_snapshot() {
    if (builder_ == nullptr) {
        return;
    }
    builder_->set_time_warp(clock_->time_warp());
    snapshot_ = builder_->build(state_, clock_->coordinate_time());
}

void SpaceflightSimulation::set_render_scale(double scale) {
    guarded(last_error_, "set_render_scale", [&] { transform_.set_scale(scale); });
}

double SpaceflightSimulation::get_render_scale() const { return transform_.scale(); }

void SpaceflightSimulation::set_body_scale_exaggeration(double factor) {
    guarded(last_error_, "set_body_scale_exaggeration",
            [&] { transform_.set_body_scale_exaggeration(factor); });
}

void SpaceflightSimulation::focus_on_spacecraft() {
    transform_.set_camera_origin(snapshot_.spacecraft.position);
}

void SpaceflightSimulation::focus_on_body(int index) {
    if (index < 0 || index >= static_cast<int>(snapshot_.bodies.size())) {
        return;
    }
    transform_.set_camera_origin(snapshot_.bodies[static_cast<std::size_t>(index)].position);
}

int SpaceflightSimulation::get_body_count() const {
    return static_cast<int>(snapshot_.bodies.size());
}

godot::String SpaceflightSimulation::get_body_name(int index) const {
    if (index < 0 || index >= get_body_count()) {
        return godot::String{};
    }
    return godot::String{snapshot_.bodies[static_cast<std::size_t>(index)].name.c_str()};
}

Vector3 SpaceflightSimulation::get_body_position(int index) const {
    if (index < 0 || index >= get_body_count()) {
        return Vector3{};
    }
    return to_godot(transform_.to_render(snapshot_.bodies[static_cast<std::size_t>(index)].position));
}

double SpaceflightSimulation::get_body_radius(int index) const {
    if (index < 0 || index >= get_body_count()) {
        return 0.0;
    }
    return static_cast<double>(
        transform_.radius_to_render(snapshot_.bodies[static_cast<std::size_t>(index)].radius));
}

Vector3 SpaceflightSimulation::get_spacecraft_position() const {
    return to_godot(transform_.to_render(snapshot_.spacecraft.position));
}

Vector3 SpaceflightSimulation::get_spacecraft_velocity_direction() const {
    const auto direction = snapshot_.spacecraft.relative_velocity.normalized();
    return Vector3{static_cast<float>(direction.x), static_cast<float>(direction.y),
                   static_cast<float>(direction.z)};
}

godot::Quaternion SpaceflightSimulation::get_spacecraft_orientation() const {
    const auto& q = snapshot_.spacecraft.orientation;
    // Scalar first (core, ADR-0008) -> scalar last (Godot). This one line is the
    // entire convention boundary.
    return godot::Quaternion{static_cast<float>(q.x()), static_cast<float>(q.y()),
                             static_cast<float>(q.z()), static_cast<float>(q.w())};
}

godot::Basis SpaceflightSimulation::get_spacecraft_basis() const {
    return godot::Basis{get_spacecraft_orientation()};
}

bool SpaceflightSimulation::set_pointing_mode(const godot::String& mode) {
    if (pointing_ == nullptr) {
        return false;
    }
    const std::string name{mode.utf8().get_data()};

    sf::attitude::PointingCommand command{};
    command.reference = sf::celestial::bodies::earth;
    if (!name.empty()) {
        const auto parsed = sf::navigation::guidance_from_string(name);
        if (!parsed.has_value()) {
            last_error_ = "unknown pointing mode \"" + name + "\"";
            godot::UtilityFunctions::push_error(godot::String{last_error_.c_str()});
            return false;
        }
        command.mode = *parsed;
    }
    pointing_->set_command(command);
    return true;
}

godot::String SpaceflightSimulation::get_pointing_mode() const {
    if (pointing_ == nullptr || !pointing_->command().mode.has_value()) {
        return godot::String{"HOLD"};
    }
    return godot::String{std::string{sf::navigation::to_string(*pointing_->command().mode)}.c_str()};
}

double SpaceflightSimulation::get_pointing_error_deg() const {
    if (pointing_ == nullptr || builder_ == nullptr) {
        return 0.0;
    }
    return sf::units::rad_to_deg(pointing_->pointing_error(state_, clock_->coordinate_time()));
}

void SpaceflightSimulation::set_manual_torque(const godot::Vector3& torque_body) {
    if (rcs_force_ == nullptr) {
        return;
    }
    rcs_force_->set_manual_torque(
        sf::math::Vec3{torque_body.x, torque_body.y, torque_body.z});
}

void SpaceflightSimulation::set_throttle(double throttle) {
    if (main_engine_ != nullptr) {
        main_engine_->set_throttle(throttle);
    }
}

double SpaceflightSimulation::get_throttle() const {
    return main_engine_ != nullptr ? main_engine_->throttle() : 0.0;
}

godot::Dictionary SpaceflightSimulation::get_snapshot() const {
    godot::Dictionary out;
    if (builder_ == nullptr) {
        return out;
    }

    const auto& craft = snapshot_.spacecraft;
    out["time_tdb_s"] = snapshot_.time.seconds_since_j2000();
    out["elapsed_s"] = snapshot_.elapsed_coordinate.seconds();
    out["proper_time_s"] = snapshot_.proper_time.seconds();
    out["clock_difference_s"] = snapshot_.clock_difference.seconds();
    out["time_warp"] = snapshot_.time_warp;

    out["reference"] = godot::String{craft.reference.name().c_str()};
    out["altitude_m"] = craft.altitude;
    out["distance_m"] = craft.distance_to_reference;
    out["speed_ms"] = craft.relative_velocity.norm();
    out["barycentric_speed_ms"] = craft.speed;
    out["acceleration_ms2"] = craft.acceleration.norm();

    out["mass_kg"] = craft.mass;
    out["propellant_kg"] = craft.propellant;
    out["delta_v_budget_ms"] = craft.delta_v_budget;
    out["thrust_n"] = main_engine_ != nullptr ? main_engine_->current_thrust() : 0.0;
    out["throttle"] = get_throttle();

    out["apoapsis_m"] = craft.elements.apoapsis_radius;
    out["periapsis_m"] = craft.elements.periapsis_radius;
    out["semi_major_axis_m"] = craft.elements.semi_major_axis;
    out["eccentricity"] = craft.elements.eccentricity;
    out["inclination_deg"] = sf::units::rad_to_deg(craft.elements.inclination);
    out["period_s"] = craft.elements.period;

    out["target"] = craft.target.has_value() ? godot::String{craft.target->name().c_str()}
                                             : godot::String{};
    out["target_distance_m"] = craft.target_distance;
    out["target_relative_speed_ms"] = craft.target_relative_speed;

    out["rotation_rate_deg_s"] = sf::units::rad_to_deg(craft.rotation_rate);
    out["angle_to_prograde_deg"] = sf::units::rad_to_deg(craft.angle_to_prograde);
    out["angle_to_nadir_deg"] = sf::units::rad_to_deg(craft.angle_to_nadir);
    out["pointing_mode"] = get_pointing_mode();
    out["pointing_error_deg"] = get_pointing_error_deg();

    out["beta"] = craft.beta;
    out["lorentz_factor"] = craft.lorentz_factor;
    // The cockpit wants gamma - 1, and must NOT compute it by subtracting: at
    // orbital speeds that throws away seven digits (core/simulation/snapshot.hpp).
    out["lorentz_factor_minus_one"] = craft.lorentz_factor_minus_one;

    // Diagnostic to surface when something looks like it is jittering: metres per
    // float ulp.  Measured at the REFERENCE BODY, not at the ship: with the
    // floating origin focused on the ship, the ship sits at the origin and its
    // resolution is identically zero -- true, and useless. The planet a few
    // thousand kilometres away is what visibly jitters when the projection is
    // losing digits.
    const auto* reference_body = snapshot_.find(craft.reference);
    out["render_resolution_m"] =
        transform_.resolution_at(reference_body != nullptr ? reference_body->position
                                                           : craft.position);
    out["render_resolution_ship_m"] = transform_.resolution_at(craft.position);
    return out;
}

godot::String SpaceflightSimulation::get_last_error() const {
    return godot::String{last_error_.c_str()};
}

}  // namespace spaceflight_godot
