#include "core/navigation/transfer_planner.hpp"

#include "core/attitude/inertia.hpp"
#include "core/attitude/rcs.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/gravity/composite_force_model.hpp"
#include "core/gravity/oblateness_gravity.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/navigation/maneuver_executor.hpp"
#include "core/navigation/mission.hpp"
#include "core/navigation/trajectory_planner.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sf::navigation {

// ---------------------------------------------------------------------------
// naming
// ---------------------------------------------------------------------------

std::string_view to_string(TransferFailure reason) {
    switch (reason) {
        case TransferFailure::None:                        return "NONE";
        case TransferFailure::NoLambertSolution:           return "NO_LAMBERT_SOLUTION";
        case TransferFailure::BadLambertBranch:            return "BAD_LAMBERT_BRANCH";
        case TransferFailure::NoFeasibleTrajectory:        return "NO_FEASIBLE_TRAJECTORY";
        case TransferFailure::DepartureCorrectorDiverged:  return "DEPARTURE_CORRECTOR_DIVERGED";
        case TransferFailure::DepartureCorrectorStagnated: return "DEPARTURE_CORRECTOR_STAGNATED";
        case TransferFailure::InvalidBPlane:               return "INVALID_BPLANE";
        case TransferFailure::BPlaneCorrectorDiverged:     return "BPLANE_CORRECTOR_DIVERGED";
        case TransferFailure::TargetImpact:                 return "TARGET_IMPACT";
        case TransferFailure::PeriapsisTooHigh:            return "PERIAPSIS_TOO_HIGH";
        case TransferFailure::PeriapsisTooLow:             return "PERIAPSIS_TOO_LOW";
        case TransferFailure::CaptureBurnTooEarly:         return "CAPTURE_BURN_TOO_EARLY";
        case TransferFailure::CaptureBurnTooLate:          return "CAPTURE_BURN_TOO_LATE";
        case TransferFailure::InsufficientCaptureDeltaV:   return "INSUFFICIENT_CAPTURE_DV";
        case TransferFailure::PostBurnHyperbolic:          return "POST_BURN_HYPERBOLIC";
        case TransferFailure::NumericalFailure:            return "NUMERICAL_FAILURE";
        case TransferFailure::Timeout:                     return "TIMEOUT";
        case TransferFailure::DepartureConicHitsCentralBody:
            return "DEPARTURE_CONIC_HITS_CENTRAL_BODY";
        case TransferFailure::InsufficientDepartureDeltaV: return "INSUFFICIENT_DEPARTURE_DV";
        case TransferFailure::Cancelled:                    return "CANCELLED";
        case TransferFailure::NoTransferGeometry:           return "NO_TRANSFER_GEOMETRY";
        case TransferFailure::DepartureGeometryUnavailable:
            return "DEPARTURE_GEOMETRY_UNAVAILABLE";
        case TransferFailure::TargetOrbitNotAchieved:      return "TARGET_ORBIT_NOT_ACHIEVED";
    }
    return "UNCLASSIFIED";
}

std::string_view to_string(TransferGeometry geometry) {
    switch (geometry) {
        case TransferGeometry::Local:          return "LOCAL";
        case TransferGeometry::Interplanetary: return "INTERPLANETARY";
    }
    return "UNKNOWN";
}

std::optional<TransferGeometry> geometry_for(celestial::BodyId origin,
                                             celestial::BodyId destination) {
    if (origin == destination) {
        return std::nullopt;
    }
    const auto primary = celestial::common_primary(origin, destination);
    if (!primary.has_value()) {
        return std::nullopt;
    }
    // The destination orbits the origin (the Moon about the Earth): the whole
    // transfer is inside one gravity well.
    if (*primary == origin) {
        return TransferGeometry::Local;
    }
    // The origin orbits the destination -- a moon-to-planet return. Same
    // geometry, opposite direction: still one well, still a Lambert about the
    // body both ends are bound to. Nothing in the pipeline below cares which of
    // the two is on top.
    if (*primary == destination) {
        return TransferGeometry::Local;
    }
    return TransferGeometry::Interplanetary;
}

std::string_view to_string(ExecutionModel model) {
    switch (model) {
        case ExecutionModel::Impulsive:  return "IMPULSIVE";
        case ExecutionModel::FiniteBurn: return "FINITE_BURN";
        case ExecutionModel::Autopilot:  return "AUTOPILOT";
    }
    return "UNKNOWN";
}

std::string_view to_string(GridClass value) {
    switch (value) {
        case GridClass::NoSolution:             return "NO_SOLUTION";
        case GridClass::DegenerateGeometry:     return "DEGENERATE_GEOMETRY";
        case GridClass::DepartureConicHitsBody: return "DEPARTURE_CONIC_HITS_BODY";
        case GridClass::TooExpensive:           return "TOO_EXPENSIVE";
        case GridClass::Feasible:               return "FEASIBLE";
    }
    return "UNKNOWN";
}

TransferFailure TargetOrbit::check(double periapsis_altitude, double apoapsis_altitude,
                                   double eccentricity) const {
    // Order: the two that say the orbit is in the wrong PLACE first, then the one
    // that says it is the wrong SHAPE.  A 40 x 900 km orbit is more usefully
    // reported as "periapsis too low" than as "too eccentric", though it is both.
    if (!(periapsis_altitude >= min_periapsis_altitude)) {
        return TransferFailure::PeriapsisTooLow;
    }
    if (!(apoapsis_altitude <= max_apoapsis_altitude)) {
        return TransferFailure::PeriapsisTooHigh;
    }
    if (!(eccentricity <= max_eccentricity)) {
        return TransferFailure::TargetOrbitNotAchieved;
    }
    return TransferFailure::None;
}

double TransferCost::evaluate(const TransferCostTerms& terms) const {
    return departure_delta_v * terms.departure_delta_v +
           insertion_delta_v * terms.insertion_delta_v +
           periapsis_error * std::abs(terms.periapsis_error_m) +
           correction_magnitude * std::abs(terms.correction_delta_v) +
           departure_conic_deficit * std::max(0.0, terms.conic_deficit_m) +
           time_of_flight * terms.time_of_flight_days +
           inclination_error * std::abs(terms.inclination_error_rad);
}

namespace {

using math::Vec3;

constexpr double kInverseGoldenRatio = 0.6180339887498949;

std::string fixed(double value, int digits = 6) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(digits) << value;
    return os.str();
}

// Closest approach to the target, refined off the dense output.
//
// Refined and not an argmin over samples: the corrector DIFFERENCES this map,
// and an argmin over a fixed grid is a staircase.  The time of closest approach
// is itself one of the three targeted quantities, and quantised to the sample
// spacing it would be useless as well as non-differentiable.
struct Approach {
    time::CoordinateTime time{};
    Vec3 relative_position{};
    Vec3 relative_velocity{};
    double distance{std::numeric_limits<double>::infinity()};
    bool valid{false};
};

// A departure velocity as the plan wants it, and how the engine delivered it.
struct FlightResult {
    propagation::PropagationState state{};
    time::CoordinateTime time{};
    propagation::PropagationStatus status{propagation::PropagationStatus::Success};
    std::string message;
    double propellant_used{0.0};
    time::CoordinateTime capture_ignition{};
    double capture_duration{0.0};
    time::CoordinateTime circularisation_ignition{};
    double circularisation_duration{0.0};
    double pointing_error_mean{0.0};   // [rad]; AUTOPILOT only
    double pointing_error_peak{0.0};   // [rad]; AUTOPILOT only
    double rcs_propellant{0.0};        // [kg];  AUTOPILOT only, inside the burn
    double rcs_duty_cycle{0.0};        // [0,1]
    double torque_saturation{0.0};     // [0,1] share of the burn against the stop
    double settling_s{0.0};            // [s]   from the guidance switch
    double angular_rate_peak{0.0};     // [rad/s]
    // The plan the engine actually flew.  Empty under Impulsive, where no engine
    // runs and there is nothing to hand a ship.
    ManeuverPlan plan{};
    bool ok{true};
};

// ---------------------------------------------------------------------------
// One epoch, searched and flown.
//
// The session owns the frames, the gravitational parameters and the coasted
// parking orbit, so that the search, the correctors and the capture all read the
// same numbers from the same place.  Everything it returns is DATA: nothing in
// here throws for a physical refusal.
// ---------------------------------------------------------------------------
class TransferSession {
public:
    TransferSession(const TransferInputs& inputs, const TransferConfig& config)
        : inputs_(inputs),
          config_(config),
          ssb_(coordinates::ReferenceFrame::ssb_j2000()),
          centered_(coordinates::ReferenceFrame::centered_on(inputs.center)) {
        if (inputs_.provider == nullptr || inputs_.craft == nullptr) {
            throw std::invalid_argument("lunar transfer: provider and craft are required");
        }
        if (config_.time_of_flight_days.empty()) {
            throw std::invalid_argument(
                "lunar transfer: the time-of-flight grid is empty; a search with nothing to "
                "search is not a search");
        }
        const auto geometry = geometry_for(inputs_.center, inputs_.target);
        if (!geometry.has_value()) {
            throw std::invalid_argument(
                "transfer planner: " + inputs_.center.name() + " and " + inputs_.target.name() +
                " have no common primary in the body directory, so there is no two-body "
                "problem to generate candidates from");
        }
        geometry_ = *geometry;
        // What Lambert is solved about. For a local transfer that is the origin
        // itself; for an interplanetary one it is whatever both bodies orbit,
        // read off the hierarchy rather than assumed to be the Sun -- the same
        // code plans Io to Europa about Jupiter.
        primary_ = geometry_ == TransferGeometry::Local
                       ? inputs_.center
                       : celestial::common_primary(inputs_.center, inputs_.target)
                             .value_or(celestial::bodies::sun);
        primary_frame_ = coordinates::ReferenceFrame::centered_on(primary_);

        gm_center_ = inputs_.provider->gravitational_parameter(inputs_.center);
        gm_target_ = inputs_.provider->gravitational_parameter(inputs_.target);
        gm_primary_ = inputs_.provider->gravitational_parameter(primary_);
        radius_center_ = inputs_.provider->mean_radius(inputs_.center);
        radius_target_ = inputs_.provider->mean_radius(inputs_.target);
        if (!inputs_.j2_bodies.empty() && inputs_.orientation == nullptr) {
            throw std::invalid_argument(
                "lunar transfer: j2_bodies were given with no orientation provider; a "
                "perturbation that is silently dropped is worse than a refusal");
        }
        if (!(gm_center_ > 0.0) || !(gm_target_ > 0.0) || !(radius_target_ > 0.0)) {
            throw std::invalid_argument(
                "lunar transfer: the centre and the target need a GM, and the target a radius, "
                "in the loaded kernels");
        }
        wanted_periapsis_ = radius_target_ + config_.flyby_altitude;
    }

    struct Candidate {
        time::CoordinateTime departure{};
        double coast_s{0.0};
        propagation::PropagationState state{};
        double tof_s{0.0};
        trajectory::TransferDirection direction{trajectory::TransferDirection::Prograde};
        trajectory::LambertSolution solution{};
        Vec3 departure_velocity{};
        double delta_v{0.0};
        double transfer_angle{0.0};
        double departure_perigee{0.0};
        double departure_eccentricity{0.0};
        double departure_conic_deficit{0.0};
        double v_infinity_estimate{0.0};
        // The arrival excess velocity as a VECTOR, in whatever frame Lambert was
        // solved in.  Kept because the aim point needs a DIRECTION and the
        // magnitude alone cannot give one -- and because re-deriving it
        // downstream from `solution` would have to know which frame that was,
        // which is exactly the confusion this milestone is avoiding.
        Vec3 v_infinity_vector{};
        double insertion_estimate{0.0};
        double proxy_cost{0.0};
        double flown_miss{0.0};
        double selection_score{0.0};
        std::string id;
    };

    // ---- what turns an approach into an ORBIT ------------------------------
    //
    // One burn, or two.
    //
    // A lunar capture is one burn: 83 s against a periapsis speed of 1.6 km/s,
    // near enough an impulse that the orbit comes out 96.9 x 103.2 km when
    // 100 x 100 was asked for.
    //
    // A Mars capture at the speeds this engine produces is not.  Arrival
    // v_infinity 18.5 km/s, periapsis speed 19.0 km/s, a single burn of 7.3 km/s
    // lasting 732 s.  Measured, by sweeping where that burn sits relative to
    // periapsis:
    //
    //     offset  -300 s    -957 x 4954 km   e = 0.549
    //     offset     0 s     192 x  993 km   e = 0.101      <- the best there is
    //     offset  +366 s   -1493 x 7131 km   e = 0.695
    //
    // Zero is a local optimum, so a corrector on (delta-v, offset) has a
    // near-singular column there and stalls -- which is exactly what the first
    // version of it did, after 57 flights.  The knob does not exist: ONE burn
    // through a 19 km/s periapsis passage cannot produce a circle, because the
    // engine is lit across four Mars radii of the trajectory.
    //
    // So: two burns, which is what a real Mars orbit insertion is.  The first
    // brakes the hyperbola into an ellipse whose PERIAPSIS is the altitude that
    // was asked for; the second, half a revolution later and an order of
    // magnitude smaller, circularises there.  Being small, the second one really
    // is nearly an impulse, which is why it can close what the first cannot.
    //
    // `has_circularisation` is false for every lunar transfer, and nothing in
    // this struct is reached at all when the single-burn flight already lands in
    // the requested orbit.
    struct CaptureSequence {
        bool active{false};

        double capture_delta_v{0.0};
        time::CoordinateTime capture_at{};
        double mass_at_capture{0.0};

        bool has_circularisation{false};
        double circularisation_delta_v{0.0};
        time::CoordinateTime circularisation_at{};
        double mass_at_circularisation{0.0};

        // PROGRADE, and that is not a detail. The second burn happens at the
        // ellipse's apoapsis and its job is to RAISE the opposite apsis, which
        // costs speed added and not speed removed. Firing it retrograde like the
        // first one would lower the periapsis further and put the ship into the
        // planet.
        GuidanceMode circularisation_guidance{GuidanceMode::Prograde};

        [[nodiscard]] double total_delta_v() const {
            return capture_delta_v + (has_circularisation ? circularisation_delta_v : 0.0);
        }
    };

    struct Rejections {
        int considered{0};
        int no_lambert{0};
        int transfer_angle{0};
        int departure_conic{0};
        int departure_geometry{0};
        int delta_v{0};
    };

    [[nodiscard]] TransferRecord run();
    [[nodiscard]] TransferRecord attempt_with_aim(const Candidate& candidate,
                                                  double aim_altitude,
                                                  const Vec3& departure_guess);
    [[nodiscard]] std::vector<GridCell> map_grid();
    [[nodiscard]] TransferGeometry geometry() const { return geometry_; }

private:
    // ---- infrastructure ---------------------------------------------------

    [[nodiscard]] std::unique_ptr<gravity::CompositeForceModel> build_forces(
        const gravity::ForceModel* thrust) const {
        auto model = std::make_unique<gravity::CompositeForceModel>();
        model->add(std::make_unique<gravity::PointMassGravity>(*inputs_.provider, inputs_.catalog,
                                                               ssb_));
        for (const auto body : inputs_.j2_bodies) {
            model->add(gravity::OblatenessGravity::for_body(*inputs_.provider,
                                                            *inputs_.orientation, body, ssb_));
        }
        if (thrust != nullptr) {
            model->add_reference(*thrust);
        }
        return model;
    }

    [[nodiscard]] propagation::IntegratorConfig integrator_for(bool stop_on_impact) const {
        auto integrator = inputs_.integrator;
        integrator.stop_inside_body = stop_on_impact;
        // Counted in STEPS and not in wall seconds, so that the same case
        // classifies the same way on a fast machine and a slow one.
        integrator.max_steps = config_.step_budget;
        integrator.minimum_mass = inputs_.craft->dry_mass();
        return integrator;
    }

    [[nodiscard]] propagation::PropagationState initial_state() const {
        const auto center = inputs_.provider->state(inputs_.center, inputs_.epoch, ssb_);
        propagation::PropagationState state{};
        state.state.position = center.state.position + inputs_.parking.position;
        state.state.velocity = center.state.velocity + inputs_.parking.velocity;
        state.mass = inputs_.craft->initial_mass();
        return state;
    }

    // Autopilot: the same finite burns, pointed by the attitude controller.
    //
    // The difference from fly_finite() is one substitution -- GuidanceMode::Hull
    // instead of Retrograde -- and everything that follows from it.  With Hull
    // the engine points along the integrated quaternion, so the thrust direction
    // is whatever the RCS and a PD controller have managed to achieve, and the
    // controller's steady-state tracking lag is IN the trajectory rather than
    // assumed away.
    //
    // The run is split at the injection cutoff because the two burns want the
    // nose in different places, and a controller has one command at a time: an
    // inertial direction while the injection runs, and retrograde about the
    // target from then on.  The days of coast in between are more than enough for
    // the slew, which is the point -- what this measures is the LAG during the
    // capture burn, not a race to get pointed.
    [[nodiscard]] FlightResult fly_autopilot(const Candidate& candidate,
                                             const Vec3& departure_velocity,
                                             time::CoordinateTime until,
                                             propagation::Trajectory* arc, bool stop_on_impact,
                                             const CaptureSequence& capture) {
        FlightResult out{};
        out.state = candidate.state;
        out.time = candidate.departure;

        const Vec3 impulse = departure_velocity - candidate.state.state.velocity;
        if (!(impulse.norm() > 0.0)) {
            out.ok = false;
            out.status = propagation::PropagationStatus::InvariantViolation;
            out.message = "autopilot: the injection has no direction";
            return out;
        }
        const Vec3 injection_direction = impulse.normalized();

        ManeuverPlan plan;
        time::CoordinateTime injection_cutoff{};
        try {
            auto injection = maneuver_for_delta_v(*inputs_.craft, candidate.state.mass,
                                                  impulse.norm(), candidate.departure,
                                                  GuidanceMode::Hull, inputs_.center, 1.0,
                                                  "injection", BurnCentering::CenterOnIgnition);
            injection.ignition = candidate.departure;
            injection.inertial_direction = injection_direction;
            injection_cutoff = injection.cutoff();
            plan.add(std::move(injection));
        } catch (const std::exception& e) {
            out.ok = false;
            out.status = propagation::PropagationStatus::OutOfPropellant;
            out.message = e.what();
            return out;
        }
        if (!add_capture_burns(plan, capture, GuidanceMode::Hull)) {
            out.ok = false;
            out.status = propagation::PropagationStatus::OutOfPropellant;
            out.message = "the ship cannot pay for the capture sequence";
            return out;
        }

        const auto inertia = attitude::InertiaTensor::solid_box(config_.autopilot_hull_mass,
                                                                config_.autopilot_hull_size);
        const propulsion::EngineSpec thruster{"RCS", config_.autopilot_rcs_mass_flow,
                                              config_.autopilot_rcs_exhaust_fraction_c, 1.0};
        const auto rcs = attitude::RcsSystem::couples(config_.autopilot_rcs_arm, thruster);
        attitude::PointingController controller{*inputs_.provider, inertia, ssb_,
                                                config_.autopilot_gains};
        // Section 11: the controller does not get to assume infinite torque.  The
        // limit is read off the layout -- twelve thrusters, the arm, the thrust --
        // rather than declared, so raising omega_n cannot quietly buy authority
        // the ship does not have.
        controller.set_actuator_limits(attitude::limits_of(rcs));
        const attitude::RcsForce rcs_force{rcs, controller};

        ManeuverExecutor executor{*inputs_.provider, *inputs_.craft, plan, ssb_};
        auto forces = build_forces(&executor);
        forces->add_reference(rcs_force);
        propagation::DormandPrince54Propagator propagator{*forces,
                                                          integrator_for(stop_on_impact)};
        propagator.set_inertia(&inertia);

        // The ship starts already pointed for the injection.  That is not a
        // convenience: the slew belongs to the parking orbit, before the departure
        // window this search is allowed to move inside, and inventing a slew
        // schedule here would put a manoeuvre in the record that the planner never
        // planned.  What is being measured is the lag while TRACKING a rotating
        // target, and that is entirely in the second burn.
        auto state = candidate.state;
        state.attitude.orientation =
            math::Quaternion::from_two_vectors(Vec3::unit_x(), injection_direction);
        state.attitude.angular_velocity = Vec3{};

        attitude::PointingCommand command{};
        command.mode = GuidanceMode::Inertial;
        command.inertial_direction = injection_direction;
        controller.set_command(command);

        const auto leg_one_end = std::min(injection_cutoff, until);
        auto mission = run_mission(propagator, executor, state, candidate.departure, leg_one_end,
                                   arc);
        account(mission.stats);
        out.status = mission.status;
        out.message = mission.message;
        out.ok = mission.ok();
        out.state = mission.state;
        out.time = mission.time;

        if (out.ok && leg_one_end < until) {
            command.mode = GuidanceMode::Retrograde;
            command.reference = inputs_.target;
            controller.set_command(command);

            // What the nose is doing WHILE the capture burn runs, sampled at every
            // accepted step inside it.
            //
            // The first version of this read the attitude from the state at the
            // END of the propagation -- two orbits after the burn -- and reported
            // the angle between an attitude nobody was steering any more and a
            // retrograde direction from a different part of the orbit.  It gave
            // 1.5 degrees at every controller bandwidth, which should have been
            // the giveaway: the lag of a PD controller goes as 1/omega_n, and a
            // number that does not move when omega_n quadruples is not measuring
            // the lag.
            const auto* insertion = plan.maneuvers().size() > 1 ? &plan.maneuvers()[1] : nullptr;
            double error_sum = 0.0;
            double error_weight = 0.0;
            double error_peak = 0.0;
            // Section 10's other columns.  A gain is not allowed to be chosen on
            // eccentricity alone, so the cost of achieving it is measured in the
            // same pass: what the thrusters burnt, how open they were, how much of
            // the time the demand was against the stop, and how fast the hull was
            // actually turning.
            double rcs_propellant = 0.0;
            double duty_sum = 0.0;
            double duty_weight = 0.0;
            double saturated_time = 0.0;
            double rate_peak = 0.0;
            const double settling_threshold = config_.settling_threshold.radians();
            double last_unsettled = mission.time.seconds_since_j2000();
            const double leg_two_start = last_unsettled;
            const auto thruster_count = static_cast<double>(std::max<std::size_t>(rcs.size(), 1));

            // ONLY on a flight that has a capture burn in it, and that is a
            // measurement rather than tidiness.
            //
            // Every probe flight the two correctors make goes through this same
            // function -- hundreds of them per epoch -- and the observer below
            // asks the ephemeris for the target's state twice per accepted step,
            // once for the pointing error and once for the torque demand.  Left
            // unguarded it multiplied the cost of an AUTOPILOT epoch by about an
            // order of magnitude while measuring probe trajectories nobody flies.
            //
            // Nothing is lost: every quantity here is about the capture burn or
            // the slew that precedes it, and neither exists on a probe.
            if (insertion != nullptr) {
                propagator.set_step_observer([&](const propagation::StepInfo& step) {
                    if (!step.accepted) {
                        return;
                    }
                    rate_peak = std::max(rate_peak, step.state.attitude.angular_velocity.norm());

                    // The slew, from the instant the guidance command changed.  This
                    // is a different question from the lag inside the burn and it is
                    // measured over a different interval on purpose: acquisition
                    // happens in the days of coast, tracking happens in the burn.
                    const double error_now = controller.pointing_error(step.state, step.time);
                    if (error_now > settling_threshold) {
                        last_unsettled = step.time.seconds_since_j2000();
                    }

                    // What the thrusters were doing, whether or not a burn was on:
                    // the acquisition slew costs propellant too.
                    const Vec3 demand = controller.unsaturated_torque(step.state, step.time);
                    if (demand.norm_squared() > 0.0) {
                        const double factor = controller.saturation_factor(demand);
                        if (factor < 1.0) {
                            saturated_time += step.step_seconds;
                        }
                        const auto throttles = rcs.allocate(demand * factor);
                        const auto output = rcs.evaluate(throttles);
                        rcs_propellant += output.mass_flow * step.step_seconds;
                        double open = 0.0;
                        for (const double throttle : throttles) {
                            open += throttle;
                        }
                        duty_sum += (open / thruster_count) * step.step_seconds;
                    }
                    duty_weight += step.step_seconds;

                    if (insertion == nullptr || !insertion->active_at(step.time)) {
                        return;
                    }
                    const auto body = inputs_.provider->state(inputs_.target, step.time, ssb_);
                    const Vec3 relative = step.state.state.velocity - body.state.velocity;
                    if (!(relative.norm() > 0.0)) {
                        return;
                    }
                    const double error =
                        math::angle_between(step.state.attitude.forward(), -relative);
                    // Weighted by the step, so a controller that spends most of
                    // the burn settled is not judged by the instant it was not.
                    error_sum += error * step.step_seconds;
                    error_weight += step.step_seconds;
                    error_peak = std::max(error_peak, error);
                });
            }

            propagation::Trajectory tail;
            const auto second = run_mission(propagator, executor, mission.state, mission.time,
                                            until, arc != nullptr ? &tail : nullptr);
            account(second.stats);
            if (arc != nullptr) {
                for (const auto& segment : tail.segments()) {
                    arc->append(segment);
                }
            }
            out.status = second.status;
            out.message = second.message;
            out.ok = second.ok();
            out.state = second.state;
            out.time = second.time;
            for (const auto& burn : second.burns) {
                if (burn.name == "insertion") {
                    out.capture_ignition = burn.ignition;
                    out.capture_duration = burn.duration.seconds();
                }
            }
            propagator.set_step_observer(nullptr);
            if (insertion != nullptr) {
                out.pointing_error_mean = error_weight > 0.0 ? error_sum / error_weight : 0.0;
                out.pointing_error_peak = error_peak;
                out.rcs_propellant = rcs_propellant;
                out.rcs_duty_cycle = duty_weight > 0.0 ? duty_sum / duty_weight : 0.0;
                out.torque_saturation = duty_weight > 0.0 ? saturated_time / duty_weight : 0.0;
                out.settling_s = std::max(0.0, last_unsettled - leg_two_start);
                out.angular_rate_peak = rate_peak;
            }
        }
        out.propellant_used = candidate.state.mass - out.state.mass;
        out.plan = plan;
        return out;
    }

    // How far from the target counts as "arrived" for the FIRST corrector stage.
    //
    // Stage 1 does not do precision -- its only job is to put the ship somewhere
    // a B-plane can be read from, and stage 2 then aims it (see the note on
    // TransferConfig::departure_targeting).  So the tolerance is a statement
    // about the size of the target's gravitational neighbourhood, and that is
    // 384 000 km from the Earth for one destination and 228 million for another.
    //
    // For a LOCAL transfer it is left exactly at the configured value, which is
    // the number the 365/365 campaign was qualified with.  For an interplanetary
    // one it is derived: half the radius at which the target's pull matches the
    // primary's, r = R (m/M)^(2/5).  That is a NUMERICAL choice about where a
    // corrector stage stops, not a physical boundary -- nothing in the dynamics
    // below knows this radius exists, and gravity stays multibody throughout
    // (rule 65).
    [[nodiscard]] TargetingConfig departure_targeting_for(const Candidate& candidate) const {
        auto targeting = config_.departure_targeting;
        if (geometry_ == TransferGeometry::Local) {
            return targeting;
        }
        const auto arrival = candidate.departure + time::Duration::seconds(candidate.tof_s);
        const double separation =
            inputs_.provider->state(inputs_.target, arrival, primary_frame_).state.position.norm();
        if (separation > 0.0 && gm_primary_ > 0.0) {
            const double influence = separation * std::pow(gm_target_ / gm_primary_, 0.4);
            targeting.position_tolerance = 0.5 * influence;
        }
        return targeting;
    }

    // How far the arrival moves per m/s of departure velocity.  Used to convert a
    // flown miss into the correction effort it predicts, so that candidates are
    // ranked on one cost function instead of two.
    //
    // To first order it is just the time of flight: a metre per second held for
    // t seconds moves the arrival by t metres.  The measured lunar figure is
    // 1e6 m per m/s against a 4.5-day flight, i.e. 2.57 times that -- the
    // amplification the target's own gravity adds on the way in.  The
    // interplanetary value carries the same factor rather than inventing a new
    // one, and the local value is left at the configured constant so that the
    // qualified campaign's ranking does not move.
    [[nodiscard]] double lever_arm_for(const Candidate& candidate) const {
        if (geometry_ == TransferGeometry::Local) {
            return std::max(config_.arrival_lever_arm, 1.0);
        }
        return std::max(2.57 * candidate.tof_s, 1.0);
    }

    // A body's velocity relative to whatever Lambert was solved about.  One line,
    // named, because getting this frame wrong is silent: the excess velocity
    // would come out about 30 km/s too large and every candidate would be
    // refused as unaffordable with no indication of why.
    [[nodiscard]] Vec3 primary_relative_velocity(celestial::BodyId body,
                                                 time::CoordinateTime t) const {
        return inputs_.provider->state(body, t, primary_frame_).state.velocity;
    }

    // ---- the capture, solved against the FLOWN orbit ----------------------
    //
    // See the note on CaptureSequence for why there are two burns and what was
    // measured to find that out.
    //
    // Each burn is solved for ONE number against ONE number -- the first one's
    // delta-v against the periapsis it produces, the second's against the
    // apoapsis -- by the secant method.  One knob per target, so the map is
    // monotone and there is no Jacobian to go singular: the two-knob version
    // this replaced stalled after 57 flights because its second column was zero
    // at the point it started from.
    struct BurnSolution {
        double delta_v{0.0};
        double achieved{0.0};
        trajectory::OrbitalElements elements{};
        bool converged{false};
        int evaluations{0};
        std::string message;
    };

    // One capture sequence, flown from a state that is already on the approach.
    //
    // Deliberately NOT a flight from departure: the solvers need a dozen
    // evaluations each, and every one would otherwise re-integrate the whole
    // interplanetary coast -- the same arc every time. Starting from the approach
    // makes an evaluation cost hours of flight instead of months, and changes no
    // physics: the same force model, the same integrator, the same maneuvers.
    [[nodiscard]] std::optional<trajectory::OrbitalElements> fly_capture_from(
        const propagation::PropagationState& start, time::CoordinateTime t_start,
        const CaptureSequence& capture, time::CoordinateTime until) {
        ManeuverPlan plan;
        if (!add_capture_burns(plan, capture,
                               config_.execution == ExecutionModel::Autopilot
                                   ? GuidanceMode::Hull
                                   : GuidanceMode::Retrograde)) {
            return std::nullopt;   // the ship cannot pay for this much braking
        }

        ManeuverExecutor executor{*inputs_.provider, *inputs_.craft, plan, ssb_};
        auto forces = build_forces(&executor);

        // A LOCAL budget, and it is the difference between a solver that answers
        // in seconds and one that does not answer.
        //
        // These flights are hours long -- a burn and a couple of revolutions --
        // and a healthy one costs a few thousand steps. Inheriting the search's
        // whole budget meant that a trial burn which sent the ship grazing the
        // planet could grind for fifty million steps before anyone was told,
        // which is how a two-second solve became a ten-minute one. Measured: the
        // departure stages of an Earth-Mars attempt cost 3.2 s together, and the
        // capture solve after them did not finish.
        //
        // Exhausting it is not an error, it is an ANSWER: the trial did not close
        // an orbit, and solve_burn responds by taking a shorter step. 200 000 is
        // about a hundred times a healthy flight.
        auto integrator = integrator_for(true);
        integrator.max_steps = 200'000;
        propagation::DormandPrince54Propagator propagator{*forces, integrator};
        const auto mission = run_mission(propagator, executor, start, t_start, until, nullptr);
        account(mission.stats);
        if (!mission.ok()) {
            return std::nullopt;
        }
        return elements_about_target(mission.state, mission.time);
    }

    [[nodiscard]] trajectory::OrbitalElements elements_about_target(
        const propagation::PropagationState& state, time::CoordinateTime t) const {
        const auto body = inputs_.provider->state(inputs_.target, t, ssb_);
        coordinates::StateVector relative{};
        relative.position = state.state.position - body.state.position;
        relative.velocity = state.state.velocity - body.state.velocity;
        return trajectory::elements_from_state(relative, gm_target_);
    }

    // Solve one burn's delta-v so that `readout` of the resulting orbit lands on
    // `wanted`.  `mutate` writes the trial delta-v into the sequence, so the same
    // routine solves the first burn and the second without knowing which is which.
    template <typename Mutate, typename Readout>
    [[nodiscard]] BurnSolution solve_burn(const propagation::PropagationState& start,
                                          time::CoordinateTime t_start, CaptureSequence sequence,
                                          Mutate&& mutate, Readout&& readout, double guess,
                                          double wanted, double tolerance,
                                          double settle_seconds) {
        BurnSolution out{};
        out.delta_v = guess;

        auto evaluate = [&](double delta_v) -> std::optional<trajectory::OrbitalElements> {
            if (!(delta_v > 0.0)) {
                return std::nullopt;
            }
            ++out.evaluations;
            CaptureSequence trial = sequence;
            mutate(trial, delta_v);
            const auto until = latest_burn_epoch(trial) + time::Duration::seconds(settle_seconds);
            return fly_capture_from(start, t_start, trial, until);
        };

        auto first = evaluate(guess);
        if (!first.has_value()) {
            out.message = "the first trial burn did not close an orbit";
            return out;
        }
        double x0 = guess;
        double f0 = readout(*first) - wanted;

        // The second point of the secant: one part in a thousand of the burn.
        // Large enough to move the orbit by far more than the integrator's error,
        // small enough that the two points still see the same curve.
        double x1 = guess * 1.001 + 1.0;
        auto second = evaluate(x1);
        if (!second.has_value()) {
            x1 = guess * 0.999;
            second = evaluate(x1);
        }
        if (!second.has_value()) {
            out.message = "the secant's second point did not close an orbit";
            return out;
        }
        double f1 = readout(*second) - wanted;
        auto best = *second;

        for (int iteration = 0; iteration < 20; ++iteration) {
            if (std::abs(f1) < tolerance) {
                out.converged = true;
                break;
            }
            const double slope = (f1 - f0) / (x1 - x0);
            if (!std::isfinite(slope) || slope == 0.0) {
                out.message = "the burn solver's secant went flat";
                break;
            }
            double next = x1 - f1 / slope;
            // The delta-v cannot go negative and cannot run away: a step of more
            // than half the current burn means the secant is extrapolating far
            // outside the two points it was built from.
            const double limit = std::max(0.5 * std::abs(x1), 1.0);
            next = std::clamp(next, x1 - limit, x1 + limit);
            if (!(next > 0.0)) {
                next = 0.5 * x1;
            }

            // A trial that does not come back is a trial that flew into the
            // planet or ran the tank dry, and the honest response is to take a
            // shorter step rather than to give up: the secant extrapolates, and
            // the first point outside the feasible set says where the edge is,
            // not that there is no answer inside it.
            std::optional<trajectory::OrbitalElements> trial;
            double accepted = next;
            for (int retreat = 0; retreat < 6; ++retreat) {
                trial = evaluate(accepted);
                if (trial.has_value()) {
                    break;
                }
                accepted = 0.5 * (accepted + x1);
                if (std::abs(accepted - x1) < 1.0e-6 * std::max(1.0, std::abs(x1))) {
                    break;
                }
            }
            if (!trial.has_value()) {
                out.message = "the burn solver lost the orbit";
                break;
            }
            x0 = x1;
            f0 = f1;
            x1 = accepted;
            f1 = readout(*trial) - wanted;
            best = *trial;
        }

        out.delta_v = x1;
        out.achieved = f1 + wanted;
        out.elements = best;
        if (out.converged && out.message.empty()) {
            out.message = "converged";
        }
        return out;
    }

    // How long until the next periapsis passage, from a set of elements read at
    // some instant on the orbit.
    //
    // Through the eccentric anomaly rather than by propagating: the answer is
    // wanted as an IGNITION EPOCH for a burn the solver will then size, and a
    // Kepler step is exact for that purpose while a propagation would cost
    // another flight. The orbit it is read from is the FLOWN one, so the two-body
    // step is taken over an arc of half a revolution rather than over a transfer.
    [[nodiscard]] static double seconds_to_periapsis(const trajectory::OrbitalElements& e) {
        if (!(e.period > 0.0) || !std::isfinite(e.period) || e.eccentricity >= 1.0) {
            return 0.0;
        }
        const double nu = e.true_anomaly.radians();
        const double eccentric = 2.0 * std::atan2(std::sqrt(1.0 - e.eccentricity) *
                                                      std::sin(0.5 * nu),
                                                  std::sqrt(1.0 + e.eccentricity) *
                                                      std::cos(0.5 * nu));
        const double mean = eccentric - e.eccentricity * std::sin(eccentric);
        // Mean anomaly is measured FROM periapsis, so the time still to run is
        // whatever is left of a revolution.
        const double since = std::fmod(mean / units::two_pi * e.period + e.period, e.period);
        return e.period - since;
    }

    [[nodiscard]] static time::CoordinateTime latest_burn_epoch(const CaptureSequence& sequence) {
        return sequence.has_circularisation ? sequence.circularisation_at : sequence.capture_at;
    }

    [[nodiscard]] double propellant_for(double mass, double delta_v) const {
        if (!(delta_v > 0.0)) {
            return 0.0;
        }
        return inputs_.craft->engine().propellant_for_delta_v(mass, delta_v);
    }

    bool coast_parking_orbit() {
        auto forces = build_forces(nullptr);
        propagation::DormandPrince54Propagator propagator{*forces, integrator_for(true)};
        propagator.set_trajectory_recorder(&coast_);
        const auto result = propagator.propagate(initial_state(), inputs_.epoch,
                                                 inputs_.epoch + config_.departure_window);
        propagator.set_trajectory_recorder(nullptr);
        account(result.stats);
        coast_message_ = result.message;
        return result.ok();
    }

    void report_progress(const char* stage, const Rejections& rejections, int screened,
                         int flown, int succeeded, double best_delta_v) const {
        if (!config_.on_progress) {
            return;
        }
        SearchProgress progress{};
        progress.stage = stage;
        progress.candidates_considered = rejections.considered;
        progress.candidates_screened = screened;
        progress.candidates_flown = flown;
        progress.candidates_succeeded = succeeded;
        progress.best_total_delta_v = best_delta_v;
        progress.integrator_steps = steps_;
        config_.on_progress(progress);
    }

    void account(const propagation::IntegratorStats& stats) {
        steps_ += stats.accepted_steps + stats.rejected_steps;
        ++propagations_;
    }

    // ---- the analytic screen ---------------------------------------------
    //
    // Two-body arithmetic, no propagation.  This is where the constraint
    // Milestone 6 did not have lives: a transfer whose post-injection conic dives
    // below the central body's surface is not a transfer, and no corrector can
    // make it one.
    //
    // `r1` and `r2` are relative to `primary_`, and `primary` is its state: for a
    // LOCAL transfer the primary IS the origin, so `primary` and `center` are the
    // same BodyState and every line below reduces term by term to what the
    // 365/365 campaign ran.  That equivalence is not a hope -- it is what
    // tests/scientific/test_planner_equivalence.cpp measures.
    [[nodiscard]] std::optional<Candidate> screen(time::CoordinateTime t_depart,
                                                  const propagation::PropagationState& state,
                                                  const ephemeris::BodyState& center,
                                                  const ephemeris::BodyState& primary,
                                                  const Vec3& r1, const Vec3& r2,
                                                  time::Duration tof,
                                                  trajectory::TransferDirection direction,
                                                  Rejections& rejections,
                                                  GridCell* cell = nullptr) const {
        const double geometric_angle = math::angle_between(r1, r2);
        const double transfer_angle = direction == trajectory::TransferDirection::Prograde
                                          ? geometric_angle
                                          : units::two_pi - geometric_angle;
        if (cell != nullptr) {
            cell->transfer_angle_deg = units::rad_to_deg(transfer_angle);
        }

        // Lambert is degenerate at a transfer angle of exactly pi: every plane
        // containing both points is a solution, so the transfer plane is not
        // defined (docs/physics/lambert.md section 3).  Near it the plane is
        // defined and badly conditioned.  The refusal band is narrow on purpose --
        // it refuses what is undefined, not what is awkward.
        const double half_width = config_.degenerate_half_width.radians();
        if (geometric_angle < config_.minimum_transfer_angle.radians() ||
            std::abs(geometric_angle - units::pi) < half_width) {
            ++rejections.transfer_angle;
            if (cell != nullptr) {
                cell->classification = GridClass::DegenerateGeometry;
            }
            return std::nullopt;
        }

        trajectory::LambertSolution solution{};
        try {
            solution = trajectory::solve_lambert(r1, r2, tof, gm_primary_, direction);
        } catch (const std::exception&) {
            ++rejections.no_lambert;
            if (cell != nullptr) {
                cell->classification = GridClass::NoSolution;
            }
            return std::nullopt;
        }
        // LambertSolution reports the time of flight it ACHIEVED rather than a
        // boolean: the iteration can return something that is not the transfer
        // that was asked for, and comparing the two is the honest check.
        if (!std::isfinite(solution.achieved_time_of_flight) ||
            std::abs(solution.achieved_time_of_flight - tof.seconds()) > 1.0e-6 * tof.seconds()) {
            ++rejections.no_lambert;
            if (cell != nullptr) {
                cell->classification = GridClass::NoSolution;
            }
            return std::nullopt;
        }

        Candidate candidate{};
        candidate.departure = t_depart;
        candidate.coast_s = (t_depart - inputs_.epoch).seconds();
        candidate.state = state;
        candidate.tof_s = tof.seconds();
        candidate.direction = direction;
        candidate.solution = solution;
        candidate.transfer_angle = transfer_angle;
        // The position and velocity of the departure conic ABOUT THE ORIGIN: the
        // conic the ship actually flies out on, whichever geometry this is.
        const Vec3 r_origin = state.state.position - center.state.position;
        Vec3 departure_velocity_relative{};

        if (geometry_ == TransferGeometry::Local) {
            // Lambert's departure velocity is already relative to the origin,
            // because the origin is what Lambert was solved about.
            departure_velocity_relative = solution.departure_velocity;
        } else {
            // Rules 33 and 34.  Lambert answered a HELIOCENTRIC question, so its
            // departure velocity is a heliocentric velocity and not a burn: the
            // difference between it and the origin body's own heliocentric
            // velocity is the excess velocity the ship has to leave with.
            //
            // Treating that excess velocity as the burn -- which is what "use
            // v_depart from Lambert directly" amounts to -- forgets that the ship
            // is 6378 km down a gravity well and has to climb out of it. It is
            // wrong by about 8 km/s at a 400 km parking orbit, which no
            // differential corrector recovers from.
            const Vec3 v_infinity_out = solution.departure_velocity - primary_relative_velocity(
                                            inputs_.center, t_depart);
            const auto hyperbola =
                trajectory::departure_onto_asymptote(r_origin, v_infinity_out, gm_center_);
            if (!hyperbola.ok) {
                ++rejections.departure_geometry;
                if (cell != nullptr) {
                    cell->classification = GridClass::NoSolution;
                }
                return std::nullopt;
            }
            departure_velocity_relative = hyperbola.velocity;
        }

        candidate.departure_velocity = center.state.velocity + departure_velocity_relative;
        candidate.delta_v = (candidate.departure_velocity - state.state.velocity).norm();

        // THE constraint.  The post-injection conic about the central body, as a
        // two-body orbit: if its periapsis is below the surface, the spacecraft
        // flies into the planet it just left, minutes after ignition.  46 of the
        // 59 Milestone 6 failures are exactly this, and every one of them was
        // preceded by a B-plane stage reporting `converged`.
        //
        // The same test for both geometries, and it has to be: an interplanetary
        // departure hyperbola can dive through the Earth exactly as readily as a
        // trans-lunar one, and for the same reason -- the asymptote the transfer
        // wants may sit on the far side of the planet from where the ship is.
        const coordinates::StateVector departure_conic{r_origin, departure_velocity_relative};
        const auto elements = trajectory::elements_from_state(departure_conic, gm_center_);
        candidate.departure_perigee = elements.periapsis_radius;
        candidate.departure_eccentricity = elements.eccentricity;
        if (cell != nullptr) {
            cell->lambert_delta_v = candidate.delta_v;
            cell->departure_perigee_altitude = elements.periapsis_radius - radius_center_;
        }

        // How far the uncorrected conic falls short of the floor.  A cost, not a
        // refusal, unless the caller asks for the stricter reading: the corrector
        // moves the departure velocity by hundreds of m/s and routinely lifts a
        // perigee that started below the surface, so refusing here refuses
        // flyable transfers.  The collision itself is caught where it is a fact
        // -- on the flown arc.
        candidate.departure_conic_deficit = std::max(
            0.0, radius_center_ + config_.minimum_departure_perigee_altitude -
                     elements.periapsis_radius);
        if (candidate.departure_conic_deficit > 0.0) {
            ++rejections.departure_conic;
            if (cell != nullptr) {
                cell->classification = GridClass::DepartureConicHitsBody;
            }
            // A penalty for a LOCAL transfer and a refusal for an interplanetary
            // one, and the difference is in what the number means rather than in
            // how strict anyone feels.
            //
            // Locally, the perigee is read off a Lambert conic that the corrector
            // then moves by 152 to 3910 m/s (median 445 over 365 epochs), and a
            // correction that size routinely lifts a perigee that started below
            // the surface. Refusing there refuses flyable transfers -- measured:
            // 84 epochs of 100 lost to it.
            //
            // Interplanetary, the perigee is read off the EXACT hyperbola through
            // the ship's own position with the asymptote the transfer needs. It is
            // a geometric fact about that departure point, not an estimate, and
            // the corrector's authority here is tens of m/s against a periapsis
            // thousands of kilometres inside the planet. Nothing lifts it.
            //
            // Flying them anyway is not merely wasteful, it is the dominant cost
            // of the whole search: with `stop_inside_body` off -- which the probe
            // flights need, because a corrector wants a smooth map -- a
            // trajectory that passes through the Earth meets an acceleration of
            // 10^11 m/s^2 and the error controller grinds the step to the floor.
            // Measured on the first Earth-Mars search: 226 000 integrator steps
            // per flight against 5 003 for the same transfer that clears the
            // surface, and the 2 000 000-step budget exhausted after nine
            // propagations.
            const bool refuse = config_.refuse_departure_conic_below_floor ||
                                geometry_ == TransferGeometry::Interplanetary;
            if (refuse) {
                return std::nullopt;
            }
        }

        // What the arrival costs, as far as two-body arithmetic can see it from
        // here: v_infinity relative to the target, and the periapsis burn it
        // implies.
        const auto target_arrival = inputs_.provider->state(inputs_.target, t_depart + tof,
                                                            primary_frame_);
        candidate.v_infinity_vector = solution.arrival_velocity - target_arrival.state.velocity;
        candidate.v_infinity_estimate = candidate.v_infinity_vector.norm();
        if (candidate.v_infinity_estimate > 0.0) {
            candidate.insertion_estimate =
                plan_insertion(wanted_periapsis_, gm_target_, candidate.v_infinity_estimate, 0.0)
                    .delta_v;
        }
        if (cell != nullptr) {
            cell->v_infinity_estimate = candidate.v_infinity_estimate;
        }

        const double budget = inputs_.craft->delta_v_budget(state.mass);
        if (candidate.delta_v + candidate.insertion_estimate > budget) {
            ++rejections.delta_v;
            if (cell != nullptr) {
                cell->classification = GridClass::TooExpensive;
            }
            return std::nullopt;
        }

        // The proxy the candidates are SORTED by before any of them is flown.
        // Not the cost function of section 8: that one needs numbers only a
        // flight produces.  This one decides which few are worth a propagation.
        candidate.proxy_cost = config_.cost.evaluate(TransferCostTerms{
            candidate.delta_v, candidate.insertion_estimate, 0.0, 0.0,
            candidate.departure_conic_deficit, tof.days(), 0.0});
        candidate.id = std::string{direction == trajectory::TransferDirection::Prograde
                                       ? "prograde"
                                       : "retrograde"} +
                       "/" + fixed(tof.days(), 2) + "d/coast=" +
                       fixed(candidate.coast_s / 3600.0, 3) + "h";
        if (cell != nullptr && candidate.departure_conic_deficit <= 0.0) {
            cell->classification = GridClass::Feasible;
        }
        return candidate;
    }

    [[nodiscard]] std::vector<Candidate> build_candidates(Rejections& rejections) const {
        std::vector<Candidate> candidates;

        // Pinned: one departure, one time of flight, one branch.  The screen
        // still runs -- a pinned geometry that violates a hard constraint is
        // still refused, and saying so is the whole reason the pin exists.
        if (config_.pinned.active) {
            const auto t_depart =
                inputs_.epoch + time::Duration::seconds(config_.pinned.coast_s);
            if (!coast_.contains(t_depart)) {
                return candidates;
            }
            const auto state = coast_.state_at(t_depart);
            const auto center = inputs_.provider->state(inputs_.center, t_depart, ssb_);
            const auto primary = inputs_.provider->state(primary_, t_depart, ssb_);
            const Vec3 r1 = state.state.position - primary.state.position;
            const auto tof = time::Duration::days(config_.pinned.time_of_flight_days);
            const Vec3 r2 =
                inputs_.provider->state(inputs_.target, t_depart + tof, primary_frame_)
                    .state.position;
            ++rejections.considered;
            auto candidate = screen(t_depart, state, center, primary, r1, r2, tof,
                                    config_.pinned.direction, rejections);
            if (candidate.has_value()) {
                candidates.push_back(std::move(*candidate));
            }
            return candidates;
        }

        const auto samples = coast_.sample(static_cast<std::size_t>(std::max(2, config_.departure_samples)));

        std::vector<trajectory::TransferDirection> directions;
        if (config_.try_prograde) {
            directions.push_back(trajectory::TransferDirection::Prograde);
        }
        if (config_.try_retrograde) {
            directions.push_back(trajectory::TransferDirection::Retrograde);
        }

        for (const auto& [t_depart, state] : samples) {
            const auto center = inputs_.provider->state(inputs_.center, t_depart, ssb_);
            const auto primary = inputs_.provider->state(primary_, t_depart, ssb_);
            const Vec3 r1 = state.state.position - primary.state.position;
            for (const double tof_days : config_.time_of_flight_days) {
                const auto tof = time::Duration::days(tof_days);
                const Vec3 r2 = inputs_.provider
                                    ->state(inputs_.target, t_depart + tof, primary_frame_)
                                    .state.position;
                for (const auto direction : directions) {
                    ++rejections.considered;
                    auto candidate = screen(t_depart, state, center, primary, r1, r2, tof,
                                            direction, rejections);
                    if (candidate.has_value()) {
                        candidates.push_back(std::move(*candidate));
                    }
                }
            }
        }
        return candidates;
    }

    // ---- flying -----------------------------------------------------------

    // A flight with no capture burns at all: every probe the correctors run.
    [[nodiscard]] FlightResult fly(const Candidate& candidate, const Vec3& departure_velocity,
                                   time::CoordinateTime until, propagation::Trajectory* arc,
                                   bool stop_on_impact) {
        const CaptureSequence none{};
        return fly(candidate, departure_velocity, until, arc, stop_on_impact, none);
    }

    [[nodiscard]] FlightResult fly(const Candidate& candidate, const Vec3& departure_velocity,
                                   time::CoordinateTime until, propagation::Trajectory* arc,
                                   bool stop_on_impact, const CaptureSequence& capture) {
        if (config_.execution == ExecutionModel::Impulsive) {
            return fly_impulsive(candidate, departure_velocity, until, arc, stop_on_impact,
                                 capture);
        }
        if (config_.execution == ExecutionModel::Autopilot) {
            return fly_autopilot(candidate, departure_velocity, until, arc, stop_on_impact,
                                 capture);
        }
        return fly_finite(candidate, departure_velocity, until, arc, stop_on_impact, capture);
    }

    // The capture burns as MANEUVERS.  One builder, used by the finite model, the
    // autopilot model and the capture corrector alike, so that what the corrector
    // solves for and what the ship is eventually armed with cannot drift apart.
    [[nodiscard]] bool add_capture_burns(ManeuverPlan& plan, const CaptureSequence& capture,
                                         GuidanceMode guidance) const {
        if (!capture.active) {
            return true;
        }
        try {
            if (capture.capture_delta_v > 0.0 && capture.mass_at_capture > 0.0) {
                plan.add(maneuver_for_delta_v(*inputs_.craft, capture.mass_at_capture,
                                              capture.capture_delta_v, capture.capture_at,
                                              guidance, inputs_.target, 1.0, "insertion",
                                              BurnCentering::CenterOnIgnition));
            }
            if (capture.has_circularisation && capture.circularisation_delta_v > 0.0 &&
                capture.mass_at_circularisation > 0.0) {
                // Under the autopilot the direction comes from the hull and the
                // controller is told where to point; under the ideal models the
                // guidance law is the direction.
                const GuidanceMode second = guidance == GuidanceMode::Hull
                                                ? GuidanceMode::Hull
                                                : capture.circularisation_guidance;
                plan.add(maneuver_for_delta_v(
                    *inputs_.craft, capture.mass_at_circularisation,
                    capture.circularisation_delta_v, capture.circularisation_at, second,
                    inputs_.target, 1.0, "circularisation", BurnCentering::CenterOnIgnition));
            }
        } catch (const std::exception&) {
            return false;   // the rocket equation refused
        }
        return true;
    }

    // Impulsive: the velocity is SET, the mass is debited by the rocket equation,
    // and no engine runs.  This is the trajectory with no control error in it at
    // all, and it is the reference the other two are measured against
    // (section 11 of the brief).
    [[nodiscard]] FlightResult fly_impulsive(const Candidate& candidate,
                                             const Vec3& departure_velocity,
                                             time::CoordinateTime until,
                                             propagation::Trajectory* arc, bool stop_on_impact,
                                             const CaptureSequence& capture) {
        FlightResult out{};
        auto forces = build_forces(nullptr);
        propagation::DormandPrince54Propagator propagator{*forces,
                                                          integrator_for(stop_on_impact)};

        auto state = candidate.state;
        const double mass_at_departure = state.mass;
        const double injection_dv = (departure_velocity - state.state.velocity).norm();
        state.mass -= propellant_for(state.mass, injection_dv);
        state.state.velocity = departure_velocity;

        // Each burn is an instantaneous change of velocity applied at a break in
        // the propagation. Two of them when the sequence carries a
        // circularisation, and the impulsive model is the one place where a
        // second burn costs nothing extra to represent -- it has no duration.
        struct Impulse {
            time::CoordinateTime at{};
            double delta_v{0.0};
        };
        std::vector<Impulse> impulses;
        if (capture.active && capture.capture_delta_v > 0.0 &&
            capture.capture_at > candidate.departure && capture.capture_at < until) {
            impulses.push_back({capture.capture_at, capture.capture_delta_v});
        }
        if (capture.active && capture.has_circularisation &&
            capture.circularisation_delta_v > 0.0 &&
            capture.circularisation_at > candidate.departure &&
            capture.circularisation_at < until) {
            impulses.push_back({capture.circularisation_at, capture.circularisation_delta_v});
        }

        std::vector<time::CoordinateTime> breaks;
        breaks.reserve(impulses.size() + 1);
        for (const auto& impulse_at : impulses) {
            breaks.push_back(impulse_at.at);
        }
        breaks.push_back(until);

        propagation::Trajectory leg;
        auto t = candidate.departure;
        for (std::size_t i = 0; i < breaks.size(); ++i) {
            if (breaks[i] <= t) {
                continue;
            }
            propagator.set_trajectory_recorder(arc != nullptr ? &leg : nullptr);
            const auto result = propagator.propagate(state, t, breaks[i]);
            account(result.stats);
            if (arc != nullptr) {
                for (const auto& segment : leg.segments()) {
                    arc->append(segment);
                }
                leg.clear();
            }
            state = result.state;
            t = result.time;
            out.status = result.status;
            out.message = result.message;
            if (!result.ok()) {
                out.ok = false;
                break;
            }
            if (i < impulses.size()) {
                const auto body = inputs_.provider->state(inputs_.target, t, ssb_);
                const Vec3 relative = state.state.velocity - body.state.velocity;
                if (relative.norm() > 0.0) {
                    state.mass -= propellant_for(state.mass, impulses[i].delta_v);
                    state.state.velocity -= relative.normalized() * impulses[i].delta_v;
                }
                if (i == 0) {
                    out.capture_ignition = t;
                    out.capture_duration = 0.0;
                } else {
                    out.circularisation_ignition = t;
                    out.circularisation_duration = 0.0;
                }
            }
        }
        out.state = state;
        out.time = t;
        out.propellant_used = mass_at_departure - state.mass;
        return out;
    }

    // Finite: the engine runs.  The injection points along a fixed inertial
    // direction, the insertion along the instantaneous retrograde about the
    // target, and both are centred on the instant the impulsive plan assumed so
    // that half of each burn happens before it and half after.
    [[nodiscard]] FlightResult fly_finite(const Candidate& candidate,
                                          const Vec3& departure_velocity,
                                          time::CoordinateTime until,
                                          propagation::Trajectory* arc, bool stop_on_impact,
                                          const CaptureSequence& capture) {
        FlightResult out{};
        out.state = candidate.state;
        out.time = candidate.departure;

        const Vec3 impulse = departure_velocity - candidate.state.state.velocity;
        ManeuverPlan plan;
        try {
            auto injection = maneuver_for_delta_v(*inputs_.craft, candidate.state.mass,
                                                  impulse.norm(), candidate.departure,
                                                  GuidanceMode::Inertial, inputs_.center, 1.0,
                                                  "injection", BurnCentering::CenterOnIgnition);
            injection.ignition = candidate.departure;
            injection.inertial_direction = impulse.normalized();
            plan.add(std::move(injection));
        } catch (const std::exception& e) {
            // The rocket equation refused: the ship cannot pay for this plan.
            out.ok = false;
            out.status = propagation::PropagationStatus::OutOfPropellant;
            out.message = e.what();
            return out;
        }
        if (!add_capture_burns(plan, capture, GuidanceMode::Retrograde)) {
            out.ok = false;
            out.status = propagation::PropagationStatus::OutOfPropellant;
            out.message = "the ship cannot pay for the capture sequence";
            return out;
        }

        ManeuverExecutor executor{*inputs_.provider, *inputs_.craft, plan, ssb_};
        auto forces = build_forces(&executor);
        propagation::DormandPrince54Propagator propagator{*forces,
                                                          integrator_for(stop_on_impact)};
        const auto mission =
            run_mission(propagator, executor, candidate.state, candidate.departure, until, arc);
        account(mission.stats);

        out.state = mission.state;
        out.time = mission.time;
        out.status = mission.status;
        out.message = mission.message;
        out.ok = mission.ok();
        out.propellant_used = candidate.state.mass - mission.state.mass;
        for (const auto& burn : mission.burns) {
            if (burn.name == "insertion") {
                out.capture_ignition = burn.ignition;
                out.capture_duration = burn.duration.seconds();
            } else if (burn.name == "circularisation") {
                out.circularisation_ignition = burn.ignition;
                out.circularisation_duration = burn.duration.seconds();
            }
        }
        out.plan = plan;
        return out;
    }

    // ---- closest approach -------------------------------------------------

    [[nodiscard]] double separation_at(const propagation::Trajectory& arc,
                                       time::CoordinateTime t) const {
        const auto state = arc.state_at(t);
        const auto body = inputs_.provider->state(inputs_.target, t, ssb_);
        return (state.state.position - body.state.position).norm();
    }

    [[nodiscard]] Approach find_approach(const propagation::Trajectory& arc) const {
        Approach best{};
        if (arc.empty()) {
            return best;
        }
        const auto samples = arc.sample(config_.approach_bracket_samples);
        std::size_t index = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto body = inputs_.provider->state(inputs_.target, samples[i].first, ssb_);
            const double distance =
                (samples[i].second.state.position - body.state.position).norm();
            if (distance < best.distance) {
                best.distance = distance;
                best.time = samples[i].first;
                index = i;
            }
        }
        if (samples.size() < 3) {
            return best;
        }
        // Golden section on the bracketing interval: 60 iterations takes a
        // four-day arc's 200 s sampling to well below a microsecond, far finer
        // than the finite-difference step needs.
        double lo = samples[index > 0 ? index - 1 : 0].first.seconds_since_j2000();
        double hi = samples[std::min(index + 1, samples.size() - 1)].first.seconds_since_j2000();
        for (int i = 0; i < 60 && hi - lo > 1.0e-6; ++i) {
            const double a = hi - (hi - lo) * kInverseGoldenRatio;
            const double b = lo + (hi - lo) * kInverseGoldenRatio;
            const auto ta = time::CoordinateTime::from_seconds_since_j2000(a);
            const auto tb = time::CoordinateTime::from_seconds_since_j2000(b);
            if (!arc.contains(ta) || !arc.contains(tb)) {
                break;
            }
            if (separation_at(arc, ta) < separation_at(arc, tb)) {
                hi = b;
            } else {
                lo = a;
            }
        }
        const auto t_ca = time::CoordinateTime::from_seconds_since_j2000(0.5 * (lo + hi));
        if (!arc.contains(t_ca)) {
            return best;
        }
        const auto state = arc.state_at(t_ca);
        const auto body = inputs_.provider->state(inputs_.target, t_ca, ssb_);
        best.time = t_ca;
        best.relative_position = state.state.position - body.state.position;
        best.relative_velocity = state.state.velocity - body.state.velocity;
        best.distance = best.relative_position.norm();
        best.valid = true;
        return best;
    }

    // ---- the aim point ----------------------------------------------------
    //
    // Where stage 1 should put the spacecraft, and it is NOT the target's centre.
    //
    // Aiming the first stage at the centre is aiming at an impact, and that is
    // not a stylistic objection: the probe trajectories of stage 2 then start out
    // passing 600 m from the Moon's centre, where the point-mass acceleration is
    // 10^11 m/s^2 and the error controller grinds the step to its floor.  All six
    // of the Milestone 6 campaign's "timeouts" were that, and one of them took
    // 160 seconds to report a stall.
    //
    // So stage 1 aims at the periapsis the mission actually wants, using the
    // B-plane frame built from the Lambert arrival's v_infinity.  The estimate
    // does not have to be good -- stage 2 measures and re-aims -- it only has to
    // be a flyby rather than a collision.
    [[nodiscard]] Vec3 aim_point(const Candidate& candidate,
                                 time::CoordinateTime t_arrive) const {
        const auto target = inputs_.provider->state(inputs_.target, t_arrive, ssb_);
        // The candidate's own arrival excess velocity, measured when it was
        // screened and in the frame Lambert was solved in.  Recomputing it here
        // from `solution.arrival_velocity` would have to know which frame that
        // was -- and getting it wrong is the kind of mistake that produces a
        // perfectly converged corrector aiming at the wrong side of the planet.
        const Vec3 v_infinity = candidate.v_infinity_vector;
        const double speed = v_infinity.norm();
        if (!(speed > 0.0)) {
            return target.state.position;
        }
        const Vec3 s_hat = v_infinity / speed;
        Vec3 t_raw = cross(s_hat, kDefaultBPlanePole);
        if (!(t_raw.norm() > 0.0)) {
            t_raw = cross(s_hat, Vec3::unit_x());
        }
        const Vec3 t_hat = t_raw.normalized();
        const Vec3 r_hat = cross(s_hat, t_hat);
        const double b = impact_parameter_for_periapsis(wanted_periapsis_, gm_target_, speed);
        const double angle = config_.b_plane_angle.radians();
        return target.state.position + t_hat * (b * std::cos(angle)) +
               r_hat * (b * std::sin(angle));
    }

    // ---- classification of a flown arc ------------------------------------

    [[nodiscard]] TransferFailure classify(const FlightResult& flight) const {
        switch (flight.status) {
            case propagation::PropagationStatus::Success:
                return TransferFailure::None;
            case propagation::PropagationStatus::InsideBody: {
                // WHICH body.  "Entered a body" is not a classification; the whole
                // point of the taxonomy is that flying into the Earth on departure
                // and flying into the Moon on arrival are different failures with
                // different fixes.
                const auto center = inputs_.provider->state(inputs_.center, flight.time, ssb_);
                const auto target = inputs_.provider->state(inputs_.target, flight.time, ssb_);
                const double to_center =
                    (flight.state.state.position - center.state.position).norm();
                const double to_target =
                    (flight.state.state.position - target.state.position).norm();
                if (to_target <= radius_target_ * 1.001) {
                    return TransferFailure::TargetImpact;
                }
                if (radius_center_ > 0.0 && to_center <= radius_center_ * 1.001) {
                    return TransferFailure::DepartureConicHitsCentralBody;
                }
                return TransferFailure::TargetImpact;
            }
            case propagation::PropagationStatus::MaxStepsExceeded:
                return TransferFailure::Timeout;
            case propagation::PropagationStatus::OutOfPropellant:
                return TransferFailure::InsufficientCaptureDeltaV;
            case propagation::PropagationStatus::MinimumStepReached:
            case propagation::PropagationStatus::NonFiniteState:
            case propagation::PropagationStatus::InvariantViolation:
            case propagation::PropagationStatus::UnsupportedRegime:
                return TransferFailure::NumericalFailure;
        }
        return TransferFailure::NumericalFailure;
    }

    [[nodiscard]] TransferRecord attempt(const Candidate& candidate);

    void fill_search_counters(TransferRecord& record, const Rejections& rejections) const {
        record.candidates_considered = rejections.considered;
        record.rejected_no_lambert = rejections.no_lambert;
        record.rejected_transfer_angle = rejections.transfer_angle;
        record.rejected_departure_conic = rejections.departure_conic;
        record.rejected_delta_v = rejections.delta_v;
    }

    const TransferInputs& inputs_;
    const TransferConfig& config_;
    coordinates::ReferenceFrame ssb_;
    coordinates::ReferenceFrame centered_;

    // Which two-body problem the candidates come from, and the body it is posed
    // about. For a local transfer `primary_` is the origin and `primary_frame_`
    // is `centered_`; the code below then reduces, term by term, to what the
    // 365/365 campaign ran.
    TransferGeometry geometry_{TransferGeometry::Local};
    celestial::BodyId primary_{};
    coordinates::ReferenceFrame primary_frame_{};

    double gm_center_{0.0};
    double gm_target_{0.0};
    double gm_primary_{0.0};
    double radius_center_{0.0};
    double radius_target_{0.0};
    double wanted_periapsis_{0.0};
    propagation::Trajectory coast_{};
    std::string coast_message_;
    std::size_t steps_{0};
    std::size_t propagations_{0};
};

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Where the flyby is AIMED, as a solved quantity rather than a wish.
//
// The B-plane aims the hyperbola's periapsis at the altitude the mission asked
// for, and for a lunar capture that is the right aim: the burn lasts 83 s, the
// periapsis barely moves, and the orbit comes out where the aim was.
//
// For a Mars capture at this engine's arrival speeds it is not.  The burn is
// lit on the way IN, so the ship is already slower than the hyperbola says when
// it reaches its closest point and falls short of it.  Measured on the first
// Earth-Mars plan: aimed at 500 km, the capture ellipse's periapsis came out at
// -523 km -- through the planet -- once the burn was sized to bring the apoapsis
// down to 500.  The aim was not wrong by a little; it was the wrong question.
//
// So the aim ANTICIPATES the droop.  Fly it once, measure how far the periapsis
// fell, aim that much higher, fly it again.  Two or three passes, each a full
// departure correction, and the loop stops the moment the orbit lands in the
// requested band -- which is the first pass, always, for every lunar transfer.
// ---------------------------------------------------------------------------
TransferRecord TransferSession::attempt(const Candidate& candidate) {
    double aim_altitude = config_.flyby_altitude;
    TransferRecord best{};
    best.failure = TransferFailure::NoFeasibleTrajectory;

    // The departure velocity each pass STARTS from.
    //
    // Warm-started from the previous pass, and that is what makes the aim loop
    // affordable rather than merely correct. Cold, each pass re-runs a full
    // two-stage correction from the Lambert guess -- about four hundred
    // propagations of a two-hundred-day arc, several minutes apiece. Warm, the
    // aim has moved by a thousand kilometres of periapsis out of two hundred
    // million of transfer, so the previous answer is already nearly this one's
    // and the corrector converges in a couple of iterations.
    Vec3 departure_guess = candidate.departure_velocity;

    for (int pass = 0; pass < std::max(1, config_.aim_passes); ++pass) {
        if (pass > 0 && config_.cancelled && config_.cancelled()) {
            break;
        }
        auto record = attempt_with_aim(candidate, aim_altitude, departure_guess);
        if (record.departure_velocity.norm() > 0.0) {
            departure_guess = record.departure_velocity;
        }
        record.aim_passes = pass + 1;
        record.aim_altitude = aim_altitude;
        if (record.success) {
            return record;
        }
        // Keep the most informative attempt, and only retry when there is a
        // MEASURED droop to correct: a case that never reached the target, or
        // that failed for a reason the aim cannot fix, is not helped by aiming
        // somewhere else.
        if (pass == 0 || record.capture_corrector_evaluations > 0) {
            best = record;
        }
        // The droop this pass measured, and it is measured rather than modelled:
        // one flight of the impulsive capture burn, with the achieved periapsis
        // read off the orbit it produced.
        const double achieved = record.intermediate_periapsis_altitude;
        if (achieved == 0.0 || !std::isfinite(achieved)) {
            break;   // the pass failed before it could measure anything
        }
        const double correction = config_.target_orbit.mean_altitude - achieved;
        if (!(correction > 1.0e3)) {
            break;   // the aim is not what is wrong
        }
        aim_altitude += correction;
    }
    return best;
}

// A stage-by-stage trace of where a search spends itself, on stderr, behind an
// environment variable.
//
// Rule 51 asks for the search to be INSTRUMENTED, and the first Earth-Mars runs
// showed why: an attempt that takes ten minutes tells you nothing about which of
// its four stages took them, and three rounds of guessing at that question cost
// more than writing this did.
namespace {
bool trace_enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SPACEFLIGHT_TRACE_SEARCH");
        return value != nullptr && *value != '\0' && *value != '0';
    }();
    return enabled;
}
}  // namespace

TransferRecord TransferSession::attempt_with_aim(const Candidate& candidate,
                                                 double aim_altitude,
                                                 const Vec3& departure_guess) {
    const auto stage_clock = std::chrono::steady_clock::now();
    auto mark = [&, last = stage_clock, steps = steps_,
                 props = propagations_](const char* what) mutable {
        if (!trace_enabled()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        std::cerr << "    [trace] " << std::left << std::setw(22) << what
                  << std::chrono::duration<double>(now - last).count() << " s, "
                  << (propagations_ - props) << " propagations, " << (steps_ - steps)
                  << " steps\n";
        last = now;
        steps = steps_;
        props = propagations_;
    };
    // The aim for THIS pass, and the band the achieved periapsis is judged
    // against, which has to move with it: a flyby deliberately aimed 1500 km out
    // is not a flyby that came out 1000 km too high.
    wanted_periapsis_ = radius_target_ + aim_altitude;
    const double band_low = std::max(0.0, aim_altitude - 80.0e3);
    const double band_high = aim_altitude + 300.0e3;

    TransferRecord record{};
    record.requested_epoch = inputs_.epoch;
    record.departure_epoch = candidate.departure;
    record.departure_coast_s = candidate.coast_s;
    record.time_of_flight_s = candidate.tof_s;
    record.execution = config_.execution;
    record.lambert_solution_id = candidate.id;
    record.lambert_direction = candidate.direction;
    record.transfer_angle_rad = candidate.transfer_angle;
    record.lambert_departure_velocity = candidate.solution.departure_velocity;
    record.lambert_arrival_velocity = candidate.solution.arrival_velocity;
    record.lambert_delta_v = candidate.delta_v;
    record.initial_flown_miss = candidate.flown_miss;
    record.departure_perigee_radius = candidate.departure_perigee;
    record.departure_conic_eccentricity = candidate.departure_eccentricity;

    const auto t_arrive = candidate.departure + time::Duration::seconds(candidate.tof_s);
    record.arrival_epoch = t_arrive;

    const auto center_at_departure =
        inputs_.provider->state(inputs_.center, candidate.departure, ssb_);
    record.center_state = center_at_departure.state;
    record.spacecraft_initial_state.position =
        candidate.state.state.position - center_at_departure.state.position;
    record.spacecraft_initial_state.velocity =
        candidate.state.state.velocity - center_at_departure.state.velocity;
    record.target_departure_state =
        inputs_.provider->state(inputs_.target, candidate.departure, centered_).state;
    record.target_arrival_state =
        inputs_.provider->state(inputs_.target, t_arrive, centered_).state;

    // The probe flies PAST the nominal arrival.  Closest approach has to be
    // INTERIOR to the window or it is pinned at the end of it -- and then the
    // time of closest approach stops responding to the departure velocity, the
    // third row of the Jacobian goes to zero, and Newton has nothing to solve.
    // A quarter of the transfer for a lunar arc -- about a day, which is what
    // the campaign qualified -- and a twentieth for an interplanetary one. It
    // only has to be long enough that the minimum is interior, and on a
    // two-hundred-day transfer a quarter is fifty extra days of integration on
    // EVERY probe flight the correctors make.
    const double probe_overshoot =
        geometry_ == TransferGeometry::Local ? 0.25 : 0.05;
    const auto t_probe_end =
        t_arrive + time::Duration::seconds(candidate.tof_s * probe_overshoot);

    // ---- stage 1: reach the flyby ----------------------------------------
    const Vec3 target_aim = aim_point(candidate, t_arrive);
    auto stage_one_config = departure_targeting_for(candidate);
    const auto reach = correct_departure(
        [&](const Vec3& v) {
            return fly(candidate, v, t_arrive, nullptr, false).state.state.position;
        },
        departure_guess, target_aim, stage_one_config);

    record.position_corrector_iterations = reach.iterations;
    record.position_corrector_evaluations = reach.evaluations;
    record.position_corrector_residual = reach.miss_distance;
    record.position_corrector_converged = reach.converged;
    record.position_corrector_message = reach.message;

    Vec3 departure_velocity = reach.departure_velocity;
    mark("stage 1 reach");

    // ---- stage 2: shape the flyby ----------------------------------------
    //
    // v_infinity is needed to turn "100 km altitude" into a |B|, and it is itself
    // a property of the trajectory.  So: measure it, aim, re-measure.  The outer
    // loop is there to prove it barely moves rather than to assume it.
    auto measure_b_plane = [&](const Vec3& v) -> std::optional<BPlane> {
        propagation::Trajectory probe;
        (void)fly(candidate, v, t_probe_end, &probe, false);
        const Approach approach = find_approach(probe);
        if (!approach.valid) {
            return std::nullopt;
        }
        try {
            return b_plane_from_state(approach.relative_position, approach.relative_velocity,
                                      gm_target_);
        } catch (const std::domain_error&) {
            return std::nullopt;   // not hyperbolic about the target
        }
    };

    TargetingResult correction{};
    correction.departure_velocity = departure_velocity;
    const double t_target = t_arrive.seconds_since_j2000();

    for (int pass = 0; pass < config_.b_plane_passes; ++pass) {
        const auto measured = measure_b_plane(departure_velocity);
        if (!measured.has_value()) {
            record.failure = TransferFailure::InvalidBPlane;
            record.detail = "no hyperbolic approach to read v_infinity from after " +
                            std::to_string(pass) + " pass(es); the transfer does not reach the "
                                                   "target";
            record.bplane_passes = pass;
            return record;
        }
        record.v_infinity = measured->v_infinity;
        const auto aim = aim_for_periapsis(wanted_periapsis_, gm_target_, measured->v_infinity,
                                           config_.b_plane_angle);
        record.bplane_target = aim;

        // The corrector's tolerance is on |B|; what the mission cares about is
        // the PERIAPSIS.  Differentiating b^2 = r_p^2 + 2 mu r_p / v_inf^2 gives
        // dr_p/db = b / (r_p + mu / v_inf^2), which here is 0.61: 10 km of slack
        // on B is 6 km of periapsis.  So the tolerance is converted, not shared.
        const double aim_magnitude = std::hypot(aim.b_dot_t, aim.b_dot_r);
        const double dr_p_db =
            aim_magnitude /
            (wanted_periapsis_ + gm_target_ / (measured->v_infinity * measured->v_infinity));
        auto b_targeting = config_.b_plane_targeting;
        b_targeting.position_tolerance = config_.periapsis_tolerance / std::max(dr_p_db, 1.0e-6);

        correction = correct_departure(
            [&](const Vec3& v) -> Vec3 {
                propagation::Trajectory probe;
                (void)fly(candidate, v, t_probe_end, &probe, false);
                const Approach approach = find_approach(probe);
                if (!approach.valid) {
                    return Vec3{1.0e12, 1.0e12, 1.0e12};   // push the solver away
                }
                try {
                    const auto bp = b_plane_from_state(approach.relative_position,
                                                       approach.relative_velocity, gm_target_);
                    // Three outputs for three inputs, and all three in METRES, so
                    // that the norm the corrector tests against a tolerance means
                    // something.  The third is the along-track timing error:
                    // closest approach should happen at the nominal arrival.
                    return Vec3{bp.b_dot_t, bp.b_dot_r,
                                bp.v_infinity *
                                    (approach.time.seconds_since_j2000() - t_target)};
                } catch (const std::domain_error&) {
                    return Vec3{1.0e12, 1.0e12, 1.0e12};
                }
            },
            departure_velocity, Vec3{aim.b_dot_t, aim.b_dot_r, 0.0}, b_targeting);

        departure_velocity = correction.departure_velocity;
        record.bplane_passes = pass + 1;
        record.bplane_iterations += correction.iterations;
        record.bplane_message = correction.message;
        record.bplane_converged = correction.converged;
        record.bplane_error = correction.miss_distance;

        const auto achieved = measure_b_plane(departure_velocity);
        if (!achieved.has_value()) {
            record.failure = TransferFailure::InvalidBPlane;
            record.detail = "the corrected departure no longer produces a hyperbolic approach";
            return record;
        }
        record.bplane_actual_t = achieved->b_dot_t;
        record.bplane_actual_r = achieved->b_dot_r;
        record.predicted_periapsis = achieved->periapsis_radius;
        record.v_infinity = achieved->v_infinity;
        if (std::abs(achieved->periapsis_radius - wanted_periapsis_) <
            config_.periapsis_tolerance) {
            record.bplane_converged = true;
            break;
        }
    }

    mark("stage 2 b-plane");
    record.correction_magnitude =
        (departure_velocity - candidate.departure_velocity).norm();
    record.departure_velocity = departure_velocity;
    record.departure_delta_v =
        (departure_velocity - candidate.state.state.velocity).norm();

    if (!record.bplane_converged) {
        // Stagnation and divergence are different diagnoses, and the corrector
        // already distinguishes them in its message: a singular Jacobian or a
        // non-finite arrival is divergence, and "no step along the Newton
        // direction reduces the miss" is stagnation.  Collapsing the two into one
        // label would throw away the only information that says whether the
        // geometry is wrong or the iteration merely stopped helping.
        const bool stalled = correction.message.find("stalled") != std::string::npos;
        record.failure = stalled ? TransferFailure::DepartureCorrectorStagnated
                                 : TransferFailure::BPlaneCorrectorDiverged;
        record.detail = correction.message;
        return record;
    }

    // ---- the approach, as flown ------------------------------------------
    propagation::Trajectory approach_arc;
    const auto approach_flight =
        fly(candidate, departure_velocity, t_probe_end, &approach_arc, true);
    if (!approach_flight.ok) {
        record.failure = classify(approach_flight);
        record.detail = approach_flight.message;
        return record;
    }
    mark("approach as flown");
    const Approach approach = find_approach(approach_arc);
    if (!approach.valid) {
        record.failure = TransferFailure::InvalidBPlane;
        record.detail = "the flown arc has no closest approach inside the window";
        return record;
    }

    record.closest_approach_epoch = approach.time;
    record.target_relative_position = approach.relative_position;
    record.target_relative_velocity = approach.relative_velocity;
    record.actual_periapsis = approach.distance;
    record.target_periapsis = wanted_periapsis_;
    record.periapsis_error = approach.distance - wanted_periapsis_;
    record.periapsis_velocity = approach.relative_velocity.norm();

    const double periapsis_altitude = approach.distance - radius_target_;
    if (periapsis_altitude < band_low) {
        record.failure = periapsis_altitude <= 0.0 ? TransferFailure::TargetImpact
                                                   : TransferFailure::PeriapsisTooLow;
        record.detail = "flown periapsis altitude " + fixed(periapsis_altitude / 1000.0, 3) +
                        " km";
        return record;
    }
    if (periapsis_altitude > band_high) {
        record.failure = TransferFailure::PeriapsisTooHigh;
        record.detail = "flown periapsis altitude " + fixed(periapsis_altitude / 1000.0, 3) +
                        " km";
        return record;
    }

    // ---- the capture ------------------------------------------------------
    BPlane bp{};
    try {
        bp = b_plane_from_state(approach.relative_position, approach.relative_velocity,
                                gm_target_);
    } catch (const std::domain_error& e) {
        record.failure = TransferFailure::InvalidBPlane;
        record.detail = e.what();
        return record;
    }
    record.v_infinity = bp.v_infinity;
    record.predicted_periapsis = bp.periapsis_radius;

    const double energy_before = 0.5 * approach.relative_velocity.norm_squared() -
                                 gm_target_ / approach.distance;
    record.energy_before_burn = energy_before;

    const auto burn = plan_insertion(bp.periapsis_radius, gm_target_, bp.v_infinity, 0.0);

    CaptureSequence sequence{};
    sequence.active = true;
    sequence.capture_delta_v = burn.delta_v;
    // Where the burn sits relative to periapsis (section 12).  Zero is centred on
    // it; the campaign tool sweeps this.
    sequence.capture_at =
        approach.time + time::Duration::seconds(config_.capture_burn_offset_seconds);
    record.burn_offset_from_periapsis_s = config_.capture_burn_offset_seconds;

    // The mass at the burn is not known until the coast has been flown, and
    // using the departure mass would ask the engine for a duration wrong by the
    // whole injection's propellant.  So: fly the coast once with no capture,
    // read the mass there, then plan the burn.
    const auto coast_to_burn = fly(candidate, departure_velocity, sequence.capture_at, nullptr,
                                   true);
    if (!coast_to_burn.ok) {
        record.failure = classify(coast_to_burn);
        record.detail = coast_to_burn.message;
        return record;
    }
    sequence.mass_at_capture = coast_to_burn.state.mass;
    const double mass_at_capture = sequence.mass_at_capture;
    record.required_capture_delta_v = burn.delta_v;
    record.available_capture_delta_v = inputs_.craft->delta_v_budget(mass_at_capture);
    if (record.required_capture_delta_v > record.available_capture_delta_v) {
        record.failure = TransferFailure::InsufficientCaptureDeltaV;
        record.detail = "needs " + fixed(record.required_capture_delta_v, 3) + " m/s, has " +
                        fixed(record.available_capture_delta_v, 3) + " m/s";
        return record;
    }

    // ---- is the impulsive plan actually an impulse? -----------------------
    //
    // Flown once, cheaply, from a state already on the approach, and the ORBIT it
    // produces is compared with the one the mission asked for.  When it matches
    // -- which is every lunar capture, where the burn lasts 83 s -- nothing below
    // runs, the sequence stays one burn long, and the trajectory is exactly the
    // one the 365/365 campaign qualified.
    //
    // When it does not, the two burns are SOLVED against the flight instead of
    // predicted from an impulse.  See the note on CaptureSequence.
    const double wanted_altitude = config_.target_orbit.mean_altitude;
    const double settle = std::min(burn.period * 2.0, 6.0 * 3600.0);

    // Not under the AUTOPILOT model, and this is a stated limitation rather than
    // an oversight.
    //
    // The short flights below are what make solving the capture affordable: they
    // start on the approach instead of at departure, so an evaluation costs hours
    // instead of months. What they cannot do is carry the autopilot, because the
    // attitude controller, the inertia tensor and the twelve thrusters are built
    // by fly_autopilot() and are not part of the force model these flights
    // assemble. Running a Hull-guided burn without them produces a ship that
    // points nowhere -- which is exactly what happened: the qualified lunar
    // AUTOPILOT case started failing with "the capture burn did not leave a
    // readable orbit".
    //
    // So under AUTOPILOT the capture stays one burn, as it was, and the lunar
    // campaign is untouched. An autopilot-flown interplanetary capture is
    // therefore not available in this milestone; it is in the backlog, and what
    // it needs is for the attitude stack to be built once and shared rather than
    // assembled inside one flight function.
    if (config_.execution != ExecutionModel::Autopilot) {
        const double impulsive_duration = inputs_.craft->engine().burn_duration_for_delta_v(
            mass_at_capture, burn.delta_v, 1.0);
        // Far enough before any plausible ignition that the arc contains the whole
        // burn whatever the solver does to its length.
        const auto arc_start =
            approach.time - time::Duration::seconds(3.0 * impulsive_duration + 120.0);
        const auto to_arc_start = fly(candidate, departure_velocity, arc_start, nullptr, true);
        if (!to_arc_start.ok) {
            record.failure = classify(to_arc_start);
            record.detail = to_arc_start.message;
            return record;
        }

        const auto first = fly_capture_from(
            to_arc_start.state, to_arc_start.time, sequence,
            sequence.capture_at + time::Duration::seconds(settle));
        if (!first.has_value()) {
            record.failure = TransferFailure::TargetOrbitNotAchieved;
            record.detail = "the capture burn did not leave a readable orbit";
            return record;
        }

        // The DROOP, measured, and this one flight is what the whole aim loop
        // runs on: the impulsive burn was sized to circularise at the altitude
        // the flyby was aimed at, so however far the periapsis falls short of
        // that aim is how far the finite burn moves it.
        //
        // Recorded before anything is decided, because a pass that is going to
        // fail still has to hand the next one this number.
        record.intermediate_periapsis_altitude = first->periapsis_radius - radius_target_;

        const bool good =
            std::isfinite(first->apoapsis_radius) &&
            config_.target_orbit.check(first->periapsis_radius - radius_target_,
                                       first->apoapsis_radius - radius_target_,
                                       first->eccentricity) == TransferFailure::None;

        // Is the AIM wrong, or only the shape?
        //
        // Two different failures with two different fixes, and spending flights
        // on the second while the first is outstanding is what made the early
        // versions of this take ten minutes. If the periapsis has fallen far
        // below what was asked for, no amount of solving the burns recovers it --
        // braking hard enough to bring the apoapsis down simply drives the
        // periapsis through the planet. The answer is to aim higher and fly
        // again, and that is the caller's loop.
        const double periapsis_shortfall =
            config_.target_orbit.mean_altitude - record.intermediate_periapsis_altitude;
        const double aim_slack =
            std::max(1.0e3, config_.target_orbit.mean_altitude -
                                config_.target_orbit.min_periapsis_altitude);
        if (!good && periapsis_shortfall > aim_slack) {
            record.failure = TransferFailure::TargetOrbitNotAchieved;
            record.detail =
                "aimed at " + fixed(aim_altitude / 1000.0, 1) +
                " km, the capture burn leaves periapsis at " +
                fixed(record.intermediate_periapsis_altitude / 1000.0, 1) + " km: " +
                fixed(periapsis_shortfall / 1000.0, 1) + " km short of the requested orbit";
            return record;
        }

        if (!good) {
            // ---- the second burn (rule 11's CIRCULARIZATION) ---------------
            //
            // Burn ONE is left exactly as the two-body arithmetic sized it, and
            // that is the point: the aim loop above has already made its result
            // land on the periapsis that was asked for. What it cannot fix is the
            // SHAPE -- the orbit comes out roughly 500 x 2100 km, because a burn
            // that is lit across four Mars radii removes energy over an arc
            // rather than at a point.
            //
            // So the second burn is a plain apoapsis trim: retrograde, at the
            // periapsis the first one produced, lowering the far side to meet the
            // near one. About 270 m/s against the first burn's 7 300, which makes
            // it 27 seconds long instead of 730 -- and THAT is why it can close
            // what the first could not. A 27-second burn really is very nearly
            // the impulse the two-body arithmetic assumes it is.
            //
            // Two earlier assignments of these two burns are worth recording,
            // because each failed for a reason that says something about the
            // physics rather than about the code:
            //
            //   * solving burn one for the PERIAPSIS is degenerate. The periapsis
            //     rises monotonically as the braking shrinks, and at zero braking
            //     it is the incoming hyperbola's own periapsis -- which is the
            //     altitude that was asked for, because that is where the flyby
            //     was aimed. The solver converged on "do not burn", reported
            //     success, and handed back an orbit with no period.
            //
            //   * solving burn one for the APOAPSIS drives it through the planet.
            //     Braking hard enough to bring the far side down to 500 km takes
            //     the near side to -523 km, measured.
            //
            // One burn cannot produce a circle here. Two can, provided the first
            // is the one the aim loop has already fixed.
            const auto& ellipse = *first;
            record.intermediate_periapsis_altitude = ellipse.periapsis_radius - radius_target_;
            if (record.intermediate_periapsis_altitude <
                config_.minimum_capture_periapsis_altitude) {
                record.failure = TransferFailure::PeriapsisTooLow;
                record.detail =
                    "the capture ellipse's periapsis falls to " +
                    fixed(record.intermediate_periapsis_altitude / 1000.0, 1) +
                    " km, below the " +
                    fixed(config_.minimum_capture_periapsis_altitude / 1000.0, 1) +
                    " km floor, before the trim burn can be reached";
                return record;
            }
            const double period = ellipse.period;
            if (!(period > 0.0) || !std::isfinite(period)) {
                record.failure = TransferFailure::TargetOrbitNotAchieved;
                record.detail = "the capture burn did not leave a closed orbit to trim";
                return record;
            }

            const auto probe_end = sequence.capture_at + time::Duration::seconds(settle);
            sequence.has_circularisation = true;
            sequence.circularisation_guidance = GuidanceMode::Retrograde;
            sequence.circularisation_at =
                probe_end + time::Duration::seconds(seconds_to_periapsis(ellipse));

            // What it has to remove: from the ellipse's own periapsis speed down
            // to the circular speed there.
            const double r_periapsis = ellipse.periapsis_radius;
            const double v_periapsis = std::sqrt(std::max(
                0.0, gm_target_ * (2.0 / r_periapsis - 1.0 / ellipse.semi_major_axis)));
            const double v_circular = std::sqrt(gm_target_ / r_periapsis);
            const double guess = std::max(1.0, v_periapsis - v_circular);
            sequence.mass_at_circularisation =
                mass_at_capture - propellant_for(mass_at_capture, sequence.capture_delta_v);

            const double apoapsis_tolerance =
                std::max(1.0e3, 0.25 * (config_.target_orbit.max_apoapsis_altitude -
                                        config_.target_orbit.mean_altitude));
            const auto trim = solve_burn(
                to_arc_start.state, to_arc_start.time, sequence,
                [](CaptureSequence& trial, double delta_v) {
                    trial.circularisation_delta_v = delta_v;
                },
                [&](const trajectory::OrbitalElements& elements) {
                    return std::isfinite(elements.apoapsis_radius)
                               ? elements.apoapsis_radius - radius_target_
                               : std::numeric_limits<double>::infinity();
                },
                guess, wanted_altitude, apoapsis_tolerance, settle);

            record.capture_corrector_iterations = 1;
            record.capture_corrector_evaluations = trim.evaluations;
            record.capture_corrector_message = trim.message;
            record.capture_corrector_converged = trim.converged;
            if (!trim.converged) {
                record.failure = TransferFailure::TargetOrbitNotAchieved;
                record.detail = "the trim burn could not be solved for a " +
                                fixed(wanted_altitude / 1000.0, 1) + " km apoapsis: " +
                                trim.message;
                return record;
            }
            sequence.circularisation_delta_v = trim.delta_v;
            record.required_capture_delta_v = sequence.total_delta_v();
        }
    }

    // Two orbits past the last burn, so the result is an ORBIT and not a lucky
    // instant: the elements are read after a full revolution.
    const auto t_end = latest_burn_epoch(sequence) + time::Duration::seconds(burn.period * 2.0);
    auto captured_arc = std::make_shared<propagation::Trajectory>();
    const auto captured =
        fly(candidate, departure_velocity, t_end,
            config_.keep_trajectory ? captured_arc.get() : nullptr, true, sequence);
    if (!captured.ok) {
        record.failure = classify(captured);
        record.detail = captured.message;
        return record;
    }
    if (config_.keep_trajectory) {
        record.trajectory = captured_arc;
    }

    record.burn_start = captured.capture_ignition;
    record.burn_duration_s = captured.capture_duration;
    record.capture_pointing_error_mean_rad = captured.pointing_error_mean;
    record.capture_pointing_error_peak_rad = captured.pointing_error_peak;
    record.capture_rcs_propellant = captured.rcs_propellant;
    record.capture_rcs_duty_cycle = captured.rcs_duty_cycle;
    record.capture_torque_saturation = captured.torque_saturation;
    record.capture_settling_s = captured.settling_s;
    record.capture_angular_rate_peak = captured.angular_rate_peak;
    record.burn_end = captured.capture_ignition +
                      time::Duration::seconds(captured.capture_duration);
    record.circularisation_delta_v =
        sequence.has_circularisation ? sequence.circularisation_delta_v : 0.0;
    record.circularisation_start = captured.circularisation_ignition;
    record.circularisation_duration_s = captured.circularisation_duration;
    record.propellant_used = captured.propellant_used;
    record.propellant_left = captured.state.mass - inputs_.craft->dry_mass();
    // The plan the flight flew, verbatim -- this is what the scene is armed with
    // (section 5 of the Milestone 6.2 brief).  Under IMPULSIVE it is empty and
    // says so, because there is no engine in that model to hand a ship.
    record.flight_plan = captured.plan;
    record.mass_at_departure = candidate.state.mass;
    record.mass_at_capture = mass_at_capture;

    // Where the burn actually happened, which is what the Oberth question of
    // section 12 asks: the radius and speed at its MIDPOINT, not at its start.
    {
        const auto midpoint = captured.capture_ignition +
                              time::Duration::seconds(0.5 * captured.capture_duration);
        const auto probe =
            fly(candidate, departure_velocity, midpoint, nullptr, true, sequence);
        if (probe.ok) {
            const auto body = inputs_.provider->state(inputs_.target, probe.time, ssb_);
            record.burn_midpoint_radius =
                (probe.state.state.position - body.state.position).norm();
            record.burn_midpoint_speed =
                (probe.state.state.velocity - body.state.velocity).norm();
        }
    }

    const auto body_final = inputs_.provider->state(inputs_.target, captured.time, ssb_);
    coordinates::StateVector relative{};
    relative.position = captured.state.state.position - body_final.state.position;
    relative.velocity = captured.state.state.velocity - body_final.state.velocity;
    const auto elements = trajectory::elements_from_state(relative, gm_target_);

    record.post_burn_specific_energy = elements.specific_energy;
    record.post_burn_eccentricity = elements.eccentricity;
    record.post_burn_periapsis_altitude = elements.periapsis_radius - radius_target_;
    record.post_burn_apoapsis_altitude =
        std::isfinite(elements.apoapsis_radius) ? elements.apoapsis_radius - radius_target_
                                                : std::numeric_limits<double>::infinity();
    record.post_burn_inclination_rad = elements.inclination.radians();
    record.post_burn_raan_rad = elements.raan.radians();

    // Independently of the orbit test: did the burn do what a capture means?
    // epsilon > 0 before, epsilon < 0 after.  Two signs, checked, not assumed.
    if (!(record.post_burn_specific_energy < 0.0)) {
        record.failure = TransferFailure::PostBurnHyperbolic;
        record.detail = "specific energy before " + fixed(energy_before, 3) + " J/kg, after " +
                        fixed(record.post_burn_specific_energy, 3) + " J/kg";
        return record;
    }

    const auto orbit_check = config_.target_orbit.check(record.post_burn_periapsis_altitude,
                                                        record.post_burn_apoapsis_altitude,
                                                        record.post_burn_eccentricity);
    if (orbit_check != TransferFailure::None) {
        record.failure = orbit_check;
        record.detail = "orbit " + fixed(record.post_burn_periapsis_altitude / 1000.0, 3) +
                        " x " + fixed(record.post_burn_apoapsis_altitude / 1000.0, 3) +
                        " km, e = " + fixed(record.post_burn_eccentricity, 6);
        return record;
    }

    mark("final flight");
    record.success = true;
    record.failure = TransferFailure::None;
    record.cost = config_.cost.evaluate(TransferCostTerms{
        record.departure_delta_v, record.required_capture_delta_v,
        record.actual_periapsis - wanted_periapsis_, record.correction_magnitude, 0.0,
        record.time_of_flight_s / 86400.0, 0.0});
    return record;
}

TransferRecord TransferSession::run() {
    const auto started = std::chrono::steady_clock::now();
    TransferRecord record{};
    record.requested_epoch = inputs_.epoch;
    record.execution = config_.execution;

    if (!coast_parking_orbit()) {
        record.failure = TransferFailure::NumericalFailure;
        record.detail = "could not coast the parking orbit: " + coast_message_;
        return record;
    }

    Rejections rejections{};
    auto candidates = build_candidates(rejections);
    if (candidates.empty()) {
        // WHICH kind of nothing.  "No feasible trajectory" is true of a ship
        // with no propellant and of a calendar with no window, and section 21 of
        // the Milestone 6.2 brief asks the first to be told apart from the
        // second: a mission the ship cannot pay for has to come back as
        // INSUFFICIENT_DEPARTURE_DV, not as a shrug.
        //
        // Before this the code could never produce that label at all -- it
        // existed in the taxonomy and nothing assigned it -- so every
        // fuel-starved scenario was reported as if the geometry were the
        // problem.
        const bool starved = rejections.delta_v > 0 &&
                             rejections.delta_v >= rejections.no_lambert &&
                             rejections.delta_v >= rejections.transfer_angle &&
                             rejections.delta_v >= rejections.departure_conic;
        record.failure = starved ? TransferFailure::InsufficientDepartureDeltaV
                                 : TransferFailure::NoFeasibleTrajectory;
        std::ostringstream os;
        os << rejections.considered << " geometries considered: " << rejections.no_lambert
           << " with no Lambert solution, " << rejections.transfer_angle
           << " in the degenerate transfer-angle band, " << rejections.departure_conic
           << " whose post-injection conic re-enters the central body, "
           << rejections.departure_geometry
           << " where no departure hyperbola exists at that point in the parking orbit, "
           << rejections.delta_v << " the ship cannot pay for";
        record.detail = os.str();
        fill_search_counters(record, rejections);
        record.integrator_steps = steps_;
        return record;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.proxy_cost < b.proxy_cost; });
    if (static_cast<int>(candidates.size()) > config_.screened_candidates) {
        candidates.resize(static_cast<std::size_t>(config_.screened_candidates));
    }

    // Fly each one once, and PRICE the result.
    //
    // The cheapest Lambert solution is the wrong one to hand a corrector -- a
    // longer transfer approaches the minimum-energy ellipse and is also far more
    // sensitive to the departure velocity, which is what the corrector then has
    // to invert.  So the two-body cost is not the last word.  But neither is the
    // flown miss: ranking on it alone chose 18 km/s departures under the
    // impulsive model, because nothing in a miss distance knows what a burn
    // costs.
    //
    // The miss is a PREDICTION of correction effort, so it is converted into the
    // delta-v it implies -- one m/s of departure moves a trans-lunar arrival by
    // about 1e6 m -- and added to the same cost function everything else is
    // priced in.  One ranking, one rule, and the rule is section 8's.
    for (auto& candidate : candidates) {
        if (config_.cancelled && config_.cancelled()) {
            record.failure = TransferFailure::Cancelled;
            record.detail = "cancelled while ranking candidates";
            record.integrator_steps = steps_;
            record.propagations = propagations_;
            return record;
        }
        report_progress("ranking", rejections, static_cast<int>(candidates.size()), 0, 0, 0.0);
        const auto t_arrive = candidate.departure + time::Duration::seconds(candidate.tof_s);
        const auto flight = fly(candidate, candidate.departure_velocity, t_arrive, nullptr, false);
        const auto target = inputs_.provider->state(inputs_.target, flight.time, ssb_);
        candidate.flown_miss =
            flight.ok ? (flight.state.state.position - target.state.position).norm()
                      : std::numeric_limits<double>::infinity();
        const double predicted_correction = candidate.flown_miss / lever_arm_for(candidate);
        candidate.selection_score = config_.cost.evaluate(TransferCostTerms{
            candidate.delta_v, candidate.insertion_estimate, 0.0, predicted_correction,
            candidate.departure_conic_deficit, candidate.tof_s / 86400.0, 0.0});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.selection_score < b.selection_score;
    });

    const std::size_t attempts =
        std::min<std::size_t>(candidates.size(),
                              static_cast<std::size_t>(std::max(1, config_.flown_candidates)));

    TransferRecord best{};
    best.failure = TransferFailure::NoFeasibleTrajectory;
    bool have_best = false;
    int flown = 0;
    int succeeded = 0;

    for (std::size_t i = 0; i < attempts; ++i) {
        if (config_.cancelled && config_.cancelled()) {
            if (!have_best) {
                best.failure = TransferFailure::Cancelled;
                best.detail = "cancelled after " + std::to_string(flown) + " attempt(s)";
            }
            break;
        }
        report_progress("flying", rejections, static_cast<int>(candidates.size()), flown,
                        succeeded, have_best ? best.departure_delta_v : 0.0);
        if (steps_ > config_.step_budget) {
            // Out of budget.  Only SAY so when there is nothing better to say:
            // an attempt that got as far as "the orbit came out at e = 0.0104,
            // just outside the specification" has diagnosed the case, and
            // overwriting that with TIMEOUT because the retry loop then ran out
            // of steps would throw the diagnosis away and report the symptom.
            if (best.failure == TransferFailure::NoFeasibleTrajectory) {
                best.failure = TransferFailure::Timeout;
                best.detail =
                    "step budget exhausted after " + std::to_string(flown) + " attempt(s)";
            } else {
                best.detail += "  [and the step budget ran out after " +
                               std::to_string(flown) + " attempt(s)]";
            }
            break;
        }
        ++flown;
        auto attempt_record = attempt(candidates[i]);
        if (config_.on_attempt) {
            config_.on_attempt(attempt_record);
        }
        if (attempt_record.success) {
            if (!have_best || attempt_record.cost < best.cost) {
                best = attempt_record;
                have_best = true;
            }
            ++succeeded;
            // The candidates are ordered by the cost function, so the first one
            // that works is usually the answer; a couple more are attempted
            // because the ORDER is built from a two-body estimate of the
            // insertion and a predicted correction, and the flown numbers can
            // reorder neighbours.  Three is where that stops paying: measured
            // over 100 epochs, no fourth attempt ever displaced the winner.
            if (succeeded >= 3) {
                break;
            }
        } else if (!have_best) {
            // Keep the most informative failure: the one that got furthest.
            best = attempt_record;
        }
    }

    record = best;
    record.requested_epoch = inputs_.epoch;
    record.execution = config_.execution;
    record.candidates_feasible = static_cast<int>(candidates.size());
    record.candidates_flown = flown;
    fill_search_counters(record, rejections);
    record.integrator_steps = steps_;
    record.propagations = propagations_;
    record.wall_time_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return record;
}

std::vector<GridCell> TransferSession::map_grid() {
    std::vector<GridCell> grid;
    if (!coast_parking_orbit()) {
        return grid;
    }
    const auto samples = coast_.sample(static_cast<std::size_t>(std::max(2, config_.departure_samples)));
    std::vector<trajectory::TransferDirection> directions;
    if (config_.try_prograde) {
        directions.push_back(trajectory::TransferDirection::Prograde);
    }
    if (config_.try_retrograde) {
        directions.push_back(trajectory::TransferDirection::Retrograde);
    }

    for (const auto& [t_depart, state] : samples) {
        const auto center = inputs_.provider->state(inputs_.center, t_depart, ssb_);
        const auto primary = inputs_.provider->state(primary_, t_depart, ssb_);
        const Vec3 r1 = state.state.position - primary.state.position;
        for (const double tof_days : config_.time_of_flight_days) {
            const auto tof = time::Duration::days(tof_days);
            const Vec3 r2 = inputs_.provider->state(inputs_.target, t_depart + tof, primary_frame_)
                                .state.position;
            for (const auto direction : directions) {
                GridCell cell{};
                cell.departure_coast_s = (t_depart - inputs_.epoch).seconds();
                cell.time_of_flight_days = tof_days;
                cell.direction = direction;
                Rejections ignored{};
                (void)screen(t_depart, state, center, primary, r1, r2, tof, direction, ignored,
                             &cell);
                grid.push_back(cell);
            }
        }
    }
    return grid;
}

}  // namespace

// ---------------------------------------------------------------------------

TransferRecord plan_and_fly(const TransferInputs& inputs, const TransferConfig& config) {
    TransferSession session{inputs, config};
    return session.run();
}

std::vector<GridCell> map_transfer_grid(const TransferInputs& inputs,
                                        const TransferConfig& config) {
    TransferSession session{inputs, config};
    return session.map_grid();
}

// ---------------------------------------------------------------------------
// reporting
// ---------------------------------------------------------------------------

std::string TransferRecord::describe() const {
    std::ostringstream os;
    os << std::setprecision(10);
    os << "result            : " << (success ? "SUCCESS" : "FAILURE") << "  ("
       << to_string(failure) << ")\n";
    if (!detail.empty()) {
        os << "detail            : " << detail << "\n";
    }
    os << "execution         : " << to_string(execution) << "\n"
       << "lambert solution  : " << lambert_solution_id << "\n"
       << "transfer angle    : " << units::rad_to_deg(transfer_angle_rad) << " deg\n"
       << "time of flight    : " << time_of_flight_s / 86400.0 << " d\n"
       << "departure coast   : " << departure_coast_s / 3600.0 << " h\n"
       << "departure perigee : " << departure_perigee_radius / 1000.0 << " km\n"
       << "departure delta-v : " << departure_delta_v << " m/s (lambert "
       << lambert_delta_v << ", correction " << correction_magnitude << ")\n"
       << "stage 1 residual  : " << position_corrector_residual / 1000.0 << " km after "
       << position_corrector_iterations << " iteration(s)  [" << position_corrector_message
       << "]\n"
       << "stage 2 B-plane   : " << bplane_error / 1000.0 << " km after " << bplane_passes
       << " pass(es)  [" << bplane_message << "]\n"
       << "v_infinity        : " << v_infinity << " m/s\n"
       << "periapsis         : predicted " << predicted_periapsis / 1000.0 << " km, flown "
       << actual_periapsis / 1000.0 << " km\n"
       << "capture delta-v   : needs " << required_capture_delta_v << " m/s, has "
       << available_capture_delta_v << " m/s\n"
       << "burn              : " << burn_duration_s << " s at "
       << burn_offset_from_periapsis_s << " s from periapsis, midpoint r "
       << burn_midpoint_radius / 1000.0 << " km, v " << burn_midpoint_speed << " m/s\n"
       << "specific energy   : before " << energy_before_burn << " J/kg, after "
       << post_burn_specific_energy << " J/kg\n"
       << "orbit             : " << post_burn_periapsis_altitude / 1000.0 << " x "
       << post_burn_apoapsis_altitude / 1000.0 << " km, e = " << post_burn_eccentricity
       << ", i = " << units::rad_to_deg(post_burn_inclination_rad) << " deg, RAAN = "
       << units::rad_to_deg(post_burn_raan_rad) << " deg\n"
       << "autopilot         : lag mean " << units::rad_to_deg(capture_pointing_error_mean_rad)
       << " deg, peak " << units::rad_to_deg(capture_pointing_error_peak_rad)
       << " deg, settled in " << capture_settling_s << " s, RCS "
       << capture_rcs_propellant << " kg at duty " << capture_rcs_duty_cycle
       << ", saturated " << capture_torque_saturation << " of the time\n"
       << "propellant        : used " << propellant_used << " kg, left " << propellant_left
       << " kg\n"
       << "search            : " << candidates_considered << " considered, "
       << candidates_feasible << " feasible, " << candidates_flown << " flown\n"
       << "                    rejected: " << rejected_no_lambert << " no solution, "
       << rejected_transfer_angle << " degenerate angle, " << rejected_departure_conic
       << " departure conic, " << rejected_delta_v << " unaffordable\n"
       << "cost              : " << cost << "\n"
       << "propagations      : " << propagations << ", integrator steps "
       << integrator_steps << ", wall " << wall_time_seconds << " s\n";
    return os.str();
}

std::string TransferRecord::csv_header() {
    return "requested_epoch_tdb_s,departure_epoch_tdb_s,arrival_epoch_tdb_s,time_of_flight_s,"
           "departure_coast_s,"
           "sc_x_m,sc_y_m,sc_z_m,sc_vx_ms,sc_vy_ms,sc_vz_ms,"
           "center_x_m,center_y_m,center_z_m,center_vx_ms,center_vy_ms,center_vz_ms,"
           "moon_dep_x_m,moon_dep_y_m,moon_dep_z_m,moon_dep_vx_ms,moon_dep_vy_ms,moon_dep_vz_ms,"
           "moon_arr_x_m,moon_arr_y_m,moon_arr_z_m,moon_arr_vx_ms,moon_arr_vy_ms,moon_arr_vz_ms,"
           "lambert_solution_id,lambert_branch,transfer_angle_deg,"
           "lambert_vdep_x_ms,lambert_vdep_y_ms,lambert_vdep_z_ms,"
           "lambert_varr_x_ms,lambert_varr_y_ms,lambert_varr_z_ms,"
           "lambert_delta_v_ms,initial_flown_miss_m,departure_perigee_m,departure_conic_ecc,"
           "departure_delta_v_ms,corrector_iterations,corrector_evaluations,"
           "corrector_residual_m,corrector_converged,"
           "bplane_target_t_m,bplane_target_r_m,bplane_actual_t_m,bplane_actual_r_m,"
           "bplane_error_m,bplane_passes,bplane_iterations,bplane_converged,"
           "correction_magnitude_ms,"
           "closest_approach_tdb_s,moon_rel_x_m,moon_rel_y_m,moon_rel_z_m,"
           "moon_rel_vx_ms,moon_rel_vy_ms,moon_rel_vz_ms,"
           "v_infinity_ms,target_periapsis_m,predicted_periapsis_m,actual_periapsis_m,"
           "periapsis_error_m,periapsis_velocity_ms,"
           "required_capture_dv_ms,available_capture_dv_ms,"
           "burn_start_tdb_s,burn_duration_s,burn_end_tdb_s,burn_offset_s,"
           "burn_midpoint_radius_m,burn_midpoint_speed_ms,"
           "capture_pointing_error_mean_deg,capture_pointing_error_peak_deg,"
           "capture_rcs_propellant_kg,capture_rcs_duty_cycle,capture_torque_saturation,"
           "capture_settling_s,capture_angular_rate_peak_rad_s,"
           "energy_before_j_kg,post_burn_energy_j_kg,post_burn_ecc,"
           "post_burn_periapsis_alt_m,post_burn_apoapsis_alt_m,post_burn_inclination_deg,"
           "post_burn_raan_deg,mass_at_departure_kg,mass_at_capture_kg,"
           "execution,result,failure_reason,detail,cost,"
           "propellant_used_kg,propellant_left_kg,"
           "candidates_considered,candidates_feasible,candidates_flown,"
           "rejected_no_lambert,rejected_transfer_angle,rejected_departure_conic,"
           "rejected_delta_v,propagations,integrator_steps,wall_time_s";
}

namespace {

void put(std::ostringstream& os, const Vec3& v) {
    os << v.x << "," << v.y << "," << v.z << ",";
}

void put(std::ostringstream& os, const coordinates::StateVector& s) {
    put(os, s.position);
    put(os, s.velocity);
}

std::string escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char c : text) {
        if (c == '"') {
            out.push_back('"');
        }
        out.push_back(c == '\n' ? ' ' : c);
    }
    out.push_back('"');
    return out;
}

}  // namespace

std::string TransferRecord::csv_row() const {
    std::ostringstream os;
    os << std::setprecision(17);
    os << requested_epoch.seconds_since_j2000() << "," << departure_epoch.seconds_since_j2000()
       << "," << arrival_epoch.seconds_since_j2000() << "," << time_of_flight_s << ","
       << departure_coast_s << ",";
    put(os, spacecraft_initial_state);
    put(os, center_state);
    put(os, target_departure_state);
    put(os, target_arrival_state);
    os << escape(lambert_solution_id) << ","
       << (lambert_direction == trajectory::TransferDirection::Prograde ? "prograde"
                                                                       : "retrograde")
       << "," << units::rad_to_deg(transfer_angle_rad) << ",";
    put(os, lambert_departure_velocity);
    put(os, lambert_arrival_velocity);
    os << lambert_delta_v << "," << initial_flown_miss << "," << departure_perigee_radius << ","
       << departure_conic_eccentricity << "," << departure_delta_v << ","
       << position_corrector_iterations << "," << position_corrector_evaluations << ","
       << position_corrector_residual << "," << (position_corrector_converged ? 1 : 0) << ","
       << bplane_target.b_dot_t << "," << bplane_target.b_dot_r << "," << bplane_actual_t << ","
       << bplane_actual_r << "," << bplane_error << "," << bplane_passes << ","
       << bplane_iterations << "," << (bplane_converged ? 1 : 0) << "," << correction_magnitude
       << "," << closest_approach_epoch.seconds_since_j2000() << ",";
    put(os, target_relative_position);
    put(os, target_relative_velocity);
    os << v_infinity << "," << target_periapsis << "," << predicted_periapsis << ","
       << actual_periapsis << "," << periapsis_error << "," << periapsis_velocity << ","
       << required_capture_delta_v << ","
       << available_capture_delta_v << "," << burn_start.seconds_since_j2000() << ","
       << burn_duration_s << "," << burn_end.seconds_since_j2000() << ","
       << burn_offset_from_periapsis_s << "," << burn_midpoint_radius << ","
       << burn_midpoint_speed << "," << units::rad_to_deg(capture_pointing_error_mean_rad)
       << "," << units::rad_to_deg(capture_pointing_error_peak_rad)
       << "," << capture_rcs_propellant << "," << capture_rcs_duty_cycle << ","
       << capture_torque_saturation << "," << capture_settling_s << ","
       << capture_angular_rate_peak
       << "," << energy_before_burn << "," << post_burn_specific_energy
       << "," << post_burn_eccentricity << "," << post_burn_periapsis_altitude << ","
       << post_burn_apoapsis_altitude << "," << units::rad_to_deg(post_burn_inclination_rad)
       << "," << units::rad_to_deg(post_burn_raan_rad) << "," << mass_at_departure << ","
       << mass_at_capture
       << "," << to_string(execution) << "," << (success ? "SUCCESS" : "FAILURE") << ","
       << to_string(failure) << "," << escape(detail) << "," << cost << "," << propellant_used
       << "," << propellant_left << "," << candidates_considered << "," << candidates_feasible
       << "," << candidates_flown << "," << rejected_no_lambert << ","
       << rejected_transfer_angle << "," << rejected_departure_conic << "," << rejected_delta_v
       << "," << propagations << "," << integrator_steps << "," << wall_time_seconds;
    return os.str();
}

}  // namespace sf::navigation
