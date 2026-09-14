#include "simulation_node.hpp"

#include "mission_planner.hpp"

#include "core/coordinates/reference_frame.hpp"
#include "core/gravity/oblateness_gravity.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/navigation/mission.hpp"
#include "core/relativity/light_time.hpp"
#include "core/relativity/optics.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/conversions.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <exception>
#include <utility>

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
    godot::ClassDB::bind_method(D_METHOD("get_beta_vector"),
                                &SpaceflightSimulation::get_beta_vector);
    godot::ClassDB::bind_method(D_METHOD("set_visual_test_beta", "beta"),
                                &SpaceflightSimulation::set_visual_test_beta);
    godot::ClassDB::bind_method(D_METHOD("get_visual_test_beta"),
                                &SpaceflightSimulation::get_visual_test_beta);
    godot::ClassDB::bind_method(D_METHOD("get_body_apparent_position", "index"),
                                &SpaceflightSimulation::get_body_apparent_position);
    godot::ClassDB::bind_method(D_METHOD("get_body_observed_position", "index", "retarded", "aberration"),
                                &SpaceflightSimulation::get_body_observed_position);
    godot::ClassDB::bind_method(D_METHOD("get_body_light_time", "index"),
                                &SpaceflightSimulation::get_body_light_time);
    godot::ClassDB::bind_method(D_METHOD("get_body_doppler", "index"),
                                &SpaceflightSimulation::get_body_doppler);
    godot::ClassDB::bind_method(D_METHOD("get_body_relative_velocity_scene", "index"),
                                &SpaceflightSimulation::get_body_relative_velocity_scene);
    godot::ClassDB::bind_method(D_METHOD("get_light_speed_scene"),
                                &SpaceflightSimulation::get_light_speed_scene);
    godot::ClassDB::bind_method(D_METHOD("set_apparent_positions_enabled", "enabled"),
                                &SpaceflightSimulation::set_apparent_positions_enabled);
    godot::ClassDB::bind_method(D_METHOD("get_apparent_positions_enabled"),
                                &SpaceflightSimulation::get_apparent_positions_enabled);
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
    godot::ClassDB::bind_method(D_METHOD("set_engine_mode", "mode"),
                                &SpaceflightSimulation::set_engine_mode);
    godot::ClassDB::bind_method(D_METHOD("cycle_engine_mode"),
                                &SpaceflightSimulation::cycle_engine_mode);
    godot::ClassDB::bind_method(D_METHOD("get_engine_mode"),
                                &SpaceflightSimulation::get_engine_mode);
    godot::ClassDB::bind_method(
        D_METHOD("plan_transfer", "target_body", "periapsis_altitude_km",
                 "apoapsis_altitude_km", "search_hours"),
        &SpaceflightSimulation::plan_transfer);
    godot::ClassDB::bind_method(D_METHOD("get_orbit_about_target"),
                                &SpaceflightSimulation::get_orbit_about_target);
    godot::ClassDB::bind_method(D_METHOD("has_plan"), &SpaceflightSimulation::has_plan);
    godot::ClassDB::bind_method(D_METHOD("clear_plan"), &SpaceflightSimulation::clear_plan);
    godot::ClassDB::bind_method(D_METHOD("get_plan"), &SpaceflightSimulation::get_plan);
    godot::ClassDB::bind_method(D_METHOD("set_execution_model", "model"),
                                &SpaceflightSimulation::set_execution_model);
    godot::ClassDB::bind_method(D_METHOD("get_execution_model"),
                                &SpaceflightSimulation::get_execution_model);
    godot::ClassDB::bind_method(D_METHOD("get_plan_alternatives"),
                                &SpaceflightSimulation::get_plan_alternatives);
    godot::ClassDB::bind_method(D_METHOD("get_mission_phase"),
                                &SpaceflightSimulation::get_mission_phase);
    godot::ClassDB::bind_method(D_METHOD("get_mission_outcome"),
                                &SpaceflightSimulation::get_mission_outcome);
    godot::ClassDB::bind_method(D_METHOD("get_planned_trajectory"),
                                &SpaceflightSimulation::get_planned_trajectory);
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

        // Fusion Torch Mk III: two operating points of one 900 GW plant.
        //
        //   IMPULSE  w = 0.03 c, 200 kN  -- one g on this 20-tonne ship, burns in
        //                                   minutes, budget 0.0899 c
        //   CRUISE   w = 0.5 c,  11.2 kN -- a third of a g falling to nothing over
        //                                   years, budget 0.9048 c
        //
        // 1 tonne of hull and 19 of propellant. Reaching beta = 0.9 needs BOTH the
        // exhaust velocity and the mass ratio, and eight years of burning; that
        // last part is the rocket equation, not the simulator
        // (config/engines/torch-mk3.json).
        const sf::propulsion::MultiModeEngine main{
            {{"IMPULSE", sf::propulsion::EngineSpec{"IMPULSE", 0.0222376, 0.03, 1.0}},
             {"CRUISE", sf::propulsion::EngineSpec{"CRUISE", 7.470950e-05, 0.5, 1.0}}}};
        craft_ = std::make_unique<sf::spacecraft::Spacecraft>("Torch", 1000.0, 19000.0, main);
        main_engine_ = std::make_unique<sf::propulsion::MainEngineForce>(*craft_);
        forces_->add_reference(*main_engine_);

        // The maneuver executor is built EMPTY and wired in now, once. It is what
        // flies a planned mission; with no plan it contributes nothing.
        plan_ = std::make_unique<sf::navigation::ManeuverPlan>();
        executor_ = std::make_unique<sf::navigation::ManeuverExecutor>(*provider_, *craft_,
                                                                       *plan_, frame);
        forces_->add_reference(*executor_);

        const auto epoch_time = time_converter_->parse(epoch);

        sf::propagation::IntegratorConfig config{};
        config.relative_tolerance = 1.0e-11;
        config.absolute_tolerance_position = 1.0e-3;
        config.absolute_tolerance_velocity = 1.0e-6;
        config.absolute_tolerance_proper_time = 1.0e-9;
        config.minimum_mass = craft_->dry_mass();
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
        builder_->set_propulsion(craft_->dry_mass(),
                                 craft_->engine().effective_exhaust_velocity());

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
        //
        // With a plan armed the frame goes through run_mission instead, because
        // the executor has to be ARMED per leg: at an ignition or a cutoff the
        // thrust is genuinely two-valued, and only the runner knows which leg it
        // is integrating. Without arming the switch becomes a discontinuity inside
        // a step and the error controller grinds the step size to the floor
        // (core/navigation/maneuver_executor.hpp).
        sf::propagation::PropagationResult result{};
        if (executor_ != nullptr && plan_ != nullptr && !plan_->empty()) {
            const auto mission = sf::navigation::run_mission(*propagator_, *executor_, state_,
                                                             clock_->coordinate_time(), target);
            result.state = mission.state;
            result.time = mission.time;
            result.status = mission.status;
            result.message = mission.message;
        } else {
            result = propagator_->propagate(state_, clock_->coordinate_time(), target);
        }
        state_ = result.state;
        clock_->commit(result.time, state_.proper_time - clock_->proper_time(), wall);

        // Section 16: where the mission has got to. A classification, computed
        // in the core from the plan and the state, and the cockpit prints it.
        if (mission_.armed() && provider_ != nullptr) {
            const double error =
                pointing_ != nullptr
                    ? pointing_->pointing_error(state_, clock_->coordinate_time())
                    : 0.0;
            mission_.update(*provider_, clock_->coordinate_time(), state_,
                            sf::units::Angle::radians(error));

            // ...and the ship points where the phase says. Without this,
            // ORIENTING would be a label nobody acts on: the cockpit would
            // report that the ship is getting ready for the burn while the nose
            // sat wherever the pilot last left it.
            //
            // The command comes from the core, which knows which burn is next
            // and which way it points; this applies it. A mission that has
            // finished or been abandoned returns nullopt and the attitude goes
            // back to the pilot -- an armed plan steers, a finished one does not.
            if (pointing_ != nullptr) {
                if (const auto command = mission_.pointing_command(); command.has_value()) {
                    pointing_->set_command(*command);
                }
            }
        }

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

Vector3 SpaceflightSimulation::get_beta_vector() const {
    const auto beta = optics_observer_velocity() / sf::units::c;
    return Vector3{static_cast<float>(beta.x), static_cast<float>(beta.y),
                   static_cast<float>(beta.z)};
}

sf::math::Vec3 SpaceflightSimulation::optics_observer_velocity() const {
    if (visual_test_beta_ < 0.0) {
        return snapshot_.spacecraft.velocity;
    }
    auto direction = snapshot_.spacecraft.velocity.normalized();
    if (direction.norm_squared() == 0.0) {
        direction = sf::math::Vec3::unit_x();
    }
    return direction * (visual_test_beta_ * sf::units::c);
}

void SpaceflightSimulation::set_visual_test_beta(double beta) {
    if (beta < 0.0) {
        visual_test_beta_ = -1.0;
    } else if (beta < 1.0 && std::isfinite(beta)) {
        visual_test_beta_ = beta;
    }
}

double SpaceflightSimulation::get_visual_test_beta() const { return visual_test_beta_; }

godot::Vector3 SpaceflightSimulation::get_body_apparent_position(int index) const {
    return get_body_observed_position(index, apparent_positions_, apparent_positions_);
}

godot::Vector3 SpaceflightSimulation::get_body_observed_position(int index, bool retarded,
                                                                 bool aberration) const {
    if (index < 0 || index >= get_body_count() || builder_ == nullptr) {
        return Vector3{};
    }
    if (!retarded && !aberration) {
        return get_body_position(index);
    }

    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    const auto& observer = snapshot_.spacecraft;

    sf::math::Vec3 relative = body.position - observer.position;
    if (retarded) {
        relative = sf::relativity::apparent_position(
                       *provider_, body.id, observer.position, snapshot_.time,
                       sf::coordinates::ReferenceFrame::ssb_j2000())
                       .relative_position;
    }

    // Aberration turns the DIRECTION; the distance is the retarded one.  Rebuilt
    // as direction x distance rather than transformed as a position, because
    // aberration is a map of the celestial sphere and nothing else.
    const sf::math::Vec3 beta = optics_observer_velocity() / sf::units::c;
    const double distance = relative.norm();
    const sf::math::Vec3 direction = aberration
                                         ? sf::relativity::aberrate_source_direction(relative, beta)
                                         : relative.normalized();

    // Back to an absolute position so that the SAME RenderTransform -- the same
    // floating origin, the same scale -- handles it (rendering.md section 2).
    return to_godot(transform_.to_render(observer.position + direction * distance));
}

double SpaceflightSimulation::get_body_light_time(int index) const {
    if (index < 0 || index >= get_body_count() || builder_ == nullptr) {
        return 0.0;
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    return sf::relativity::apparent_position(*provider_, body.id, snapshot_.spacecraft.position,
                                             snapshot_.time,
                                             sf::coordinates::ReferenceFrame::ssb_j2000())
        .light_time;
}

double SpaceflightSimulation::get_body_doppler(int index) const {
    if (index < 0 || index >= get_body_count() || builder_ == nullptr) {
        return 1.0;
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    const auto& observer = snapshot_.spacecraft;

    // The body moves too, so the Doppler factor is the one of the RELATIVE
    // motion: the observer's velocity minus the source's, over c.  Using the
    // observer's velocity alone would make a co-moving planet blue.
    const sf::math::Vec3 beta = (optics_observer_velocity() - body.velocity) / sf::units::c;
    const sf::math::Vec3 to_source = body.position - observer.position;
    if (to_source.norm_squared() <= 0.0) {
        return 1.0;
    }
    return sf::relativity::doppler_factor_to_source(to_source, beta);
}

godot::Vector3 SpaceflightSimulation::get_body_relative_velocity_scene(int index) const {
    if (index < 0 || index >= get_body_count()) {
        return Vector3{};
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    const auto relative = body.velocity - optics_observer_velocity();
    // Scene units per second: vector_to_render scales without translating, which
    // is exactly right for a velocity and wrong for a position.
    return to_godot(transform_.vector_to_render(relative));
}

double SpaceflightSimulation::get_light_speed_scene() const {
    // The SAME scale as the velocity above, so that |v|/c is preserved exactly
    // and the structural |v| < c survives the conversion to float
    // (docs/architecture/relativistic-shaders.md section 4).
    return sf::units::c * transform_.scale();
}

void SpaceflightSimulation::set_apparent_positions_enabled(bool enabled) {
    apparent_positions_ = enabled;
}

bool SpaceflightSimulation::get_apparent_positions_enabled() const {
    return apparent_positions_;
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
    // An armed mission steers the ship, and the next frame would overwrite this
    // command anyway (see advance()). Refusing says so; accepting and then
    // silently undoing it would leave the cockpit showing a mode the ship is not
    // in -- the same class of quiet lie as a label that keeps saying "prograde"
    // after the ship has stopped being prograde.
    if (mission_.armed() && mission_.pointing_command().has_value()) {
        last_error_ =
            "the flight computer is steering: abandon the plan (K) to take the attitude back";
        godot::UtilityFunctions::push_warning(godot::String{last_error_.c_str()});
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

bool SpaceflightSimulation::set_engine_mode(const godot::String& mode) {
    if (craft_ == nullptr) {
        return false;
    }
    return craft_->select_mode(std::string{mode.utf8().get_data()});
}

void SpaceflightSimulation::cycle_engine_mode() {
    if (craft_ != nullptr) {
        craft_->cycle_mode();
    }
}

godot::String SpaceflightSimulation::get_engine_mode() const {
    return craft_ != nullptr ? godot::String{craft_->mode_name().c_str()} : godot::String{};
}

void SpaceflightSimulation::set_throttle(double throttle) {
    if (main_engine_ != nullptr) {
        main_engine_->set_throttle(throttle);
    }
}

double SpaceflightSimulation::get_throttle() const {
    return main_engine_ != nullptr ? main_engine_->throttle() : 0.0;
}

godot::Dictionary SpaceflightSimulation::plan_transfer(const godot::String& target_body,
                                                       double periapsis_altitude_km,
                                                       double apoapsis_altitude_km,
                                                       double search_hours) {
    plan_summary_ = godot::Dictionary{};
    if (builder_ == nullptr) {
        last_error_ = "plan_transfer: configure() first";
        return plan_summary_;
    }

    guarded(last_error_, "plan_transfer", [&] {
        const std::string name{target_body.utf8().get_data()};
        const auto lookup = sf::celestial::body_from_name(name);
        if (!lookup.ok) {
            throw std::invalid_argument("no body named \"" + name + "\"");
        }

        SceneTransferRequest request{};
        request.provider = provider_.get();
        request.orientation = provider_.get();
        request.catalog = catalog_.get();
        request.craft = craft_.get();
        // The same perturbation set the propagator itself runs with. Planning
        // against a different force model than the one that flies the result is
        // how a corrected trajectory stops being corrected.
        request.j2_bodies = {sf::celestial::bodies::earth};
        request.integrator = propagator_->config();
        request.initial = state_;
        request.epoch = clock_->coordinate_time();
        request.center = sf::celestial::bodies::earth;
        request.target = lookup.id;
        request.target_periapsis_altitude_m = periapsis_altitude_km * 1000.0;
        request.target_apoapsis_altitude_m = apoapsis_altitude_km * 1000.0;
        request.search_window_s = search_hours * 3600.0;
        request.execution = execution_;
        request.pointing = pointing_->gains();

        // Into a LOCAL first. A failed re-plan must not disturb the mission that
        // is already flying: assigning straight into planned_ would leave the
        // cockpit reading an empty plan while plan_ still held an armed burn and
        // the executor still meant to fly it.
        //
        // Nothing else needs cleaning up. The core planner never writes into the
        // ship's live plan while it searches -- the old GDExtension planner did,
        // and a failure there left a trial burn armed for the ship to fly four
        // simulated days from a target it was never going to reach.
        auto result = spaceflight_godot::plan_transfer(request);
        if (!result.ok()) {
            throw std::runtime_error(
                result.failure.has_value()
                    ? std::string{result.failure->name()} + ": " + result.failure->detail
                    : std::string{sf::navigation::to_string(result.status)});
        }

        planned_ = std::move(result);
        // Install it by replacing the CONTENTS of the plan the executor already
        // points at. Swapping the objects would dangle the reference the force
        // model holds.
        *plan_ = planned_.maneuvers;
        mission_.set_vehicle(craft_.get());
        mission_.arm(planned_);
    });
    // Built from the plan rather than cached separately: the summary and the
    // readout have to be the same thing, and two copies of it would be two
    // things that can disagree -- which is the mistake this whole milestone is
    // about, in miniature.
    plan_summary_ = get_plan();
    return plan_summary_;
}

// Section 15: everything the computer must show BEFORE execution, and all of it
// out of the core's MissionMetrics. Nothing here computes; it renames.
godot::Dictionary SpaceflightSimulation::get_plan() const {
    godot::Dictionary out;
    if (!planned_.ok() || clock_ == nullptr) {
        return out;
    }
    const auto& m = planned_.metrics;
    const auto now = clock_->coordinate_time();

    out["valid"] = true;
    out["target"] = godot::String{m.destination.name().data()};
    out["origin"] = godot::String{m.origin.name().data()};

    out["departure_tdb_s"] = m.departure.seconds_since_j2000();
    out["arrival_tdb_s"] = m.arrival.seconds_since_j2000();
    out["seconds_to_ignition"] = mission_.seconds_to_injection(now);
    out["seconds_to_insertion"] = mission_.seconds_to_capture(now);
    out["time_of_flight_s"] = m.time_of_flight_s;
    out["time_of_flight_days"] = m.time_of_flight_s / 86400.0;
    out["transfer_angle_deg"] = m.transfer_angle.degrees();
    out["branch"] = godot::String{m.branch == sf::trajectory::TransferDirection::Prograde
                                      ? "prograde"
                                      : "retrograde"};

    out["injection_delta_v"] = m.injection_delta_v;
    out["midcourse_delta_v"] = m.midcourse_delta_v;
    out["insertion_delta_v"] = m.capture_delta_v;
    out["total_delta_v"] = m.total_delta_v;
    out["delta_v_available"] = m.delta_v_available;
    out["injection_duration_s"] = m.injection_duration_s;
    out["insertion_duration_s"] = m.capture_duration_s;

    out["v_infinity"] = m.v_infinity;
    out["flyby_periapsis_m"] = m.predicted_flyby_periapsis;
    out["predicted_periapsis_m"] = m.predicted_periapsis_altitude;
    out["predicted_apoapsis_m"] = m.predicted_apoapsis_altitude;
    out["predicted_eccentricity"] = m.predicted_eccentricity;
    out["predicted_inclination_deg"] = m.predicted_inclination.degrees();
    out["predicted_raan_deg"] = m.predicted_raan.degrees();

    out["propellant_required_kg"] = m.propellant_required;
    out["propellant_remaining_kg"] = m.propellant_remaining;

    // What the autopilot is expected to do while the capture burn runs. Shown
    // because section 10 is explicit that a controller is not judged on the
    // orbit alone.
    out["pointing_error_mean_deg"] = m.pointing_error_mean.degrees();
    out["pointing_error_peak_deg"] = m.pointing_error_peak.degrees();
    out["rcs_propellant_kg"] = m.rcs_propellant;
    out["rcs_duty_cycle"] = m.rcs_duty_cycle;
    out["torque_saturation"] = m.torque_saturation;

    out["phase"] = get_mission_phase();
    if (plan_ != nullptr && !plan_->empty()) {
        const auto& first = plan_->maneuvers().front();
        const auto& last = plan_->maneuvers().back();
        out["burns"] = static_cast<int64_t>(plan_->size());
        out["burning"] = first.active_at(now) || last.active_at(now);
        out["done"] = now >= last.cutoff();
    }
    return out;
}

godot::Array SpaceflightSimulation::get_plan_alternatives() const {
    godot::Array out;
    for (const auto& alternative : planned_.alternatives) {
        godot::Dictionary entry;
        entry["label"] = godot::String{alternative.label.c_str()};
        entry["feasible"] = alternative.feasible;
        entry["departure_tdb_s"] = alternative.departure.seconds_since_j2000();
        entry["time_of_flight_days"] = alternative.time_of_flight_s / 86400.0;
        entry["branch"] =
            godot::String{alternative.branch == sf::trajectory::TransferDirection::Prograde
                              ? "prograde"
                              : "retrograde"};
        entry["injection_delta_v"] = alternative.injection_delta_v;
        entry["insertion_delta_v"] = alternative.capture_delta_v;
        entry["total_delta_v"] = alternative.total_delta_v;
        entry["predicted_periapsis_m"] = alternative.predicted_periapsis_altitude;
        entry["predicted_apoapsis_m"] = alternative.predicted_apoapsis_altitude;
        entry["predicted_eccentricity"] = alternative.predicted_eccentricity;
        entry["predicted_inclination_deg"] = alternative.predicted_inclination.degrees();
        entry["predicted_raan_deg"] = alternative.predicted_raan.degrees();
        entry["failure"] =
            godot::String{std::string{sf::navigation::to_string(alternative.failure)}.c_str()};
        out.append(entry);
    }
    return out;
}

godot::String SpaceflightSimulation::get_mission_phase() const {
    return godot::String{
        std::string{sf::navigation::to_string(mission_.phase())}.c_str()};
}

godot::Dictionary SpaceflightSimulation::get_mission_outcome() const {
    godot::Dictionary out;
    const auto& outcome = mission_.outcome();
    if (!outcome.recorded) {
        return out;
    }
    const auto row = [](const sf::navigation::PredictedVersusActual& value) {
        godot::Dictionary entry;
        entry["predicted"] = value.predicted;
        entry["actual"] = value.actual;
        entry["difference"] = value.difference();
        entry["recorded"] = value.recorded;
        return entry;
    };
    out["periapsis_m"] = row(outcome.periapsis_altitude);
    out["apoapsis_m"] = row(outcome.apoapsis_altitude);
    out["eccentricity"] = row(outcome.eccentricity);
    out["inclination_deg"] = row(outcome.inclination_deg);
    out["propellant_kg"] = row(outcome.propellant_used);
    out["arrival_tdb_s"] = row(outcome.arrival_tdb_s);
    out["capture_delta_v"] = row(outcome.capture_delta_v);
    out["note"] = godot::String{outcome.note.c_str()};
    return out;
}

godot::PackedVector3Array SpaceflightSimulation::get_planned_trajectory() const {
    godot::PackedVector3Array out;
    if (!planned_.ok() || provider_ == nullptr) {
        return out;
    }
    const auto frame = sf::coordinates::ReferenceFrame::ssb_j2000();
    out.resize(static_cast<int64_t>(planned_.trajectory.samples.size()));
    int64_t index = 0;
    for (const auto& sample : planned_.trajectory.samples) {
        // The arc is stored relative to the origin body; the renderer wants it
        // in the same absolute frame everything else goes through, so the origin
        // is added back at the sample's own epoch. Using the CURRENT epoch would
        // draw the transfer against an Earth that has moved 1.1e9 m over the
        // four days the arc spans.
        const auto origin = provider_->state(planned_.metrics.origin, sample.time, frame);
        out[index++] = to_godot(transform_.to_render(origin.state.position + sample.from_origin));
    }
    return out;
}

bool SpaceflightSimulation::set_execution_model(const godot::String& model) {
    const std::string name{model.utf8().get_data()};
    if (name == "finite" || name == "finite_burn") {
        execution_ = sf::navigation::ExecutionModel::FiniteBurn;
        return true;
    }
    if (name == "autopilot") {
        execution_ = sf::navigation::ExecutionModel::Autopilot;
        return true;
    }
    // IMPULSIVE is deliberately not offered. It produces no maneuvers at all --
    // there is no engine in that model -- so a ship cannot be armed with its
    // result, and offering it would be offering a button that plans a mission
    // nobody can fly.
    last_error_ = "unknown execution model \"" + name + "\" (finite | autopilot)";
    godot::UtilityFunctions::push_error(godot::String{last_error_.c_str()});
    return false;
}

godot::String SpaceflightSimulation::get_execution_model() const {
    return godot::String{std::string{sf::navigation::to_string(execution_)}.c_str()};
}

godot::Dictionary SpaceflightSimulation::get_orbit_about_target() const {
    godot::Dictionary out;
    const auto& craft = snapshot_.spacecraft;
    if (builder_ == nullptr || !craft.target.has_value()) {
        return out;
    }
    const auto* body = snapshot_.find(*craft.target);
    if (body == nullptr || !(body->gm > 0.0)) {
        return out;
    }

    // The Moon's Hill sphere is 61 500 km; outside it the Earth dominates and an
    // osculating orbit about the Moon is a number, not a description. The cut is
    // stated rather than tuned: it is the radius at which the two-body picture
    // about the target stops being the right picture at all.
    constexpr double kHillRadius = 6.15e7;   // [m]
    const double distance = (craft.position - body->position).norm();
    if (distance > kHillRadius) {
        return out;
    }

    sf::coordinates::StateVector relative{};
    relative.position = craft.position - body->position;
    relative.velocity = craft.velocity - body->velocity;
    const auto elements = sf::trajectory::elements_from_state(relative, body->gm);

    out["body"] = godot::String{body->name.c_str()};
    out["radius_m"] = body->radius;
    out["periapsis_m"] = elements.periapsis_radius;
    out["apoapsis_m"] = elements.apoapsis_radius;
    out["eccentricity"] = elements.eccentricity;
    out["inclination_deg"] = elements.inclination.degrees();
    out["period_s"] = elements.period;
    out["distance_m"] = distance;
    out["speed_ms"] = relative.velocity.norm();
    out["captured"] = elements.eccentricity < 1.0;
    return out;
}

bool SpaceflightSimulation::has_plan() const {
    return plan_ != nullptr && !plan_->empty();
}

void SpaceflightSimulation::clear_plan() {
    if (plan_ != nullptr) {
        *plan_ = sf::navigation::ManeuverPlan{};
    }
    planned_ = sf::navigation::MissionPlanResult{};
    mission_.abort();
    plan_summary_ = godot::Dictionary{};
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
    out["engine_mode"] = get_engine_mode();
    out["mass_flow_kg_s"] = craft.mass_flow;
    out["endurance_s"] = craft.endurance;
    out["thrust_along_track"] = craft.thrust_along_track;
    out["specific_energy_rate"] = craft.specific_energy_rate;
    out["exhaust_velocity_c"] =
        craft_ != nullptr ? craft_->engine().exhaust_velocity_fraction_c() : 0.0;

    out["apoapsis_m"] = craft.elements.apoapsis_radius;
    out["periapsis_m"] = craft.elements.periapsis_radius;
    out["semi_major_axis_m"] = craft.elements.semi_major_axis;
    out["eccentricity"] = craft.elements.eccentricity;
    out["inclination_deg"] = craft.elements.inclination.degrees();
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
