#include "app/session/flight_session.hpp"

#include "app/session/transfer_bridge.hpp"

#include "core/coordinates/reference_frame.hpp"
#include "core/gravity/oblateness_gravity.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/navigation/mission.hpp"
#include "core/relativity/light_time.hpp"
#include "core/relativity/optics.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/conversions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <numbers>
#include <utility>

namespace sf::app {
namespace {

math::Vec3 column(const math::Mat3& m, int c) { return math::Vec3{m.at(0, c), m.at(1, c), m.at(2, c)}; }

}  // namespace

std::optional<math::Vec3> FlightDirections::by_name(const std::string& key) const {
    if (key == "prograde") return prograde;
    if (key == "retrograde") return retrograde;
    if (key == "normal") return normal;
    if (key == "anti_normal") return anti_normal;
    if (key == "radial_out") return radial_out;
    if (key == "radial_in") return radial_in;
    if (key == "nose") return nose;
    if (key == "target") return target;
    if (key == "anti_target") return anti_target;
    if (key == "sun") return sun;
    return std::nullopt;
}

FlightSession::FlightSession() = default;

FlightSession::~FlightSession() {
    // A worker still holding pointers into members that are about to be
    // destroyed is the one way this class can crash the application. Ask it to
    // stop, then WAIT: cancellation is checked between candidates, so the wait is
    // at most one flight long.
    if (job_ != nullptr) {
        job_->cancel.store(true);
        join_worker();
    }
}

void FlightSession::report(const std::string& message, bool is_error) const {
    if (sink_) {
        sink_(message, is_error);
        return;
    }
    std::fprintf(stderr, "%s: %s\n", is_error ? "ERROR" : "WARNING", message.c_str());
}

// Every public entry point that can reach a throwing core call funnels through
// this. The presentation cannot do anything useful with an exception thrown from
// inside a frame, so a failure becomes a logged error and a false return, which
// is what a caller can actually handle.
template <typename Fn>
bool FlightSession::guarded(const char* what, Fn&& fn) {
    try {
        fn();
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string{what} + ": " + e.what();
        report(last_error_, true);
        return false;
    } catch (...) {
        last_error_ = std::string{what} + ": unknown error";
        report(last_error_, true);
        return false;
    }
}

bool FlightSession::configure(const std::string& kernel_directory, const std::string& epoch_utc) {
    return guarded("configure", [&] {
        kernels_ = std::make_shared<const ephemeris::SpiceKernelSet>(
            ephemeris::SpiceKernelSet::from_directory(kernel_directory));
        provider_ = std::make_unique<ephemeris::SpiceEphemerisProvider>(kernels_);
        time_converter_ = std::make_unique<ephemeris::SpiceTimeConverter>(kernels_);

        catalog_ = std::make_unique<celestial::BodyCatalog>(
            celestial::BodyCatalog::default_solar_system(*provider_));

        const auto frame = coordinates::ReferenceFrame::ssb_j2000();
        forces_ = std::make_unique<gravity::CompositeForceModel>();
        forces_->add(std::make_unique<gravity::PointMassGravity>(*provider_, *catalog_, frame));
        forces_->add(gravity::OblatenessGravity::for_body(*provider_, *provider_,
                                                           celestial::bodies::earth, frame));

        // Attitude: a 1000 kg box 8 x 3 x 3 m, with twelve RCS thrusters in six
        // couples on a 2 m arm. Numbers chosen to be plausible, not fitted.
        inertia_ = std::make_unique<attitude::InertiaTensor>(
            attitude::InertiaTensor::solid_box(1000.0, math::Vec3{8.0, 3.0, 3.0}));
        // RCS on the same torch technology as the main engine: a thousand times
        // the exhaust velocity and a thousandth of the flow, so each thruster
        // still pushes with 180 N and a slew costs a thousandth of the propellant.
        const propulsion::EngineSpec rcs_thruster{"RCS", 2.0e-5, 3.0e-2, 1.0};
        rcs_ = std::make_unique<attitude::RcsSystem>(attitude::RcsSystem::couples(2.0, rcs_thruster));
        pointing_ = std::make_unique<attitude::PointingController>(*provider_, *inertia_, frame);
        rcs_force_ = std::make_unique<attitude::RcsForce>(*rcs_, *pointing_);
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
        const propulsion::MultiModeEngine main{
            {{"IMPULSE", propulsion::EngineSpec{"IMPULSE", 0.0222376, 0.03, 1.0}},
             {"CRUISE", propulsion::EngineSpec{"CRUISE", 7.470950e-05, 0.5, 1.0}}}};
        craft_ = std::make_unique<spacecraft::Spacecraft>("Torch", 1000.0, 19000.0, main);
        main_engine_ = std::make_unique<propulsion::MainEngineForce>(*craft_);
        forces_->add_reference(*main_engine_);

        // The maneuver executor is built EMPTY and wired in now, once. It is what
        // flies a planned mission; with no plan it contributes nothing.
        plan_ = std::make_unique<navigation::ManeuverPlan>();
        executor_ = std::make_unique<navigation::ManeuverExecutor>(*provider_, *craft_, *plan_, frame);
        forces_->add_reference(*executor_);

        const auto epoch_time = time_converter_->parse(epoch_utc);

        propagation::IntegratorConfig config{};
        config.relative_tolerance = 1.0e-11;
        config.absolute_tolerance_position = 1.0e-3;
        config.absolute_tolerance_velocity = 1.0e-6;
        config.absolute_tolerance_proper_time = 1.0e-9;
        config.minimum_mass = craft_->dry_mass();
        config.max_step = time::Duration::seconds(3600.0);
        propagator_ = std::make_unique<propagation::DormandPrince54Propagator>(*forces_, config);
        propagator_->set_inertia(inertia_.get());

        clock_ = std::make_unique<simulation::SimulationClock>(epoch_time);

        builder_ = std::make_unique<simulation::SnapshotBuilder>(
            *provider_, *catalog_, *forces_, celestial::bodies::earth, epoch_time, frame);

        // What EXISTS, as opposed to whose mass is in the force model above.
        // Resolved at the scenario epoch because availability is a statement
        // about a time span: mar099s.bsp covers 1995-2050 and a game set in 2075
        // has to be told that now rather than when the pilot selects Mars.
        system_ = std::make_unique<celestial::SolarSystem>(
            celestial::SolarSystem::resolve(*provider_, epoch_time));

        std::vector<simulation::DisplayBody> display;
        for (const auto id : system_->renderable()) {
            const auto* body = system_->find(id);
            display.push_back(simulation::DisplayBody{body->entry.id, std::string{body->entry.name},
                                                      body->ephemeris_source, body->gm,
                                                      body->radius});
        }
        builder_->set_display_bodies(std::move(display));

        builder_->set_target(celestial::bodies::moon);
        // 900 kg dry + 100 kg of RCS propellant, burnt through the thruster's own
        // v_eff. Without this the propellant readout would sit at zero while the
        // thrusters fired, which is the sort of quiet lie this project exists to
        // avoid.
        builder_->set_propulsion(craft_->dry_mass(), craft_->engine().effective_exhaust_velocity());

        state_ = propagation::PropagationState{};
        state_.mass = craft_->initial_mass();
    });
}

bool FlightSession::start_circular_orbit(double altitude_m, double inclination_deg) {
    if (builder_ == nullptr) {
        last_error_ = "start_circular_orbit: configure() first";
        report(last_error_, true);
        return false;
    }

    return guarded("start_circular_orbit", [&] {
        const auto frame = coordinates::ReferenceFrame::ssb_j2000();
        const auto earth = celestial::bodies::earth;
        const auto t = clock_->coordinate_time();

        const double radius = provider_->mean_radius(earth) + altitude_m;
        const double gm = provider_->gravitational_parameter(earth);
        const double speed = trajectory::circular_speed(gm, radius);
        const double inclination = units::deg_to_rad(inclination_deg);

        const auto earth_state = provider_->state(earth, t, frame);
        state_.state.position = earth_state.state.position + math::Vec3{radius, 0.0, 0.0};
        state_.state.velocity = earth_state.state.velocity +
                                math::Vec3{0.0, speed * std::cos(inclination),
                                           speed * std::sin(inclination)};
        state_.proper_time = time::Duration::zero();
        state_.mass = craft_->initial_mass();
        state_.attitude = attitude::AttitudeState{};

        rebuild_snapshot();
        focus_on_spacecraft();
    });
}

bool FlightSession::align_attitude_to_flight(double nadir_bias_deg) {
    if (builder_ == nullptr) {
        last_error_ = "align_attitude_to_flight: configure() first";
        return false;
    }
    return guarded("align_attitude_to_flight", [&] {
        const auto& craft = snapshot_.spacecraft;
        const math::Vec3 forward = craft.relative_velocity.normalized();
        const math::Vec3 outward = craft.relative_position.normalized();
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
        const math::Vec3 side = cross(outward, forward).normalized();
        const math::Vec3 up = cross(forward, side).normalized();

        // The matrix whose COLUMNS are the body axes in the inertial frame,
        // which is exactly what from_rotation_matrix documents itself as taking.
        math::Mat3 basis{};
        const math::Vec3 columns[3] = {forward, side, up};
        for (int row = 0; row < 3; ++row) {
            basis.m[static_cast<std::size_t>(row)] = {columns[0][row], columns[1][row], columns[2][row]};
        }
        auto orientation = math::Quaternion::from_rotation_matrix(basis).normalized();

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
            const auto pitch = math::Quaternion::from_axis_angle(math::Vec3::unit_y(),
                                                                 units::Angle::degrees(nadir_bias_deg));
            orientation = (orientation * pitch).normalized();
        }

        state_.attitude.orientation = orientation;
        state_.attitude.angular_velocity = math::Vec3{};
        rebuild_snapshot();
    });
}

void FlightSession::set_time_warp(double warp) {
    if (clock_ == nullptr) {
        return;
    }
    guarded("set_time_warp", [&] { clock_->set_time_warp(warp); });
}

double FlightSession::time_warp() const { return clock_ != nullptr ? clock_->time_warp() : 1.0; }

void FlightSession::advance(double wall_seconds) {
    if (builder_ == nullptr || wall_seconds <= 0.0) {
        return;
    }

    guarded("advance", [&] {
        const auto wall = time::Duration::seconds(wall_seconds);
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
        propagation::PropagationResult result{};
        if (executor_ != nullptr && plan_ != nullptr && !plan_->empty()) {
            const auto mission = navigation::run_mission(*propagator_, *executor_, state_,
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
                pointing_ != nullptr ? pointing_->pointing_error(state_, clock_->coordinate_time()) : 0.0;
            mission_.update(*provider_, clock_->coordinate_time(), state_, units::Angle::radians(error));

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
            last_error_ = "advance: " + propagation::to_string(result.status) + " -- " + result.message;
            report(last_error_, false);
        }

        rebuild_snapshot();
    });
}

celestial::BodyId FlightSession::natural_reference() const {
    // Computed from the snapshot that was JUST BUILT, and therefore from
    // positions the frame has already paid for.
    //
    // ⚠️ The first version queried the ephemeris itself -- the Sun, eight
    // planets, then the children of whichever won -- about forty extra spkez
    // calls per frame on top of the thirteen the snapshot already makes. Every
    // one of them takes CSPICE's global mutex, which is the same mutex the
    // planning worker needs, and the effect was not subtle: an Earth-Mars search
    // that takes 64 seconds from the command line had not finished ranking its
    // candidates after four thousand frames of an uncapped headless run. The
    // frame loop was starving the worker.
    //
    // Reading the snapshot costs nothing and the answer is one frame stale,
    // which for a label that changes three times in two hundred days is not a
    // difference anyone can see.
    if (system_ == nullptr || snapshot_.bodies.empty()) {
        return celestial::bodies::earth;
    }

    auto position_of = [&](celestial::BodyId id) -> const math::Vec3* {
        const auto* body = snapshot_.find(id);
        return body != nullptr ? &body->position : nullptr;
    };

    const math::Vec3 ship = snapshot_.spacecraft.position;
    const auto* current = system_->find(celestial::bodies::sun);
    if (current == nullptr || position_of(current->entry.id) == nullptr) {
        return celestial::bodies::earth;
    }

    // Walk DOWN from the Sun: the Sun, then whichever planet's neighbourhood
    // holds the ship, then whichever of that planet's moons does. The first
    // level that has no match ends the walk, so a ship inside Mars's
    // neighbourhood but nowhere near Phobos gets Mars.
    //
    // "Neighbourhood" is r = R (m/M)^(2/5) against the body's own parent. It is a
    // LENGTH SCALE and not a boundary: nothing in the dynamics knows it exists,
    // gravity stays multibody, and what it decides is which body a readout is
    // labelled against (rule 65).
    for (int depth = 0; depth < 4; ++depth) {
        const auto* parent_position = position_of(current->entry.id);
        if (parent_position == nullptr || !(current->gm > 0.0)) {
            break;
        }
        const celestial::SolarSystemBody* next = nullptr;
        double best = std::numeric_limits<double>::infinity();
        for (const auto id : celestial::children_of(current->entry.id)) {
            const auto* child = system_->find(id);
            const auto* child_position = position_of(id);
            if (child == nullptr || child_position == nullptr || !(child->gm > 0.0)) {
                continue;
            }
            const double separation = (*child_position - *parent_position).norm();
            const double influence = separation * std::pow(child->gm / current->gm, 0.4);
            const double distance = (ship - *child_position).norm();
            if (influence > 0.0 && distance < influence && distance < best) {
                best = distance;
                next = child;
            }
        }
        if (next == nullptr) {
            break;
        }
        current = next;
    }
    return current->entry.id;
}

bool FlightSession::set_reference_body(const std::string& name) {
    if (builder_ == nullptr || system_ == nullptr) {
        last_error_ = "set_reference_body: configure() first";
        return false;
    }
    const auto* body = system_->find(name);
    if (body == nullptr || !body->has_ephemeris) {
        last_error_ = "set_reference_body: no body named \"" + name + "\"";
        return false;
    }
    auto_reference_ = false;
    builder_->set_reference(body->entry.id);
    rebuild_snapshot();
    return true;
}

std::string FlightSession::reference_body() const { return snapshot_.spacecraft.reference.name(); }

void FlightSession::set_auto_reference(bool enabled) {
    auto_reference_ = enabled;
    if (enabled) {
        rebuild_snapshot();
    }
}

bool FlightSession::auto_reference() const { return auto_reference_; }

void FlightSession::rebuild_snapshot() {
    if (builder_ == nullptr) {
        return;
    }
    builder_->set_time_warp(clock_->time_warp());
    snapshot_ = builder_->build(state_, clock_->coordinate_time());

    // AFTER the build, for the NEXT one: natural_reference() reads the snapshot's
    // own body positions rather than asking the ephemeris again. See the note
    // there for what that costs and what the alternative cost.
    if (auto_reference_) {
        const auto wanted = natural_reference();
        if (wanted != snapshot_.spacecraft.reference) {
            builder_->set_reference(wanted);
            snapshot_ = builder_->build(state_, clock_->coordinate_time());
        }
    }
}

void FlightSession::set_render_scale(double scale) {
    guarded("set_render_scale", [&] { transform_.set_scale(scale); });
}

double FlightSession::render_scale() const { return transform_.scale(); }

void FlightSession::set_body_scale_exaggeration(double factor) {
    guarded("set_body_scale_exaggeration", [&] { transform_.set_body_scale_exaggeration(factor); });
}

void FlightSession::focus_on_spacecraft() { transform_.set_camera_origin(snapshot_.spacecraft.position); }

void FlightSession::focus_on_body(int index) {
    if (index < 0 || index >= static_cast<int>(snapshot_.bodies.size())) {
        return;
    }
    transform_.set_camera_origin(snapshot_.bodies[static_cast<std::size_t>(index)].position);
}

int FlightSession::body_count() const { return static_cast<int>(snapshot_.bodies.size()); }

std::string FlightSession::body_name(int index) const {
    if (index < 0 || index >= body_count()) {
        return {};
    }
    return snapshot_.bodies[static_cast<std::size_t>(index)].name;
}

int FlightSession::body_index(const std::string& name) const {
    for (int i = 0; i < body_count(); ++i) {
        if (snapshot_.bodies[static_cast<std::size_t>(i)].name == name) {
            return i;
        }
    }
    return -1;
}

RenderVec3 FlightSession::body_position(int index) const {
    if (index < 0 || index >= body_count()) {
        return {};
    }
    return transform_.to_render(snapshot_.bodies[static_cast<std::size_t>(index)].position);
}

double FlightSession::body_radius(int index) const {
    if (index < 0 || index >= body_count()) {
        return 0.0;
    }
    return static_cast<double>(
        transform_.radius_to_render(snapshot_.bodies[static_cast<std::size_t>(index)].radius));
}

RenderVec3 FlightSession::spacecraft_position() const {
    return transform_.to_render(snapshot_.spacecraft.position);
}

math::Vec3 FlightSession::spacecraft_velocity_direction() const {
    return snapshot_.spacecraft.relative_velocity.normalized();
}

math::Vec3 FlightSession::beta_vector() const { return optics_observer_velocity() / units::c; }

math::Vec3 FlightSession::optics_observer_velocity() const {
    if (visual_test_beta_ < 0.0) {
        return snapshot_.spacecraft.velocity;
    }
    auto direction = snapshot_.spacecraft.velocity.normalized();
    if (direction.norm_squared() == 0.0) {
        direction = math::Vec3::unit_x();
    }
    return direction * (visual_test_beta_ * units::c);
}

void FlightSession::set_visual_test_beta(double beta) {
    if (beta < 0.0) {
        visual_test_beta_ = -1.0;
    } else if (beta < 1.0 && std::isfinite(beta)) {
        visual_test_beta_ = beta;
    }
}

double FlightSession::visual_test_beta() const { return visual_test_beta_; }

RenderVec3 FlightSession::body_apparent_position(int index) const {
    return body_observed_position(index, apparent_positions_, apparent_positions_);
}

RenderVec3 FlightSession::body_observed_position(int index, bool retarded, bool aberration) const {
    if (index < 0 || index >= body_count() || builder_ == nullptr) {
        return {};
    }
    if (!retarded && !aberration) {
        return body_position(index);
    }

    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    const auto& observer = snapshot_.spacecraft;

    math::Vec3 relative = body.position - observer.position;
    if (retarded) {
        try {
            relative = relativity::apparent_position(*provider_, body.ephemeris_source, observer.position,
                                                     snapshot_.time,
                                                     coordinates::ReferenceFrame::ssb_j2000())
                           .relative_position;
        } catch (const std::exception&) {
            // A body whose light-time solution leaves kernel coverage is drawn
            // where it geometrically is: an approximation that is visible, not
            // a crash in the middle of a frame.
        }
    }

    // Aberration turns the DIRECTION; the distance is the retarded one.  Rebuilt
    // as direction x distance rather than transformed as a position, because
    // aberration is a map of the celestial sphere and nothing else.
    const math::Vec3 beta = optics_observer_velocity() / units::c;
    const double distance = relative.norm();
    const math::Vec3 direction =
        aberration ? relativity::aberrate_source_direction(relative, beta) : relative.normalized();

    // Back to an absolute position so that the SAME RenderTransform -- the same
    // floating origin, the same scale -- handles it (rendering.md section 2).
    return transform_.to_render(observer.position + direction * distance);
}

double FlightSession::body_light_time(int index) const {
    if (index < 0 || index >= body_count() || builder_ == nullptr) {
        return 0.0;
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    try {
        return relativity::apparent_position(*provider_, body.ephemeris_source,
                                             snapshot_.spacecraft.position, snapshot_.time,
                                             coordinates::ReferenceFrame::ssb_j2000())
            .light_time;
    } catch (const std::exception&) {
        return 0.0;
    }
}

double FlightSession::body_doppler(int index) const {
    if (index < 0 || index >= body_count() || builder_ == nullptr) {
        return 1.0;
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    const auto& observer = snapshot_.spacecraft;

    // The body moves too, so the Doppler factor is the one of the RELATIVE
    // motion: the observer's velocity minus the source's, over c.  Using the
    // observer's velocity alone would make a co-moving planet blue.
    const math::Vec3 beta = (optics_observer_velocity() - body.velocity) / units::c;
    const math::Vec3 to_source = body.position - observer.position;
    if (to_source.norm_squared() <= 0.0) {
        return 1.0;
    }
    return relativity::doppler_factor_to_source(to_source, beta);
}

RenderVec3 FlightSession::body_relative_velocity_scene(int index) const {
    if (index < 0 || index >= body_count()) {
        return {};
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    const auto relative = body.velocity - optics_observer_velocity();
    // Scene units per second: vector_to_render scales without translating, which
    // is exactly right for a velocity and wrong for a position.
    return transform_.vector_to_render(relative);
}

double FlightSession::light_speed_scene() const {
    // The SAME scale as the velocity above, so that |v|/c is preserved exactly
    // and the structural |v| < c survives the conversion to float
    // (docs/architecture/relativistic-shaders.md section 4).
    return units::c * transform_.scale();
}

void FlightSession::set_apparent_positions_enabled(bool enabled) { apparent_positions_ = enabled; }

bool FlightSession::apparent_positions_enabled() const { return apparent_positions_; }

math::Quaternion FlightSession::spacecraft_orientation() const { return snapshot_.spacecraft.orientation; }

BodyAxes FlightSession::spacecraft_axes() const {
    const auto& q = snapshot_.spacecraft.orientation;
    return BodyAxes{q.rotate(math::Vec3::unit_x()), q.rotate(math::Vec3::unit_y()),
                    q.rotate(math::Vec3::unit_z())};
}

bool FlightSession::set_pointing_mode(const std::string& mode) {
    if (pointing_ == nullptr) {
        return false;
    }
    // An armed mission steers the ship, and the next frame would overwrite this
    // command anyway (see advance()). Refusing says so; accepting and then
    // silently undoing it would leave the cockpit showing a mode the ship is not
    // in -- the same class of quiet lie as a label that keeps saying "prograde"
    // after the ship has stopped being prograde.
    if (mission_.armed() && mission_.pointing_command().has_value()) {
        last_error_ = "the flight computer is steering: abandon the plan (K) to take the attitude back";
        report(last_error_, false);
        return false;
    }

    attitude::PointingCommand command{};
    command.reference = celestial::bodies::earth;
    if (!mode.empty()) {
        const auto parsed = navigation::guidance_from_string(mode);
        if (!parsed.has_value()) {
            last_error_ = "unknown pointing mode \"" + mode + "\"";
            report(last_error_, true);
            return false;
        }
        command.mode = *parsed;
    }
    pointing_->set_command(command);
    return true;
}

std::string FlightSession::pointing_mode() const {
    if (pointing_ == nullptr || !pointing_->command().mode.has_value()) {
        return "HOLD";
    }
    return std::string{navigation::to_string(*pointing_->command().mode)};
}

double FlightSession::pointing_error_deg() const {
    if (pointing_ == nullptr || builder_ == nullptr) {
        return 0.0;
    }
    return units::rad_to_deg(pointing_->pointing_error(state_, clock_->coordinate_time()));
}

void FlightSession::set_manual_torque(const math::Vec3& torque_body) {
    if (rcs_force_ != nullptr) {
        rcs_force_->set_manual_torque(torque_body);
    }
}

void FlightSession::set_manual_translation(const math::Vec3& force_body) {
    if (rcs_force_ != nullptr) {
        rcs_force_->set_manual_force(force_body);
    }
}

bool FlightSession::set_engine_mode(const std::string& mode) {
    if (craft_ == nullptr) {
        return false;
    }
    return craft_->select_mode(mode);
}

void FlightSession::cycle_engine_mode() {
    if (craft_ != nullptr) {
        craft_->cycle_mode();
    }
}

std::string FlightSession::engine_mode() const { return craft_ != nullptr ? craft_->mode_name() : std::string{}; }

void FlightSession::set_throttle(double throttle) {
    if (main_engine_ != nullptr) {
        main_engine_->set_throttle(throttle);
    }
}

double FlightSession::throttle() const { return main_engine_ != nullptr ? main_engine_->throttle() : 0.0; }

// ---------------------------------------------------------------------------
// Planning on a worker thread.  See the note in flight_session.hpp for why
// there is exactly one of them and what it is allowed to touch.
// ---------------------------------------------------------------------------

bool FlightSession::start_planning_alternative(int index) {
    if (!planned_.ok() || !last_request_.valid) {
        last_error_ = "start_planning_alternative: there is no search to choose from";
        return false;
    }
    if (index < 0 || index >= static_cast<int>(planned_.alternatives.size())) {
        last_error_ = "start_planning_alternative: no alternative " + std::to_string(index);
        return false;
    }
    const auto& alternative = planned_.alternatives[static_cast<std::size_t>(index)];
    if (!alternative.feasible) {
        last_error_ = "start_planning_alternative: \"" + alternative.label + "\" was flown and refused (" +
                      std::string{navigation::to_string(alternative.failure)} + ")";
        report(last_error_, true);
        return false;
    }

    navigation::TransferConfig::PinnedDeparture pin{};
    pin.active = true;
    pin.coast_s = alternative.departure_coast_s;
    pin.time_of_flight_days = alternative.time_of_flight_s / 86400.0;
    pin.direction = alternative.branch;

    return begin_planning(last_request_.target, last_request_.periapsis_altitude_km,
                          last_request_.apoapsis_altitude_km, last_request_.search_hours, pin);
}

bool FlightSession::start_planning(const std::string& target_body, double periapsis_altitude_km,
                                   double apoapsis_altitude_km, double search_hours) {
    const auto lookup = celestial::body_from_name(target_body);
    if (!lookup.ok) {
        last_error_ = "start_planning: no body named \"" + target_body + "\"";
        report(last_error_, true);
        return false;
    }
    return begin_planning(lookup.id, periapsis_altitude_km, apoapsis_altitude_km, search_hours,
                          navigation::TransferConfig::PinnedDeparture{});
}

bool FlightSession::begin_planning(celestial::BodyId target, double periapsis_altitude_km,
                                   double apoapsis_altitude_km, double search_hours,
                                   const navigation::TransferConfig::PinnedDeparture& pin) {
    if (builder_ == nullptr) {
        last_error_ = "start_planning: configure() first";
        return false;
    }
    if (job_ != nullptr && job_->running.load()) {
        last_error_ = "start_planning: a search is already running";
        return false;
    }
    join_worker();

    last_request_.valid = true;
    last_request_.target = target;
    last_request_.periapsis_altitude_km = periapsis_altitude_km;
    last_request_.apoapsis_altitude_km = apoapsis_altitude_km;
    last_request_.search_hours = search_hours;

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
    request.j2_bodies = {celestial::bodies::earth};
    request.integrator = propagator_->config();
    request.initial = state_;
    request.epoch = clock_->coordinate_time();
    request.center = celestial::bodies::earth;
    request.target = target;
    request.target_periapsis_altitude_m = periapsis_altitude_km * 1000.0;
    request.target_apoapsis_altitude_m = apoapsis_altitude_km * 1000.0;
    request.search_window_s = search_hours * 3600.0;
    request.execution = execution_;
    request.pointing = pointing_->gains();
    request.pinned = pin;

    PlanningJob* job = job_.get();
    request.cancelled = [job] { return job->cancel.load(); };
    request.on_progress = [job](const navigation::SearchProgress& progress) {
        const std::lock_guard lock{job->mutex};
        job->progress = progress;
    };

    job->running.store(true);
    job->worker = std::thread([job, request] {
        const auto started = std::chrono::steady_clock::now();
        try {
            auto result = app::plan_transfer(request);
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

bool FlightSession::is_planning() const { return job_ != nullptr && job_->running.load(); }

PlanningProgress FlightSession::planning_progress() const {
    PlanningProgress out{};
    if (job_ == nullptr) {
        return out;
    }
    const std::lock_guard lock{job_->mutex};
    out.present = true;
    out.running = job_->running.load();
    out.cancelled = job_->cancel.load();
    out.stage = job_->progress.stage;
    out.candidates_considered = static_cast<long long>(job_->progress.candidates_considered);
    out.candidates_screened = static_cast<long long>(job_->progress.candidates_screened);
    out.candidates_flown = static_cast<long long>(job_->progress.candidates_flown);
    out.candidates_succeeded = static_cast<long long>(job_->progress.candidates_succeeded);
    out.best_total_delta_v = job_->progress.best_total_delta_v;
    out.integrator_steps = static_cast<long long>(job_->progress.integrator_steps);
    out.wall_seconds = job_->wall_seconds;
    return out;
}

void FlightSession::cancel_planning() {
    if (job_ != nullptr) {
        job_->cancel.store(true);
    }
}

void FlightSession::join_worker() {
    if (job_ != nullptr && job_->worker.joinable()) {
        job_->worker.join();
    }
}

PlanSummary FlightSession::collect_plan() {
    PlanSummary empty{};
    if (job_ == nullptr || job_->running.load()) {
        return empty;
    }
    join_worker();

    const bool cancelled = job_->cancel.load();
    std::string error;
    navigation::MissionPlanResult result{};
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
        report(last_error_, true);
        return empty;
    }
    if (!have_result) {
        last_error_ = "plan_transfer: the search produced nothing";
        return empty;
    }
    if (result.status == navigation::MissionPlanStatus::Cancelled) {
        last_error_ = "search cancelled";
        empty.cancelled = true;
        return empty;
    }
    if (!result.ok()) {
        last_error_ = result.failure.has_value()
                          ? std::string{result.failure->name()} + ": " + result.failure->detail
                          : std::string{navigation::to_string(result.status)};
        report(last_error_, true);
        empty.cancelled = cancelled;
        return empty;
    }

    // Installed on the FRAME's thread and nowhere else. The worker never writes
    // into planned_, because the cockpit reads it every frame.
    planned_ = std::move(result);
    return plan();
}

PlanSummary FlightSession::plan_transfer(const std::string& target_body, double periapsis_altitude_km,
                                         double apoapsis_altitude_km, double search_hours) {
    if (builder_ == nullptr) {
        last_error_ = "plan_transfer: configure() first";
        return {};
    }

    guarded("plan_transfer", [&] {
        const auto lookup = celestial::body_from_name(target_body);
        if (!lookup.ok) {
            throw std::invalid_argument("no body named \"" + target_body + "\"");
        }

        SceneTransferRequest request{};
        request.provider = provider_.get();
        request.orientation = provider_.get();
        request.catalog = catalog_.get();
        request.craft = craft_.get();
        // The same perturbation set the propagator itself runs with. Planning
        // against a different force model than the one that flies the result is
        // how a corrected trajectory stops being corrected.
        request.j2_bodies = {celestial::bodies::earth};
        request.integrator = propagator_->config();
        request.initial = state_;
        request.epoch = clock_->coordinate_time();
        request.center = celestial::bodies::earth;
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
        auto result = app::plan_transfer(request);
        if (!result.ok()) {
            throw std::runtime_error(result.failure.has_value()
                                         ? std::string{result.failure->name()} + ": " +
                                               result.failure->detail
                                         : std::string{navigation::to_string(result.status)});
        }

        planned_ = std::move(result);
    });
    // Built from the plan rather than cached separately: the summary and the
    // readout have to be the same thing, and two copies of it would be two
    // things that can disagree.
    return plan();
}

PlanSummary FlightSession::plan() const {
    PlanSummary out{};
    if (!planned_.ok() || clock_ == nullptr) {
        return out;
    }
    const auto& m = planned_.metrics;
    const auto now = clock_->coordinate_time();

    out.valid = true;
    out.target = std::string{m.destination.name()};
    out.origin = std::string{m.origin.name()};
    out.departure_tdb_s = m.departure.seconds_since_j2000();
    out.arrival_tdb_s = m.arrival.seconds_since_j2000();
    // Armed, these come from the mission runner, which knows which leg is next.
    // Not armed, the plan is a proposal and the countdown has to come from its
    // own epochs -- otherwise the review screen would show a departure "in
    // 0.0 s" for a burn ninety minutes away, and the pilot would press EXECUTE
    // on a number that meant nothing.
    if (mission_.armed()) {
        out.seconds_to_ignition = mission_.seconds_to_injection(now);
        out.seconds_to_insertion = mission_.seconds_to_capture(now);
    } else {
        out.seconds_to_ignition = (m.departure - now).seconds();
        out.seconds_to_insertion = (m.arrival - now).seconds();
    }
    out.armed = mission_.armed();
    out.time_of_flight_s = m.time_of_flight_s;
    out.time_of_flight_days = m.time_of_flight_s / 86400.0;
    out.transfer_angle_deg = m.transfer_angle.degrees();
    out.branch = m.branch == trajectory::TransferDirection::Prograde ? "prograde" : "retrograde";

    out.injection_delta_v = m.injection_delta_v;
    out.midcourse_delta_v = m.midcourse_delta_v;
    out.insertion_delta_v = m.capture_delta_v;
    out.total_delta_v = m.total_delta_v;
    out.delta_v_available = m.delta_v_available;
    out.injection_duration_s = m.injection_duration_s;
    out.insertion_duration_s = m.capture_duration_s;

    out.v_infinity = m.v_infinity;
    out.flyby_periapsis_m = m.predicted_flyby_periapsis;
    out.predicted_periapsis_m = m.predicted_periapsis_altitude;
    out.predicted_apoapsis_m = m.predicted_apoapsis_altitude;
    out.predicted_eccentricity = m.predicted_eccentricity;
    out.predicted_inclination_deg = m.predicted_inclination.degrees();
    out.predicted_raan_deg = m.predicted_raan.degrees();

    out.propellant_required_kg = m.propellant_required;
    out.propellant_remaining_kg = m.propellant_remaining;

    // What the autopilot is expected to do while the capture burn runs. Shown
    // because section 10 is explicit that a controller is not judged on the
    // orbit alone.
    out.pointing_error_mean_deg = m.pointing_error_mean.degrees();
    out.pointing_error_peak_deg = m.pointing_error_peak.degrees();
    out.rcs_propellant_kg = m.rcs_propellant;
    out.rcs_duty_cycle = m.rcs_duty_cycle;
    out.torque_saturation = m.torque_saturation;

    out.phase = mission_phase();
    // The burn COUNT is a property of the plan, armed or not. It used to be read
    // off the executor's list, which is empty until arm_plan() installs it -- so
    // the review screen announced "0 burns" for a two-burn transfer it was
    // showing the delta-v of, one line above.
    out.burns = static_cast<int>(planned_.maneuvers.size());
    if (plan_ != nullptr && !plan_->empty()) {
        const auto& first = plan_->maneuvers().front();
        const auto& last = plan_->maneuvers().back();
        out.burning = first.active_at(now) || last.active_at(now);
        out.done = now >= last.cutoff();
    }
    return out;
}

std::vector<PlanAlternative> FlightSession::plan_alternatives() const {
    std::vector<PlanAlternative> out;
    for (const auto& alternative : planned_.alternatives) {
        PlanAlternative entry{};
        entry.label = alternative.label;
        entry.feasible = alternative.feasible;
        entry.departure_tdb_s = alternative.departure.seconds_since_j2000();
        entry.time_of_flight_days = alternative.time_of_flight_s / 86400.0;
        entry.time_of_flight_s = alternative.time_of_flight_s;
        entry.departure_coast_s = alternative.departure_coast_s;
        entry.branch =
            alternative.branch == trajectory::TransferDirection::Prograde ? "prograde" : "retrograde";
        entry.injection_delta_v = alternative.injection_delta_v;
        entry.insertion_delta_v = alternative.capture_delta_v;
        entry.capture_delta_v = alternative.capture_delta_v;
        entry.total_delta_v = alternative.total_delta_v;
        entry.predicted_periapsis_m = alternative.predicted_periapsis_altitude;
        entry.predicted_apoapsis_m = alternative.predicted_apoapsis_altitude;
        entry.predicted_eccentricity = alternative.predicted_eccentricity;
        entry.predicted_inclination_deg = alternative.predicted_inclination.degrees();
        entry.predicted_raan_deg = alternative.predicted_raan.degrees();
        entry.failure = std::string{navigation::to_string(alternative.failure)};
        out.push_back(std::move(entry));
    }
    return out;
}

std::string FlightSession::mission_phase() const { return std::string{navigation::to_string(mission_.phase())}; }

MissionOutcomeView FlightSession::mission_outcome() const {
    MissionOutcomeView out{};
    const auto& outcome = mission_.outcome();
    if (!outcome.recorded) {
        return out;
    }
    const auto row = [](const navigation::PredictedVersusActual& value) {
        return PredictedActual{value.predicted, value.actual, value.difference(), value.recorded};
    };
    out.recorded = true;
    out.periapsis_m = row(outcome.periapsis_altitude);
    out.apoapsis_m = row(outcome.apoapsis_altitude);
    out.eccentricity = row(outcome.eccentricity);
    out.inclination_deg = row(outcome.inclination_deg);
    out.propellant_kg = row(outcome.propellant_used);
    out.arrival_tdb_s = row(outcome.arrival_tdb_s);
    out.capture_delta_v = row(outcome.capture_delta_v);
    out.note = outcome.note;
    return out;
}

FlightDirections FlightSession::flight_directions() const {
    FlightDirections out{};
    if (pointing_ == nullptr || clock_ == nullptr) {
        return out;
    }
    const auto t = clock_->coordinate_time();
    const auto reference = snapshot_.spacecraft.reference;

    const auto direction = [&](navigation::GuidanceMode mode) {
        return pointing_->direction_for(mode, reference, state_, t);
    };
    out.prograde = direction(navigation::GuidanceMode::Prograde);
    out.retrograde = direction(navigation::GuidanceMode::Retrograde);
    out.normal = direction(navigation::GuidanceMode::Normal);
    out.anti_normal = direction(navigation::GuidanceMode::AntiNormal);
    out.radial_out = direction(navigation::GuidanceMode::RadialOut);
    out.radial_in = direction(navigation::GuidanceMode::RadialIn);

    // The nose, straight off the integrated quaternion. Not derived from
    // anything on screen: it is where the hull IS pointing, which is what the
    // error between it and a marker means.
    out.nose = state_.attitude.orientation.rotate(math::Vec3::unit_x());

    // Target and Sun are directions to a PLACE rather than guidance laws, so
    // they are geometry here rather than a call to the controller -- and they
    // are geometric in the controller too, which is why "point at the target"
    // is not one of its modes.
    if (snapshot_.spacecraft.target.has_value()) {
        if (const auto* body = snapshot_.find(*snapshot_.spacecraft.target); body != nullptr) {
            const auto to_target = (body->position - snapshot_.spacecraft.position).normalized();
            out.target = to_target;
            out.anti_target = to_target * -1.0;
        }
    }
    if (const auto* sun = snapshot_.find(celestial::bodies::sun); sun != nullptr) {
        out.sun = (sun->position - snapshot_.spacecraft.position).normalized();
    }
    return out;
}

BodyAxes FlightSession::body_orientation(int index) const {
    if (index < 0 || index >= body_count() || provider_ == nullptr) {
        return {};
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    math::Mat3 rotation = math::Mat3::identity();
    try {
        // The BODY's own id and not its ephemeris source: a body-fixed frame comes
        // from the text PCK, which knows IAU_JUPITER whether or not any SPK can
        // say where Jupiter is. Rotating a planet by its barycentre's frame would
        // be asking a different question and getting the identity for an answer.
        rotation = provider_->body_fixed_rotation(body.id, snapshot_.time, coordinates::FrameAxes::J2000);
    } catch (const std::exception&) {
        // A barycentre, or a body whose PCK is not loaded. The identity draws it
        // unrotated, which is a visible approximation rather than a crash -- the
        // same policy the whole project applies to missing kernels.
        return {};
    }
    // ⚠️ The body-fixed axes are the COLUMNS of the SPICE matrix.
    //
    // The GDExtension's first version passed the SPICE ROWS while a comment
    // right above it said "columns", and the result was the TRANSPOSE: the
    // inverse rotation. With a flat-coloured sphere nobody could see it. With a
    // texture on it, the Earth turns backwards and the prime meridian is in the
    // wrong place, which is how it was found.
    //
    // Checked against something outside this code: at 2026-01-01 00:00 UTC the
    // sub-solar point must be near 180 degrees east, because solar noon at
    // Greenwich is 12:00 UTC, and near 23 degrees SOUTH, because it is January.
    // tests/presentation/test_presentation_orientation.cpp computes both from
    // this matrix:
    //
    //     sub-solar  longitude 180.92 east   latitude -23.01
    return BodyAxes{column(rotation, 0), column(rotation, 1), column(rotation, 2)};
}

std::vector<std::string> FlightSession::selectable_targets() const {
    std::vector<std::string> out;
    if (system_ == nullptr) {
        return out;
    }
    // Everything the display can DRAW, which is what a navigation target is:
    // somewhere to measure a distance and a relative speed against. Wider than
    // the set of destinations -- the Sun is a perfectly good thing to point the
    // camera at and a very poor thing to capture into orbit around -- and the
    // two lists are separate for exactly that reason.
    for (const auto id : system_->renderable()) {
        out.emplace_back(system_->find(id)->entry.name);
    }
    return out;
}

std::vector<BodyDirectoryEntry> FlightSession::body_directory() const {
    std::vector<BodyDirectoryEntry> out;
    if (system_ == nullptr) {
        return out;
    }
    for (const auto& body : system_->bodies()) {
        BodyDirectoryEntry entry{};
        entry.name = std::string{body.entry.name};
        entry.naif_id = body.entry.id.naif_id();
        entry.type = std::string{celestial::to_string(body.entry.type)};
        entry.parent = body.entry.parent.naif_id();
        const auto* parent = system_->find(body.entry.parent);
        entry.parent_name = parent != nullptr ? std::string{parent->entry.name} : std::string{"Sun"};
        entry.radius_m = body.radius;
        entry.gm = body.gm;
        entry.has_ephemeris = body.has_ephemeris;
        entry.position_substituted = body.position_from_barycenter;
        entry.can_be_destination = body.can_be_destination();
        entry.mission_support = std::string{celestial::to_string(body.support)};
        out.push_back(std::move(entry));
    }
    return out;
}

bool FlightSession::set_target_body(const std::string& wanted) {
    if (builder_ == nullptr) {
        last_error_ = "set_target_body: configure() first";
        return false;
    }
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
    const auto lookup = celestial::body_from_name(wanted);
    if (lookup.ok) {
        builder_->set_target(lookup.id);
        rebuild_snapshot();
        return true;
    }
    last_error_ = "set_target_body: no body named \"" + wanted + "\"";
    report(last_error_, true);
    return false;
}

std::string FlightSession::target_body() const {
    if (!snapshot_.spacecraft.target.has_value()) {
        return {};
    }
    return snapshot_.spacecraft.target->name();
}

std::vector<ThrusterView> FlightSession::rcs_thrusters() const {
    std::vector<ThrusterView> out;
    if (rcs_ == nullptr) {
        return out;
    }
    for (const auto& thruster : rcs_->thrusters()) {
        // Body frame, metres and unit vector. NOT through RenderTransform: these
        // are offsets on a hull the renderer draws at its own size, and scaling
        // them by 1e-6 would put every thruster at the origin.
        //
        // The direction is the FORCE ON THE SHIP. The exhaust leaves the other
        // way, and the visual has to flip it -- said here once so that the
        // renderer is not left to guess which convention this is.
        out.push_back(ThrusterView{thruster.name, thruster.position, thruster.direction});
    }
    return out;
}

std::vector<double> FlightSession::rcs_throttles() const {
    if (rcs_force_ == nullptr || clock_ == nullptr) {
        return {};
    }
    // Not a second copy of the allocation: RcsForce::throttles IS what
    // RcsForce::evaluate feeds to the RCS, and the renderer asks it the same
    // question at the snapshot's own epoch. There is no arrangement of code in
    // which the drawn jet and the burnt propellant disagree.
    const auto open = rcs_force_->throttles(state_, clock_->coordinate_time());
    return std::vector<double>(open.begin(), open.end());
}

std::vector<RenderVec3> FlightSession::orbit_track(int samples) const {
    std::vector<RenderVec3> out;
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
    const math::Vec3 centre = craft.position - craft.relative_position;

    auto elements = craft.elements;
    const double e = elements.eccentricity;

    // Where the true anomaly is allowed to go.
    //
    // Closed orbit: the whole circle. Hyperbola: the asymptotes sit at
    // +-acos(-1/e), and the radius goes to infinity as they are approached, so
    // the sweep stops short of them. 0.92 of the way is far enough that the arc
    // reads as an escape and near enough that the last sample is still a place
    // the ship could be.
    double from = -std::numbers::pi;
    double to = std::numbers::pi;
    if (e >= 1.0) {
        const double asymptote = std::acos(-1.0 / e);
        from = -0.92 * asymptote;
        to = 0.92 * asymptote;
    }

    out.resize(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const double nu = from + (to - from) * static_cast<double>(i) / static_cast<double>(samples - 1);
        elements.true_anomaly = units::Angle::radians(nu);
        // Kepler lives in core/trajectory, here as everywhere else. This is the
        // inverse the campaign tool uses to build parking orbits from elements;
        // reimplementing it in the presentation would make the drawn orbit a
        // second opinion about the shape of the first.
        const auto sample = trajectory::state_from_elements(elements, reference->gm);
        out[static_cast<std::size_t>(i)] = transform_.to_render(centre + sample.position);
    }
    return out;
}

std::vector<RenderVec3> FlightSession::body_orbit_track(int index, int samples) const {
    std::vector<RenderVec3> out;
    if (provider_ == nullptr || catalog_ == nullptr || samples < 8) {
        return out;
    }
    if (index < 0 || index >= body_count()) {
        return out;
    }
    const auto& body = snapshot_.bodies[static_cast<std::size_t>(index)];
    const auto& craft = snapshot_.spacecraft;
    const auto* reference = catalog_->find(craft.reference);
    if (reference == nullptr || reference->gm <= 0.0 || body.id == craft.reference) {
        return out;
    }

    const auto frame = coordinates::ReferenceFrame::ssb_j2000();
    const auto now = snapshot_.time;

    // How long one lap takes, from the body's own osculating elements about the
    // reference. Not a hard-coded 27.32 days: the same call works for anything
    // in the catalogue, and a number in the code would be a fact about the Moon
    // written down where nothing checks it.
    coordinates::StateVector relative{};
    relative.position = body.position - (craft.position - craft.relative_position);
    relative.velocity = body.velocity - craft.velocity + craft.relative_velocity;
    const auto elements = trajectory::elements_from_state(relative, reference->gm);
    if (!elements.bound || elements.period <= 0.0) {
        return out;
    }

    const math::Vec3 centre = craft.position - craft.relative_position;
    out.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const double fraction = static_cast<double>(i) / static_cast<double>(samples - 1);
        const auto t = now + time::Duration::seconds(elements.period * fraction);
        // The EPHEMERIS, sampled -- not the ellipse. The Moon's path is
        // perturbed by the Sun and by the Earth's figure, and the difference
        // between the two answers is hundreds of kilometres.
        //
        // The SOURCE id, not the body's own: Jupiter is drawn at its system
        // barycentre because no loaded kernel can place 599, and asking for 599
        // here would throw.
        try {
            const auto body_state = provider_->state(body.ephemeris_source, t, frame);
            const auto reference_state = provider_->state(craft.reference, t, frame);
            out.push_back(
                transform_.to_render(centre + (body_state.state.position - reference_state.state.position)));
        } catch (const std::exception&) {
            break;
        }
    }
    return out;
}

std::vector<ManeuverView> FlightSession::maneuvers() const {
    std::vector<ManeuverView> out;
    if (plan_ == nullptr || provider_ == nullptr || clock_ == nullptr) {
        return out;
    }
    const auto now = clock_->coordinate_time();
    for (const auto& maneuver : plan_->maneuvers()) {
        ManeuverView entry{};
        entry.name = maneuver.name;
        entry.guidance = std::string{navigation::to_string(maneuver.guidance)};
        entry.ignition_tdb_s = maneuver.ignition.seconds_since_j2000();
        entry.cutoff_tdb_s = maneuver.cutoff().seconds_since_j2000();
        entry.duration_s = maneuver.duration.seconds();
        entry.throttle = maneuver.throttle;
        entry.seconds_to_ignition = (maneuver.ignition - now).seconds();
        entry.active = maneuver.active_at(now);
        entry.done = now >= maneuver.cutoff();
        // Where it happens, for a marker on the map: the arc the planner flew,
        // at the sample nearest the ignition. The renderer places a dot; it does
        // not work out where the burn is.
        if (planned_.ok() && !planned_.trajectory.samples.empty()) {
            const navigation::TrajectoryPrediction::Sample* nearest = nullptr;
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
                const math::Vec3 anchor = origin_body != nullptr
                                              ? origin_body->position
                                              : snapshot_.spacecraft.position -
                                                    snapshot_.spacecraft.relative_position;
                entry.position = transform_.to_render(anchor + nearest->from_origin);
                entry.located = true;
            }
        }
        out.push_back(std::move(entry));
    }
    return out;
}

// ---------------------------------------------------------------------------
// The Solar System map.  Sun-centred J2000, in metres, and it says so.
// ---------------------------------------------------------------------------

SystemMap FlightSession::system_map() const {
    SystemMap out{};
    if (builder_ == nullptr || provider_ == nullptr) {
        return out;
    }
    out.valid = true;

    // The frame, named, in the payload. Not a comment: a layer that arrives on
    // this map without knowing which frame it is in is the bug Milestone 7 spent
    // a day on, and a string the consumer can read is cheap insurance.
    out.frame = "Sun-centred J2000, metres";
    out.units = "m";

    const auto* sun = snapshot_.find(celestial::bodies::sun);
    const math::Vec3 centre = sun != nullptr ? sun->position : math::Vec3{};
    out.centre = "Sun";

    for (const auto& body : snapshot_.bodies) {
        // Moons are left off the heliocentric map: at this zoom the Moon and the
        // Earth are the same pixel, and Phobos is a label on top of Mars
        // (rule 27). They are still in the snapshot, still selectable, and the
        // LOCAL map draws them.
        const auto* entry = system_ != nullptr ? system_->find(body.id) : nullptr;
        if (entry != nullptr && entry->entry.type == celestial::BodyType::Moon) {
            continue;
        }
        SystemMapBody row{};
        row.name = body.name;
        row.naif_id = body.id.naif_id();
        row.position = body.position - centre;
        row.radius_m = body.radius;
        row.is_sun = body.id == celestial::bodies::sun;
        row.is_origin = planned_.ok() && body.id == planned_.metrics.origin;
        row.is_destination =
            snapshot_.spacecraft.target.has_value() && body.id == *snapshot_.spacecraft.target;
        out.bodies.push_back(std::move(row));
    }

    out.ship_position = snapshot_.spacecraft.position - centre;
    out.ship_velocity = snapshot_.spacecraft.velocity;

    // The planned arc, in the SAME frame, and rebuilt in absolute coordinates on
    // purpose: each sample is placed where the origin body was at that sample's
    // own epoch. That is exactly what planned_trajectory() must NOT do for a
    // planet-centred map and exactly what this one needs -- the Earth really does
    // move half a billion kilometres while the ship is in transit, and a
    // heliocentric map that anchored the arc at today's Earth would draw a
    // transfer that never happened.
    //
    // `sample_index` keeps, for every point that made it into the arc, which
    // trajectory sample it came from, so that a burn can be placed on the arc
    // by epoch without the two lists having to stay the same length.
    std::vector<std::size_t> sample_index;
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();
    if (planned_.ok() && !planned_.trajectory.empty()) {
        out.planned_trajectory.reserve(planned_.trajectory.samples.size());
        for (std::size_t i = 0; i < planned_.trajectory.samples.size(); ++i) {
            const auto& sample = planned_.trajectory.samples[i];
            math::Vec3 absolute{};
            try {
                absolute = provider_->position(planned_.metrics.origin, sample.time, frame) +
                           sample.from_origin;
            } catch (const std::exception&) {
                continue;
            }
            // The Sun moves too -- about one solar radius over a couple of
            // centuries -- so it is read at the SAMPLE's epoch and not at now.
            math::Vec3 sun_then = centre;
            try {
                sun_then = provider_->position(celestial::bodies::sun, sample.time, frame);
            } catch (const std::exception&) {
            }
            out.planned_trajectory.push_back(absolute - sun_then);
            sample_index.push_back(i);
        }
    }

    // Where the origin WAS at departure and where the destination WILL BE at
    // arrival.
    //
    // Rule 30 asks the map to mark the departure and the arrival, and neither is
    // where the body is drawn: the Earth moves 500 million kilometres while the
    // ship is in transit and Mars moves a third of its orbit. An arc that ends
    // nowhere near the Mars marker is not a bug -- it ends where Mars will BE --
    // and the only way a reader can see that is if the map draws both.
    if (planned_.ok()) {
        auto anchor = [&](celestial::BodyId body, time::CoordinateTime t) -> std::optional<math::Vec3> {
            try {
                const auto there = provider_->position(body, t, frame);
                const auto sun_then = provider_->position(celestial::bodies::sun, t, frame);
                return there - sun_then;
            } catch (const std::exception&) {
                return std::nullopt;
            }
        };
        out.origin_at_departure = anchor(planned_.metrics.origin, planned_.metrics.departure);
        out.destination_at_arrival = anchor(planned_.metrics.destination, planned_.metrics.arrival);

        // And where the destination is at the epoch the ARC ENDS, which is not
        // the arrival: the trajectory runs two revolutions past the capture burn
        // so that what it shows is an orbit and not the instant one closed.
        //
        // ⚠️ Three point nine hours, in a heliocentric frame, is 424 000
        // kilometres -- because the Moon orbits the SUN at 30 km/s along with the
        // Earth, and so does everything else on this map. Two positions of the
        // same body at two epochs are never comparable here without saying which
        // epochs, and the difference is not small: it is larger than the
        // Earth-Moon distance.
        if (!planned_.trajectory.empty()) {
            out.destination_at_trajectory_end =
                anchor(planned_.metrics.destination, planned_.trajectory.samples.back().time);
        }
    }

    // The burns, each where the ship will be when it lights.
    if (clock_ != nullptr) {
        const auto now = clock_->coordinate_time();
        for (const auto& maneuver : mission_.plan().maneuvers()) {
            SystemMapManeuver row{};
            row.name = maneuver.name;
            row.seconds_to_ignition = (maneuver.ignition - now).seconds();
            row.done = now >= maneuver.cutoff();
            row.active = maneuver.active_at(now);
            // Located by walking the planned arc to the ignition epoch, which is
            // the only place a future position exists: the map does not propagate.
            const auto& samples = planned_.trajectory.samples;
            for (std::size_t k = 1; k < sample_index.size(); ++k) {
                if (samples[sample_index[k]].time >= maneuver.ignition) {
                    row.position = out.planned_trajectory[k];
                    row.located = true;
                    break;
                }
            }
            out.maneuvers.push_back(std::move(row));
        }
    }

    out.destination = target_body();
    out.phase = mission_phase();
    return out;
}

std::map<std::string, SystemOrbitPath> FlightSession::system_orbit_paths(int samples_per_body) const {
    std::map<std::string, SystemOrbitPath> out;
    if (builder_ == nullptr || provider_ == nullptr || system_ == nullptr) {
        return out;
    }
    const int samples = std::clamp(samples_per_body, 8, 512);
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();
    const auto now = clock_->coordinate_time();
    const double gm_sun = provider_->gravitational_parameter(celestial::bodies::sun);

    for (const auto& body : snapshot_.bodies) {
        const auto* entry = system_->find(body.id);
        if (entry == nullptr || entry->entry.type != celestial::BodyType::Planet) {
            continue;
        }

        // ONE revolution, from the body's own osculating period about the Sun.
        // Not a table of orbital periods: the same call works for anything in the
        // directory, and a constant here would be a fact about Mars written down
        // where nothing checks it.
        double period = 0.0;
        try {
            const auto heliocentric = provider_->state(
                body.id, now, coordinates::ReferenceFrame::centered_on(celestial::bodies::sun));
            period = trajectory::elements_from_state(heliocentric.state, gm_sun).period;
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

        SystemOrbitPath row{};
        row.path.reserve(static_cast<std::size_t>(samples));
        for (int i = 0; i < samples; ++i) {
            const double fraction = static_cast<double>(i) / static_cast<double>(samples - 1);
            const auto t = time::CoordinateTime::from_seconds_since_j2000(from + fraction * (to - from));
            try {
                const auto p = provider_->position(entry->ephemeris_source, t, frame);
                const auto sun_then = provider_->position(celestial::bodies::sun, t, frame);
                row.path.push_back(p - sun_then);
            } catch (const std::exception&) {
                break;
            }
        }
        if (row.path.size() < 3) {
            continue;
        }
        row.period_s = period;
        row.clipped = clipped;
        out[body.name] = std::move(row);
    }
    return out;
}

std::vector<RenderVec3> FlightSession::planned_trajectory() const {
    std::vector<RenderVec3> out;
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
    // expressed in. This is the SAME convention body_orbit_track() uses, and
    // that is the point: three curves drawn on one map have to be in one frame,
    // or the map cannot be read. The heliocentric map asks for the absolute form
    // explicitly (system_map()).
    const auto* origin_body = snapshot_.find(planned_.metrics.origin);
    const math::Vec3 anchor = origin_body != nullptr
                                  ? origin_body->position
                                  : snapshot_.spacecraft.position - snapshot_.spacecraft.relative_position;

    out.reserve(planned_.trajectory.samples.size());
    for (const auto& sample : planned_.trajectory.samples) {
        out.push_back(transform_.to_render(anchor + sample.from_origin));
    }
    return out;
}

bool FlightSession::set_execution_model(const std::string& model) {
    if (model == "finite" || model == "finite_burn") {
        execution_ = navigation::ExecutionModel::FiniteBurn;
        return true;
    }
    if (model == "autopilot") {
        execution_ = navigation::ExecutionModel::Autopilot;
        return true;
    }
    // IMPULSIVE is deliberately not offered. It produces no maneuvers at all --
    // there is no engine in that model -- so a ship cannot be armed with its
    // result, and offering it would be offering a button that plans a mission
    // nobody can fly.
    last_error_ = "unknown execution model \"" + model + "\" (finite | autopilot)";
    report(last_error_, true);
    return false;
}

std::string FlightSession::execution_model() const { return std::string{navigation::to_string(execution_)}; }

OrbitAboutTarget FlightSession::orbit_about_target() const {
    OrbitAboutTarget out{};
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

    coordinates::StateVector relative{};
    relative.position = craft.position - body->position;
    relative.velocity = craft.velocity - body->velocity;
    const auto elements = trajectory::elements_from_state(relative, body->gm);

    out.valid = true;
    out.body = body->name;
    out.radius_m = body->radius;
    out.periapsis_m = elements.periapsis_radius;
    out.apoapsis_m = elements.apoapsis_radius;
    out.eccentricity = elements.eccentricity;
    out.inclination_deg = elements.inclination.degrees();
    out.period_s = elements.period;
    out.distance_m = distance;
    out.speed_ms = relative.velocity.norm();
    out.captured = elements.eccentricity < 1.0;
    return out;
}

bool FlightSession::arm_plan() {
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
    return true;
}

bool FlightSession::has_plan() const { return plan_ != nullptr && !plan_->empty(); }

void FlightSession::clear_plan() {
    if (plan_ != nullptr) {
        *plan_ = navigation::ManeuverPlan{};
    }
    planned_ = navigation::MissionPlanResult{};
    mission_.abort();
}

SnapshotView FlightSession::snapshot() const {
    SnapshotView out{};
    if (builder_ == nullptr) {
        return out;
    }
    out.valid = true;

    const auto& craft = snapshot_.spacecraft;
    out.time_tdb_s = snapshot_.time.seconds_since_j2000();
    out.elapsed_s = snapshot_.elapsed_coordinate.seconds();
    out.proper_time_s = snapshot_.proper_time.seconds();
    out.clock_difference_s = snapshot_.clock_difference.seconds();
    out.time_warp = snapshot_.time_warp;

    out.reference = craft.reference.name();
    out.altitude_m = craft.altitude;
    out.distance_m = craft.distance_to_reference;
    out.speed_ms = craft.relative_velocity.norm();
    out.barycentric_speed_ms = craft.speed;
    out.acceleration_ms2 = craft.acceleration.norm();

    out.mass_kg = craft.mass;
    out.propellant_kg = craft.propellant;
    out.delta_v_budget_ms = craft.delta_v_budget;
    out.thrust_n = main_engine_ != nullptr ? main_engine_->current_thrust() : 0.0;
    out.throttle = throttle();
    out.engine_mode = engine_mode();
    out.mass_flow_kg_s = craft.mass_flow;
    out.endurance_s = craft.endurance;
    out.thrust_along_track = craft.thrust_along_track;
    out.specific_energy_rate = craft.specific_energy_rate;
    out.exhaust_velocity_c = craft_ != nullptr ? craft_->engine().exhaust_velocity_fraction_c() : 0.0;

    out.apoapsis_m = craft.elements.apoapsis_radius;
    out.periapsis_m = craft.elements.periapsis_radius;
    out.semi_major_axis_m = craft.elements.semi_major_axis;
    out.eccentricity = craft.elements.eccentricity;
    out.inclination_deg = craft.elements.inclination.degrees();
    out.period_s = craft.elements.period;

    out.target = craft.target.has_value() ? craft.target->name() : std::string{};
    out.target_distance_m = craft.target_distance;
    out.target_relative_speed_ms = craft.target_relative_speed;

    out.rotation_rate_deg_s = units::rad_to_deg(craft.rotation_rate);
    out.angle_to_prograde_deg = units::rad_to_deg(craft.angle_to_prograde);
    out.angle_to_nadir_deg = units::rad_to_deg(craft.angle_to_nadir);
    out.pointing_mode = pointing_mode();
    out.pointing_error_deg = pointing_error_deg();

    out.beta = craft.beta;
    out.lorentz_factor = craft.lorentz_factor;
    out.lorentz_factor_minus_one = craft.lorentz_factor_minus_one;

    // Diagnostic to surface when something looks like it is jittering: metres per
    // float ulp.  Measured at the REFERENCE BODY, not at the ship: with the
    // floating origin focused on the ship, the ship sits at the origin and its
    // resolution is identically zero -- true, and useless. The planet a few
    // thousand kilometres away is what visibly jitters when the projection is
    // losing digits.
    const auto* reference_body = snapshot_.find(craft.reference);
    out.render_resolution_m =
        transform_.resolution_at(reference_body != nullptr ? reference_body->position : craft.position);
    out.render_resolution_ship_m = transform_.resolution_at(craft.position);
    return out;
}

}  // namespace sf::app
