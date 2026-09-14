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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
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
SpaceflightSimulation::~SpaceflightSimulation() {
    // A worker still holding pointers into members that are about to be
    // destroyed is the one way this class can crash the editor. Ask it to stop,
    // then WAIT: cancellation is checked between candidates, so the wait is at
    // most one flight long.
    if (job_ != nullptr) {
        job_->cancel.store(true);
        join_worker();
    }
}

void SpaceflightSimulation::_bind_methods() {
    godot::ClassDB::bind_method(D_METHOD("configure", "kernel_directory", "epoch_utc"),
                                &SpaceflightSimulation::configure);
    godot::ClassDB::bind_method(D_METHOD("start_circular_orbit", "altitude_m", "inclination_deg"),
                                &SpaceflightSimulation::start_circular_orbit);
    godot::ClassDB::bind_method(D_METHOD("align_attitude_to_flight", "nadir_bias_deg"),
                                &SpaceflightSimulation::align_attitude_to_flight);

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
    godot::ClassDB::bind_method(D_METHOD("set_manual_translation", "force_body"),
                                &SpaceflightSimulation::set_manual_translation);
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
    godot::ClassDB::bind_method(D_METHOD("arm_plan"), &SpaceflightSimulation::arm_plan);
    godot::ClassDB::bind_method(D_METHOD("has_planned_transfer"),
                                &SpaceflightSimulation::has_planned_transfer);
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
    godot::ClassDB::bind_method(D_METHOD("get_flight_directions"),
                                &SpaceflightSimulation::get_flight_directions);
    godot::ClassDB::bind_method(D_METHOD("get_body_orientation", "index"),
                                &SpaceflightSimulation::get_body_orientation);
    godot::ClassDB::bind_method(D_METHOD("get_selectable_targets"),
                                &SpaceflightSimulation::get_selectable_targets);
    godot::ClassDB::bind_method(D_METHOD("get_body_directory"),
                                &SpaceflightSimulation::get_body_directory);
    godot::ClassDB::bind_method(
        D_METHOD("start_planning", "target_body", "periapsis_altitude_km",
                 "apoapsis_altitude_km", "search_hours"),
        &SpaceflightSimulation::start_planning);
    godot::ClassDB::bind_method(D_METHOD("is_planning"), &SpaceflightSimulation::is_planning);
    godot::ClassDB::bind_method(D_METHOD("get_planning_progress"),
                                &SpaceflightSimulation::get_planning_progress);
    godot::ClassDB::bind_method(D_METHOD("collect_plan"), &SpaceflightSimulation::collect_plan);
    godot::ClassDB::bind_method(D_METHOD("cancel_planning"),
                                &SpaceflightSimulation::cancel_planning);
    godot::ClassDB::bind_method(D_METHOD("get_system_map"),
                                &SpaceflightSimulation::get_system_map);
    godot::ClassDB::bind_method(D_METHOD("get_system_orbit_paths", "samples_per_body"),
                                &SpaceflightSimulation::get_system_orbit_paths);
    godot::ClassDB::bind_method(D_METHOD("set_target_body", "name"),
                                &SpaceflightSimulation::set_target_body);
    godot::ClassDB::bind_method(D_METHOD("get_target_body"),
                                &SpaceflightSimulation::get_target_body);
    godot::ClassDB::bind_method(D_METHOD("get_rcs_thrusters"),
                                &SpaceflightSimulation::get_rcs_thrusters);
    godot::ClassDB::bind_method(D_METHOD("get_rcs_throttles"),
                                &SpaceflightSimulation::get_rcs_throttles);
    godot::ClassDB::bind_method(D_METHOD("get_orbit_track", "samples"),
                                &SpaceflightSimulation::get_orbit_track);
    godot::ClassDB::bind_method(D_METHOD("get_body_orbit_track", "index", "samples"),
                                &SpaceflightSimulation::get_body_orbit_track);
    godot::ClassDB::bind_method(D_METHOD("get_maneuvers"), &SpaceflightSimulation::get_maneuvers);
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

        // What EXISTS, as opposed to whose mass is in the force model above.
        // Resolved at the scenario epoch because availability is a statement
        // about a time span: mar099s.bsp covers 1995-2050 and a game set in 2075
        // has to be told that now rather than when the pilot selects Mars.
        system_ = std::make_unique<sf::celestial::SolarSystem>(
            sf::celestial::SolarSystem::resolve(*provider_, epoch_time));

        std::vector<sf::simulation::DisplayBody> display;
        for (const auto id : system_->renderable()) {
            const auto* body = system_->find(id);
            display.push_back(sf::simulation::DisplayBody{body->entry.id,
                                                          std::string{body->entry.name},
                                                          body->ephemeris_source, body->gm,
                                                          body->radius});
        }
        builder_->set_display_bodies(std::move(display));

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

bool SpaceflightSimulation::align_attitude_to_flight(double nadir_bias_deg) {
    if (builder_ == nullptr) {
        last_error_ = "align_attitude_to_flight: configure() first";
        return false;
    }
    return guarded(last_error_, "align_attitude_to_flight", [&] {
        const auto& craft = snapshot_.spacecraft;
        const sf::math::Vec3 forward = craft.relative_velocity.normalized();
        const sf::math::Vec3 outward = craft.relative_position.normalized();
        if (forward.norm_squared() <= 0.0 || outward.norm_squared() <= 0.0) {
            throw std::runtime_error("no reference orbit to align to");
        }

        // Body +x on the velocity, body +z outward from the planet -- so the
        // planet is under the floor, which is where a pilot expects it. The
        // third axis is the cross product, which makes the set orthonormal by
        // construction rather than by assertion.
        //
        // In a near-circular orbit the velocity and the outward radial are
        // already perpendicular to about a part in 10^4, but "about" is not
        // orthonormal, and a quaternion built from a non-orthonormal triad is a
        // quaternion that shears. So +z is re-derived from the other two.
        const sf::math::Vec3 side = cross(outward, forward).normalized();
        const sf::math::Vec3 up = cross(forward, side).normalized();

        // The matrix whose COLUMNS are the body axes in the inertial frame,
        // which is exactly what from_rotation_matrix documents itself as taking.
        sf::math::Mat3 basis{};
        const sf::math::Vec3 columns[3] = {forward, side, up};
        for (int row = 0; row < 3; ++row) {
            basis.m[static_cast<std::size_t>(row)] = {columns[0][row], columns[1][row],
                                                      columns[2][row]};
        }
        auto orientation = sf::math::Quaternion::from_rotation_matrix(basis).normalized();

        // The nadir bias: the nose tipped down from the velocity by this much,
        // about the body's own pitch axis.
        //
        // Not decoration. From 400 km the Earth's limb sits 19.7 degrees below
        // the local horizontal (arcsin(6371/6771) = 70.3 degrees from nadir), so
        // a nose exactly on the velocity puts the entire planet below the window
        // sill and the first frame of a new flight is empty sky -- with the
        // planet genuinely there, under the floor. A 25-degree bias brings the
        // limb to 5 degrees above the nose and fills the lower two thirds of the
        // window, which is what "you are in orbit around the Earth" looks like.
        //
        // Real spacecraft hold nadir-biased attitudes for exactly this reason.
        if (std::abs(nadir_bias_deg) > 0.0) {
            const auto pitch = sf::math::Quaternion::from_axis_angle(
                sf::math::Vec3::unit_y(), sf::units::Angle::degrees(nadir_bias_deg));
            orientation = (orientation * pitch).normalized();
        }

        state_.attitude.orientation = orientation;
        state_.attitude.angular_velocity = sf::math::Vec3{};
        rebuild_snapshot();
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
                       *provider_, body.ephemeris_source, observer.position, snapshot_.time,
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
    return sf::relativity::apparent_position(*provider_, body.ephemeris_source,
                                             snapshot_.spacecraft.position,
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

void SpaceflightSimulation::set_manual_translation(const godot::Vector3& force_body) {
    if (rcs_force_ == nullptr) {
        return;
    }
    rcs_force_->set_manual_force(sf::math::Vec3{force_body.x, force_body.y, force_body.z});
}

void SpaceflightSimulation::set_throttle(double throttle) {
    if (main_engine_ != nullptr) {
        main_engine_->set_throttle(throttle);
    }
}

double SpaceflightSimulation::get_throttle() const {
    return main_engine_ != nullptr ? main_engine_->throttle() : 0.0;
}

// ---------------------------------------------------------------------------
// Planning on a worker thread.  See the note in simulation_node.hpp for why
// there is exactly one of them and what it is allowed to touch.
// ---------------------------------------------------------------------------

bool SpaceflightSimulation::start_planning(const godot::String& target_body,
                                           double periapsis_altitude_km,
                                           double apoapsis_altitude_km, double search_hours) {
    if (builder_ == nullptr) {
        last_error_ = "start_planning: configure() first";
        return false;
    }
    if (job_ != nullptr && job_->running.load()) {
        last_error_ = "start_planning: a search is already running";
        return false;
    }
    join_worker();

    const std::string name{target_body.utf8().get_data()};
    const auto lookup = sf::celestial::body_from_name(name);
    if (!lookup.ok) {
        last_error_ = "start_planning: no body named \"" + name + "\"";
        godot::UtilityFunctions::push_error(godot::String{last_error_.c_str()});
        return false;
    }

    job_ = std::make_unique<PlanningJob>();

    // The request is built HERE, on the frame's thread, from state the frame
    // owns -- and everything that can change while the search runs is copied by
    // value. `initial` is the ship's state and `epoch` its coordinate time, both
    // values; the provider, the catalogue and the vehicle are pointers to objects
    // that configure() created and nothing mutates during flight.
    SceneTransferRequest request{};
    request.provider = provider_.get();
    request.orientation = provider_.get();
    request.catalog = catalog_.get();
    request.craft = craft_.get();
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

    PlanningJob* job = job_.get();
    request.cancelled = [job] { return job->cancel.load(); };
    request.on_progress = [job](const sf::navigation::SearchProgress& progress) {
        const std::lock_guard lock{job->mutex};
        job->progress = progress;
    };

    job->running.store(true);
    job->worker = std::thread([job, request] {
        const auto started = std::chrono::steady_clock::now();
        try {
            auto result = spaceflight_godot::plan_transfer(request);
            const std::lock_guard lock{job->mutex};
            job->result = std::move(result);
            job->have_result = true;
        } catch (const std::exception& e) {
            const std::lock_guard lock{job->mutex};
            job->error = e.what();
        } catch (...) {
            const std::lock_guard lock{job->mutex};
            job->error = "unknown error while planning";
        }
        {
            const std::lock_guard lock{job->mutex};
            job->wall_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        }
        job->running.store(false);
    });
    return true;
}

bool SpaceflightSimulation::is_planning() const {
    return job_ != nullptr && job_->running.load();
}

godot::Dictionary SpaceflightSimulation::get_planning_progress() const {
    godot::Dictionary out;
    if (job_ == nullptr) {
        return out;
    }
    const std::lock_guard lock{job_->mutex};
    out["running"] = job_->running.load();
    out["cancelled"] = job_->cancel.load();
    out["stage"] = godot::String{job_->progress.stage.c_str()};
    out["candidates_considered"] = job_->progress.candidates_considered;
    out["candidates_screened"] = job_->progress.candidates_screened;
    out["candidates_flown"] = job_->progress.candidates_flown;
    out["candidates_succeeded"] = job_->progress.candidates_succeeded;
    out["best_total_delta_v"] = job_->progress.best_total_delta_v;
    out["integrator_steps"] = static_cast<int64_t>(job_->progress.integrator_steps);
    out["wall_seconds"] = job_->wall_seconds;
    return out;
}

void SpaceflightSimulation::cancel_planning() {
    if (job_ != nullptr) {
        job_->cancel.store(true);
    }
}

void SpaceflightSimulation::join_worker() {
    if (job_ == nullptr) {
        return;
    }
    if (job_->worker.joinable()) {
        job_->worker.join();
    }
}

godot::Dictionary SpaceflightSimulation::collect_plan() {
    plan_summary_ = godot::Dictionary{};
    if (job_ == nullptr || job_->running.load()) {
        return plan_summary_;
    }
    join_worker();

    bool cancelled = job_->cancel.load();
    std::string error;
    sf::navigation::MissionPlanResult result{};
    bool have_result = false;
    {
        const std::lock_guard lock{job_->mutex};
        error = job_->error;
        have_result = job_->have_result;
        if (have_result) {
            result = std::move(job_->result);
        }
    }
    job_.reset();

    if (!error.empty()) {
        last_error_ = "plan_transfer: " + error;
        godot::UtilityFunctions::push_error(godot::String{last_error_.c_str()});
        return plan_summary_;
    }
    if (!have_result) {
        last_error_ = "plan_transfer: the search produced nothing";
        return plan_summary_;
    }
    if (result.status == sf::navigation::MissionPlanStatus::Cancelled) {
        last_error_ = "search cancelled";
        plan_summary_["cancelled"] = true;
        return plan_summary_;
    }
    if (!result.ok()) {
        last_error_ = result.failure.has_value()
                          ? std::string{result.failure->name()} + ": " + result.failure->detail
                          : std::string{sf::navigation::to_string(result.status)};
        godot::UtilityFunctions::push_error(godot::String{last_error_.c_str()});
        if (cancelled) {
            plan_summary_["cancelled"] = true;
        }
        return plan_summary_;
    }

    // Installed on the FRAME's thread and nowhere else. The worker never writes
    // into planned_, because the cockpit reads it every frame.
    planned_ = std::move(result);
    plan_summary_ = get_plan();
    return plan_summary_;
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
    // Armed, these come from the mission runner, which knows which leg is next.
    // Not armed, the plan is a proposal and the countdown has to come from its
    // own epochs -- otherwise the review screen would show a departure "in
    // 0.0 s" for a burn ninety minutes away, and the pilot would press EXECUTE
    // on a number that meant nothing.
    if (mission_.armed()) {
        out["seconds_to_ignition"] = mission_.seconds_to_injection(now);
        out["seconds_to_insertion"] = mission_.seconds_to_capture(now);
    } else {
        out["seconds_to_ignition"] = (m.departure - now).seconds();
        out["seconds_to_insertion"] = (m.arrival - now).seconds();
    }
    out["armed"] = mission_.armed();
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
    // The burn COUNT is a property of the plan, armed or not. It used to be read
    // off the executor's list, which is empty until arm_plan() installs it -- so
    // the review screen announced "0 burns" for a two-burn transfer it was
    // showing the delta-v of, one line above.
    out["burns"] = static_cast<int64_t>(planned_.maneuvers.size());
    out["burning"] = false;
    out["done"] = false;
    if (plan_ != nullptr && !plan_->empty()) {
        const auto& first = plan_->maneuvers().front();
        const auto& last = plan_->maneuvers().back();
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
        entry["time_of_flight_s"] = alternative.time_of_flight_s;
        entry["branch"] =
            godot::String{alternative.branch == sf::trajectory::TransferDirection::Prograde
                              ? "prograde"
                              : "retrograde"};
        entry["injection_delta_v"] = alternative.injection_delta_v;
        entry["insertion_delta_v"] = alternative.capture_delta_v;
        entry["capture_delta_v"] = alternative.capture_delta_v;
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

godot::Dictionary SpaceflightSimulation::get_flight_directions() const {
    godot::Dictionary out;
    if (pointing_ == nullptr || clock_ == nullptr) {
        return out;
    }
    const auto t = clock_->coordinate_time();
    const auto reference = snapshot_.spacecraft.reference;

    const auto put = [&](const char* key, sf::navigation::GuidanceMode mode) {
        if (const auto direction = pointing_->direction_for(mode, reference, state_, t);
            direction.has_value()) {
            out[key] = Vector3{static_cast<float>(direction->x), static_cast<float>(direction->y),
                               static_cast<float>(direction->z)};
        }
    };
    put("prograde", sf::navigation::GuidanceMode::Prograde);
    put("retrograde", sf::navigation::GuidanceMode::Retrograde);
    put("normal", sf::navigation::GuidanceMode::Normal);
    put("anti_normal", sf::navigation::GuidanceMode::AntiNormal);
    put("radial_out", sf::navigation::GuidanceMode::RadialOut);
    put("radial_in", sf::navigation::GuidanceMode::RadialIn);

    // The nose, straight off the integrated quaternion. Not derived from
    // anything on screen: it is where the hull IS pointing, which is what the
    // error between it and a marker means.
    const auto nose = state_.attitude.orientation.rotate(sf::math::Vec3::unit_x());
    out["nose"] = Vector3{static_cast<float>(nose.x), static_cast<float>(nose.y),
                          static_cast<float>(nose.z)};

    // Target and Sun are directions to a PLACE rather than guidance laws, so
    // they are geometry here rather than a call to the controller -- and they
    // are geometric in the controller too, which is why "point at the target"
    // is not one of its modes.
    if (snapshot_.spacecraft.target.has_value()) {
        if (const auto* body = snapshot_.find(*snapshot_.spacecraft.target); body != nullptr) {
            const auto to_target = (body->position - snapshot_.spacecraft.position).normalized();
            out["target"] = Vector3{static_cast<float>(to_target.x),
                                    static_cast<float>(to_target.y),
                                    static_cast<float>(to_target.z)};
            out["anti_target"] = Vector3{static_cast<float>(-to_target.x),
                                         static_cast<float>(-to_target.y),
                                         static_cast<float>(-to_target.z)};
        }
    }
    if (const auto* sun = snapshot_.find(sf::celestial::bodies::sun); sun != nullptr) {
        const auto to_sun = (sun->position - snapshot_.spacecraft.position).normalized();
        out["sun"] = Vector3{static_cast<float>(to_sun.x), static_cast<float>(to_sun.y),
                             static_cast<float>(to_sun.z)};
    }
    return out;
}

godot::Basis SpaceflightSimulation::get_body_orientation(int index) const {
    if (index < 0 || index >= get_body_count() || provider_ == nullptr) {
        return godot::Basis{};
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    sf::math::Mat3 rotation = sf::math::Mat3::identity();
    try {
        // The BODY's own id and not its ephemeris source: a body-fixed frame comes
        // from the text PCK, which knows IAU_JUPITER whether or not any SPK can
        // say where Jupiter is. Rotating a planet by its barycentre's frame would
        // be asking a different question and getting the identity for an answer.
        rotation = provider_->body_fixed_rotation(body.id, snapshot_.time,
                                                  sf::coordinates::FrameAxes::J2000);
    } catch (const std::exception&) {
        // A barycentre, or a body whose PCK is not loaded. The identity draws it
        // unrotated, which is a visible approximation rather than a crash -- the
        // same policy the whole project applies to missing kernels.
        return godot::Basis{};
    }
    // ⚠️ Godot's three-Vector3 Basis constructor sets the COLUMNS, and the
    // body-fixed axes are the columns of the SPICE matrix -- so the SPICE
    // columns are what has to be handed over.
    //
    // The first version passed the SPICE ROWS while a comment right above it
    // said "columns", and the result was the TRANSPOSE: the inverse rotation.
    // With a flat-coloured sphere nobody could see it. With a texture on it, the
    // Earth turns backwards and the prime meridian is in the wrong place, which
    // is how it was found.
    //
    // Checked against something outside this code: at 2026-01-01 00:00 UTC the
    // sub-solar point must be near 180 degrees east, because solar noon at
    // Greenwich is 12:00 UTC, and near 23 degrees SOUTH, because it is January.
    // `godot/project/tests/probe_orientation.gd` computes both from this matrix:
    //
    //     sub-solar  longitude 180.92 east   latitude -23.01
    return godot::Basis{
        Vector3{static_cast<float>(rotation.at(0, 0)), static_cast<float>(rotation.at(1, 0)),
                static_cast<float>(rotation.at(2, 0))},
        Vector3{static_cast<float>(rotation.at(0, 1)), static_cast<float>(rotation.at(1, 1)),
                static_cast<float>(rotation.at(2, 1))},
        Vector3{static_cast<float>(rotation.at(0, 2)), static_cast<float>(rotation.at(1, 2)),
                static_cast<float>(rotation.at(2, 2))}};
}

godot::Array SpaceflightSimulation::get_selectable_targets() const {
    godot::Array out;
    if (system_ == nullptr) {
        return out;
    }
    // Everything the display can DRAW, which is what a navigation target is:
    // somewhere to measure a distance and a relative speed against. Wider than
    // the set of destinations -- the Sun is a perfectly good thing to point the
    // camera at and a very poor thing to capture into orbit around -- and the
    // two lists are separate for exactly that reason.
    for (const auto id : system_->renderable()) {
        out.append(godot::String{system_->find(id)->entry.name.data()});
    }
    return out;
}

godot::Array SpaceflightSimulation::get_body_directory() const {
    godot::Array out;
    if (system_ == nullptr) {
        return out;
    }
    for (const auto& body : system_->bodies()) {
        godot::Dictionary entry;
        entry["name"] = godot::String{body.entry.name.data()};
        entry["naif_id"] = body.entry.id.naif_id();
        entry["type"] = godot::String{sf::celestial::to_string(body.entry.type).data()};
        entry["parent"] = body.entry.parent.naif_id();
        const auto* parent = system_->find(body.entry.parent);
        entry["parent_name"] =
            parent != nullptr ? godot::String{parent->entry.name.data()} : godot::String{"Sun"};
        entry["radius_m"] = body.radius;
        entry["gm"] = body.gm;
        entry["has_ephemeris"] = body.has_ephemeris;
        entry["position_substituted"] = body.position_from_barycenter;
        entry["can_be_destination"] = body.can_be_destination();
        entry["mission_support"] = godot::String{sf::celestial::to_string(body.support).data()};
        out.append(entry);
    }
    return out;
}

bool SpaceflightSimulation::set_target_body(const godot::String& name) {
    if (builder_ == nullptr) {
        last_error_ = "set_target_body: configure() first";
        return false;
    }
    const std::string wanted{name.utf8().get_data()};
    if (wanted.empty()) {
        builder_->set_target(std::nullopt);
        rebuild_snapshot();
        return true;
    }
    if (system_ != nullptr) {
        if (const auto* body = system_->find(wanted);
            body != nullptr && body->has_ephemeris && body->radius > 0.0) {
            builder_->set_target(body->entry.id);
            rebuild_snapshot();
            return true;
        }
    }
    const auto lookup = sf::celestial::body_from_name(wanted);
    if (lookup.ok) {
        builder_->set_target(lookup.id);
        rebuild_snapshot();
        return true;
    }
    last_error_ = "set_target_body: no body named \"" + wanted + "\"";
    godot::UtilityFunctions::push_error(godot::String{last_error_.c_str()});
    return false;
}

godot::String SpaceflightSimulation::get_target_body() const {
    if (!snapshot_.spacecraft.target.has_value()) {
        return godot::String{};
    }
    return godot::String{snapshot_.spacecraft.target->name().c_str()};
}

godot::Array SpaceflightSimulation::get_rcs_thrusters() const {
    godot::Array out;
    if (rcs_ == nullptr) {
        return out;
    }
    for (const auto& thruster : rcs_->thrusters()) {
        godot::Dictionary entry;
        entry["name"] = godot::String{thruster.name.c_str()};
        // Body frame, metres and unit vector. NOT through RenderTransform: these
        // are offsets on a hull the renderer draws at its own exaggerated size,
        // and scaling them by 1e-6 would put every thruster at the origin.
        entry["position"] = Vector3{static_cast<float>(thruster.position.x),
                                    static_cast<float>(thruster.position.y),
                                    static_cast<float>(thruster.position.z)};
        // The direction of the FORCE ON THE SHIP. The exhaust leaves the other
        // way, and the visual has to flip it -- said here once so that the
        // renderer is not left to guess which convention this is.
        entry["force_direction"] = Vector3{static_cast<float>(thruster.direction.x),
                                           static_cast<float>(thruster.direction.y),
                                           static_cast<float>(thruster.direction.z)};
        out.append(entry);
    }
    return out;
}

godot::PackedFloat64Array SpaceflightSimulation::get_rcs_throttles() const {
    godot::PackedFloat64Array out;
    if (rcs_force_ == nullptr || clock_ == nullptr) {
        return out;
    }
    // Not a second copy of the allocation: RcsForce::throttles IS what
    // RcsForce::evaluate feeds to the RCS, and the renderer asks it the same
    // question at the snapshot's own epoch. There is no arrangement of code in
    // which the drawn jet and the burnt propellant disagree.
    const auto open = rcs_force_->throttles(state_, clock_->coordinate_time());
    out.resize(static_cast<int64_t>(open.size()));
    for (std::size_t i = 0; i < open.size(); ++i) {
        out[static_cast<int64_t>(i)] = open[i];
    }
    return out;
}

godot::PackedVector3Array SpaceflightSimulation::get_orbit_track(int samples) const {
    godot::PackedVector3Array out;
    if (builder_ == nullptr || catalog_ == nullptr || samples < 8) {
        return out;
    }
    const auto& craft = snapshot_.spacecraft;
    const auto* reference = catalog_->find(craft.reference);
    if (reference == nullptr || reference->gm <= 0.0) {
        return out;
    }
    // The centre, taken as a difference rather than by asking the ephemeris
    // again: position - relative_position IS the reference body's position, at
    // the same instant and with no second call that could answer for a slightly
    // different epoch.
    const sf::math::Vec3 centre = craft.position - craft.relative_position;

    auto elements = craft.elements;
    const double e = elements.eccentricity;

    // Where the true anomaly is allowed to go.
    //
    // Closed orbit: the whole circle. Hyperbola: the asymptotes sit at
    // +-acos(-1/e), and the radius goes to infinity as they are approached, so
    // the sweep stops short of them. 0.92 of the way is far enough that the arc
    // reads as an escape and near enough that the last sample is still a place
    // the ship could be.
    double from = -M_PI;
    double to = M_PI;
    if (e >= 1.0) {
        const double asymptote = std::acos(-1.0 / e);
        from = -0.92 * asymptote;
        to = 0.92 * asymptote;
    }

    out.resize(samples);
    for (int i = 0; i < samples; ++i) {
        const double nu = from + (to - from) * static_cast<double>(i) /
                                     static_cast<double>(samples - 1);
        elements.true_anomaly = sf::units::Angle::radians(nu);
        // Kepler lives in core/trajectory, here as everywhere else. This is the
        // inverse the campaign tool uses to build parking orbits from elements;
        // reimplementing it in GDScript would make the drawn orbit a second
        // opinion about the shape of the first.
        const auto sample = sf::trajectory::state_from_elements(elements, reference->gm);
        out[i] = to_godot(transform_.to_render(centre + sample.position));
    }
    return out;
}

godot::PackedVector3Array SpaceflightSimulation::get_body_orbit_track(int index,
                                                                      int samples) const {
    godot::PackedVector3Array out;
    if (provider_ == nullptr || catalog_ == nullptr || samples < 8) {
        return out;
    }
    if (index < 0 || index >= get_body_count()) {
        return out;
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    const auto& craft = snapshot_.spacecraft;
    const auto* reference = catalog_->find(craft.reference);
    if (reference == nullptr || reference->gm <= 0.0 || body.id == craft.reference) {
        return out;
    }

    const auto frame = sf::coordinates::ReferenceFrame::ssb_j2000();
    const auto now = snapshot_.time;

    // How long one lap takes, from the body's own osculating elements about the
    // reference. Not a hard-coded 27.32 days: the same call works for anything
    // in the catalogue, and a number in the code would be a fact about the Moon
    // written down where nothing checks it.
    sf::coordinates::StateVector relative{};
    relative.position = body.position - (craft.position - craft.relative_position);
    relative.velocity = body.velocity - craft.velocity + craft.relative_velocity;
    const auto elements = sf::trajectory::elements_from_state(relative, reference->gm);
    if (!elements.bound || elements.period <= 0.0) {
        return out;
    }

    const sf::math::Vec3 centre = craft.position - craft.relative_position;
    out.resize(samples);
    for (int i = 0; i < samples; ++i) {
        const double fraction = static_cast<double>(i) / static_cast<double>(samples - 1);
        const auto t = now + sf::time::Duration::seconds(elements.period * fraction);
        // The EPHEMERIS, sampled -- not the ellipse. The Moon's path is
        // perturbed by the Sun and by the Earth's figure, and the difference
        // between the two answers is hundreds of kilometres. Drawing the ellipse
        // would be drawing a trajectory the simulation does not fly.
        const auto body_state = provider_->state(body.id, t, frame);
        const auto reference_state = provider_->state(craft.reference, t, frame);
        out[i] = to_godot(transform_.to_render(
            centre + (body_state.state.position - reference_state.state.position)));
    }
    return out;
}

godot::Array SpaceflightSimulation::get_maneuvers() const {
    godot::Array out;
    if (plan_ == nullptr || provider_ == nullptr || clock_ == nullptr) {
        return out;
    }
    const auto now = clock_->coordinate_time();
    for (const auto& maneuver : plan_->maneuvers()) {
        godot::Dictionary entry;
        entry["name"] = godot::String{maneuver.name.c_str()};
        entry["guidance"] =
            godot::String{std::string{sf::navigation::to_string(maneuver.guidance)}.c_str()};
        entry["ignition_tdb_s"] = maneuver.ignition.seconds_since_j2000();
        entry["cutoff_tdb_s"] = maneuver.cutoff().seconds_since_j2000();
        entry["duration_s"] = maneuver.duration.seconds();
        entry["throttle"] = maneuver.throttle;
        entry["seconds_to_ignition"] = (maneuver.ignition - now).seconds();
        entry["active"] = maneuver.active_at(now);
        entry["done"] = now >= maneuver.cutoff();
        // Where it happens, for a marker on the map: the arc the planner flew,
        // at the sample nearest the ignition. The renderer places a dot; it does
        // not work out where the burn is.
        bool located = false;
        if (planned_.ok() && !planned_.trajectory.samples.empty()) {
            const sf::navigation::TrajectoryPrediction::Sample* nearest = nullptr;
            double best = std::numeric_limits<double>::infinity();
            for (const auto& sample : planned_.trajectory.samples) {
                const double gap = std::abs((sample.time - maneuver.ignition).seconds());
                if (gap < best) {
                    best = gap;
                    nearest = &sample;
                }
            }
            if (nearest != nullptr) {
                // The same frame the arc is in, for the same reason: a marker in
                // a different frame from the line it marks is a marker on the
                // wrong part of the line.
                const auto* origin_body = snapshot_.find(planned_.metrics.origin);
                const sf::math::Vec3 anchor =
                    origin_body != nullptr
                        ? origin_body->position
                        : snapshot_.spacecraft.position - snapshot_.spacecraft.relative_position;
                entry["position"] =
                    to_godot(transform_.to_render(anchor + nearest->from_origin));
                located = true;
            }
        }
        entry["located"] = located;
        if (!located) {
            entry["position"] = Vector3{};
        }
        out.append(entry);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The Solar System map.  Sun-centred J2000, in metres, and it says so.
// ---------------------------------------------------------------------------
namespace {

godot::Vector3 to_metres(const sf::math::Vec3& v) {
    return godot::Vector3{static_cast<float>(v.x), static_cast<float>(v.y),
                          static_cast<float>(v.z)};
}

}  // namespace

godot::Dictionary SpaceflightSimulation::get_system_map() const {
    godot::Dictionary out;
    if (builder_ == nullptr || provider_ == nullptr) {
        return out;
    }

    // The frame, named, in the payload. Not a comment: a layer that arrives on
    // this map without knowing which frame it is in is the bug Milestone 7 spent
    // a day on, and a string the consumer can read is cheap insurance.
    out["frame"] = godot::String{"Sun-centred J2000, metres"};
    out["units"] = godot::String{"m"};

    const auto* sun = snapshot_.find(sf::celestial::bodies::sun);
    const sf::math::Vec3 centre = sun != nullptr ? sun->position : sf::math::Vec3{};
    out["centre"] = godot::String{"Sun"};

    godot::Array bodies;
    for (const auto& body : snapshot_.bodies) {
        // Moons are left off the heliocentric map: at this zoom the Moon and the
        // Earth are the same pixel, and Phobos is a label on top of Mars
        // (rule 27). They are still in the snapshot, still selectable, and the
        // LOCAL map draws them.
        const auto* entry = system_ != nullptr ? system_->find(body.id) : nullptr;
        if (entry != nullptr && entry->entry.type == sf::celestial::BodyType::Moon) {
            continue;
        }
        godot::Dictionary row;
        row["name"] = godot::String{body.name.c_str()};
        row["naif_id"] = body.id.naif_id();
        row["position"] = to_metres(body.position - centre);
        row["radius_m"] = body.radius;
        row["is_sun"] = body.id == sf::celestial::bodies::sun;
        row["is_origin"] = planned_.ok() && body.id == planned_.metrics.origin;
        row["is_destination"] =
            snapshot_.spacecraft.target.has_value() && body.id == *snapshot_.spacecraft.target;
        bodies.append(row);
    }
    out["bodies"] = bodies;

    out["ship_position"] = to_metres(snapshot_.spacecraft.position - centre);
    out["ship_velocity"] = to_metres(snapshot_.spacecraft.velocity);

    // The planned arc, in the SAME frame, and rebuilt in absolute coordinates on
    // purpose: each sample is placed where the origin body was at that sample's
    // own epoch. That is exactly what get_planned_trajectory() must NOT do for a
    // planet-centred map and exactly what this one needs -- the Earth really does
    // move half a billion kilometres while the ship is in transit, and a
    // heliocentric map that anchored the arc at today's Earth would draw a
    // transfer that never happened.
    godot::PackedVector3Array arc;
    if (planned_.ok() && !planned_.trajectory.empty()) {
        arc.resize(static_cast<int64_t>(planned_.trajectory.samples.size()));
        int64_t index = 0;
        const auto frame = sf::coordinates::ReferenceFrame::ssb_j2000();
        for (const auto& sample : planned_.trajectory.samples) {
            sf::math::Vec3 absolute{};
            try {
                absolute = provider_->position(planned_.metrics.origin, sample.time, frame) +
                           sample.from_origin;
            } catch (const std::exception&) {
                continue;
            }
            // The Sun moves too -- about one solar radius over a couple of
            // centuries -- so it is read at the SAMPLE's epoch and not at now.
            sf::math::Vec3 sun_then = centre;
            try {
                sun_then = provider_->position(sf::celestial::bodies::sun, sample.time, frame);
            } catch (const std::exception&) {
            }
            arc[index++] = to_metres(absolute - sun_then);
        }
        arc.resize(index);
    }
    out["planned_trajectory"] = arc;

    // The burns, each where the ship will be when it lights.
    godot::Array maneuvers;
    if (clock_ != nullptr) {
        const auto now = clock_->coordinate_time();
        for (const auto& maneuver : mission_.plan().maneuvers()) {
            godot::Dictionary row;
            row["name"] = godot::String{maneuver.name.c_str()};
            row["seconds_to_ignition"] = (maneuver.ignition - now).seconds();
            row["done"] = now >= maneuver.cutoff();
            row["active"] = maneuver.active_at(now);
            // Located by walking the planned arc to the ignition epoch, which is
            // the only place a future position exists: the map does not propagate.
            bool located = false;
            godot::Vector3 at{};
            const auto& samples = planned_.trajectory.samples;
            for (std::size_t i = 1; i < samples.size(); ++i) {
                if (samples[i].time >= maneuver.ignition) {
                    if (static_cast<int64_t>(i) < arc.size()) {
                        at = arc[static_cast<int64_t>(i)];
                        located = true;
                    }
                    break;
                }
            }
            row["located"] = located;
            row["position"] = at;
            maneuvers.append(row);
        }
    }
    out["maneuvers"] = maneuvers;

    out["destination"] = get_target_body();
    out["phase"] = get_mission_phase();
    return out;
}

godot::Dictionary SpaceflightSimulation::get_system_orbit_paths(int samples_per_body) const {
    godot::Dictionary out;
    if (builder_ == nullptr || provider_ == nullptr || system_ == nullptr) {
        return out;
    }
    const int samples = std::clamp(samples_per_body, 8, 512);
    const auto frame = sf::coordinates::ReferenceFrame::ssb_j2000();
    const auto now = clock_->coordinate_time();
    const double gm_sun = provider_->gravitational_parameter(sf::celestial::bodies::sun);

    for (const auto& body : snapshot_.bodies) {
        const auto* entry = system_->find(body.id);
        if (entry == nullptr || entry->entry.type != sf::celestial::BodyType::Planet) {
            continue;
        }

        // ONE revolution, from the body's own osculating period about the Sun.
        // Not a table of orbital periods: the same call works for anything in the
        // directory, and a constant here would be a fact about Mars written down
        // where nothing checks it.
        double period = 0.0;
        try {
            const auto heliocentric = provider_->state(
                body.id, now, sf::coordinates::ReferenceFrame::centered_on(
                                  sf::celestial::bodies::sun));
            period = sf::trajectory::elements_from_state(heliocentric.state, gm_sun).period;
        } catch (const std::exception&) {
            continue;
        }
        if (!(period > 0.0) || !std::isfinite(period)) {
            continue;
        }

        // Centred on now, and CLAMPED to what the kernels actually cover.
        // Neptune's year is 165 of ours: half of it forward from 2026 lands in
        // 2108, which de440s covers, and a naive "now to now + period" would land
        // in 2191, which it does not -- and the honest answer to that is a
        // shorter arc, not an extrapolation.
        double from = now.seconds_since_j2000() - 0.5 * period;
        double to = now.seconds_since_j2000() + 0.5 * period;
        const auto coverage = provider_->coverage(entry->ephemeris_source);
        bool clipped = false;
        if (coverage.valid) {
            const double low = coverage.begin.seconds_since_j2000();
            const double high = coverage.end.seconds_since_j2000();
            if (from < low) {
                from = low;
                clipped = true;
            }
            if (to > high) {
                to = high;
                clipped = true;
            }
        }
        if (!(to > from)) {
            continue;
        }

        godot::PackedVector3Array path;
        path.resize(samples);
        int64_t written = 0;
        for (int i = 0; i < samples; ++i) {
            const double fraction = static_cast<double>(i) / static_cast<double>(samples - 1);
            const auto t = sf::time::CoordinateTime::from_seconds_since_j2000(
                from + fraction * (to - from));
            try {
                const auto p = provider_->position(entry->ephemeris_source, t, frame);
                const auto sun_then = provider_->position(sf::celestial::bodies::sun, t, frame);
                path[written++] = to_metres(p - sun_then);
            } catch (const std::exception&) {
                break;
            }
        }
        path.resize(written);
        if (written < 3) {
            continue;
        }

        godot::Dictionary row;
        row["path"] = path;
        row["period_s"] = period;
        row["clipped"] = clipped;
        out[godot::String{body.name.c_str()}] = row;
    }
    return out;
}

godot::PackedVector3Array SpaceflightSimulation::get_planned_trajectory() const {
    godot::PackedVector3Array out;
    if (!planned_.ok() || provider_ == nullptr) {
        return out;
    }
    // ORIGIN-RELATIVE, anchored at where the origin body is NOW.
    //
    // ⚠️ This is a correction to what Milestone 6.2 shipped, and the measurement
    // is worth writing down. The arc was being rebuilt in absolute coordinates by
    // adding the origin body's position AT EACH SAMPLE'S OWN EPOCH -- which is
    // the right answer to "where was the ship in the Solar System", and the wrong
    // one for anything drawn around the Earth. The Earth travels 30 km/s: over
    // the 4.75 days a translunar transfer spans, it moves 12.7 MILLION km. The
    // map drew a transfer to the Moon as a line reaching 13.2 million km, against
    // a Moon at 361 thousand, and everything else collapsed into a dot at the
    // centre.
    //
    // Measured, with a planner run from a 400 km parking orbit:
    //
    //     ship                     6 771 km from Earth
    //     Moon                   361 025 km
    //     arc, absolute      153 734 .. 13 228 253 km      <- what M6.2 returned
    //     arc, origin-relative     6 771 ..    361 000 km  <- this
    //
    // Nothing about the trajectory changed; what changed is which frame it is
    // expressed in. This is the SAME convention get_body_orbit_track() uses, and
    // that is the point: three curves drawn on one map have to be in one frame,
    // or the map cannot be read.
    //
    // For an interplanetary map centred on the Sun the absolute form would be the
    // right one. When that map exists it should ask for it explicitly rather than
    // this one guessing.
    const auto* origin_body = snapshot_.find(planned_.metrics.origin);
    const sf::math::Vec3 anchor =
        origin_body != nullptr ? origin_body->position
                               : snapshot_.spacecraft.position - snapshot_.spacecraft.relative_position;

    out.resize(static_cast<int64_t>(planned_.trajectory.samples.size()));
    int64_t index = 0;
    for (const auto& sample : planned_.trajectory.samples) {
        out[index++] = to_godot(transform_.to_render(anchor + sample.from_origin));
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

bool SpaceflightSimulation::arm_plan() {
    if (!planned_.ok() || plan_ == nullptr || clock_ == nullptr) {
        last_error_ = "arm_plan: no transfer has been planned";
        return false;
    }
    if (planned_.maneuvers.empty()) {
        last_error_ = "arm_plan: the plan has no burns";
        return false;
    }
    const auto now = clock_->coordinate_time();
    if (now >= planned_.maneuvers.maneuvers().front().ignition) {
        // The injection epoch is behind us. Arming anyway would put the executor
        // straight into the coast leg and then fire a capture burn worked out
        // for a trajectory the ship never flew. Refused with the reason, which
        // the cockpit prints.
        last_error_ = "arm_plan: the departure has passed -- plan again";
        return false;
    }
    // Install it by replacing the CONTENTS of the plan the executor already
    // points at. Swapping the objects would dangle the reference the force
    // model holds.
    *plan_ = planned_.maneuvers;
    mission_.set_vehicle(craft_.get());
    mission_.arm(planned_);
    plan_summary_ = get_plan();
    return true;
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
