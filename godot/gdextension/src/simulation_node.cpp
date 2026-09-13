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

        const auto epoch_time = time_converter_->parse(epoch);

        sf::propagation::IntegratorConfig config{};
        config.relative_tolerance = 1.0e-11;
        config.absolute_tolerance_position = 1.0e-3;
        config.absolute_tolerance_velocity = 1.0e-6;
        config.max_step = sf::time::Duration::seconds(3600.0);
        propagator_ = std::make_unique<sf::propagation::DormandPrince54Propagator>(*forces_, config);

        clock_ = std::make_unique<sf::simulation::SimulationClock>(epoch_time);

        builder_ = std::make_unique<sf::simulation::SnapshotBuilder>(
            *provider_, *catalog_, *forces_, sf::celestial::bodies::earth, epoch_time, frame);
        builder_->set_target(sf::celestial::bodies::moon);

        state_ = sf::propagation::PropagationState{};
        state_.mass = 1000.0;
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
    out["thrust_n"] = craft.thrust;
    out["throttle"] = craft.throttle;

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
