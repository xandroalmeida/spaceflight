#include "core/navigation/lunar_transfer.hpp"

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
        case TransferFailure::LunarImpact:                 return "LUNAR_IMPACT";
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
        case TransferFailure::TargetOrbitNotAchieved:      return "TARGET_ORBIT_NOT_ACHIEVED";
    }
    return "UNCLASSIFIED";
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

double TransferCost::evaluate(double departure_dv, double insertion_dv, double periapsis_error_m,
                              double correction_dv, double conic_deficit_m) const {
    return departure_delta_v * departure_dv + insertion_delta_v * insertion_dv +
           periapsis_error * std::abs(periapsis_error_m) +
           correction_magnitude * std::abs(correction_dv) +
           departure_conic_deficit * std::max(0.0, conic_deficit_m);
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
    double pointing_error_mean{0.0};   // [rad]; AUTOPILOT only
    double pointing_error_peak{0.0};   // [rad]; AUTOPILOT only
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
    TransferSession(const TransferInputs& inputs, const LunarTransferConfig& config)
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
        gm_center_ = inputs_.provider->gravitational_parameter(inputs_.center);
        gm_target_ = inputs_.provider->gravitational_parameter(inputs_.target);
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
        double insertion_estimate{0.0};
        double proxy_cost{0.0};
        double flown_miss{0.0};
        double selection_score{0.0};
        std::string id;
    };

    struct Rejections {
        int considered{0};
        int no_lambert{0};
        int transfer_angle{0};
        int departure_conic{0};
        int delta_v{0};
    };

    [[nodiscard]] TransferRecord run();
    [[nodiscard]] std::vector<GridCell> map_grid();

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
                                             const std::optional<InsertionBurn>& capture,
                                             time::CoordinateTime capture_at,
                                             double mass_at_capture) {
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

            if (capture.has_value() && capture->delta_v > 0.0 && mass_at_capture > 0.0) {
                plan.add(maneuver_for_delta_v(*inputs_.craft, mass_at_capture, capture->delta_v,
                                              capture_at, GuidanceMode::Hull, inputs_.target,
                                              1.0, "insertion",
                                              BurnCentering::CenterOnIgnition));
            }
        } catch (const std::exception& e) {
            out.ok = false;
            out.status = propagation::PropagationStatus::OutOfPropellant;
            out.message = e.what();
            return out;
        }

        const auto inertia = attitude::InertiaTensor::solid_box(config_.autopilot_hull_mass,
                                                                config_.autopilot_hull_size);
        const propulsion::EngineSpec thruster{"RCS", config_.autopilot_rcs_mass_flow,
                                              config_.autopilot_rcs_exhaust_fraction_c, 1.0};
        const auto rcs = attitude::RcsSystem::couples(config_.autopilot_rcs_arm, thruster);
        attitude::PointingController controller{*inputs_.provider, inertia, ssb_,
                                                config_.autopilot_gains};
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
            if (insertion != nullptr) {
                propagator.set_step_observer([&](const propagation::StepInfo& step) {
                    if (!step.accepted || !insertion->active_at(step.time)) {
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
            out.pointing_error_mean = error_weight > 0.0 ? error_sum / error_weight : 0.0;
            out.pointing_error_peak = error_peak;
        }
        out.propellant_used = candidate.state.mass - out.state.mass;
        return out;
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
    [[nodiscard]] std::optional<Candidate> screen(time::CoordinateTime t_depart,
                                                  const propagation::PropagationState& state,
                                                  const ephemeris::BodyState& center,
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
            solution = trajectory::solve_lambert(r1, r2, tof, gm_center_, direction);
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
        candidate.departure_velocity = center.state.velocity + solution.departure_velocity;
        candidate.delta_v = (candidate.departure_velocity - state.state.velocity).norm();

        // THE constraint.  The post-injection conic about the central body, as a
        // two-body orbit: if its periapsis is below the surface, the spacecraft
        // flies into the planet it just left, minutes after ignition.  46 of the
        // 59 Milestone 6 failures are exactly this, and every one of them was
        // preceded by a B-plane stage reporting `converged`.
        const coordinates::StateVector departure_conic{r1, solution.departure_velocity};
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
            if (config_.refuse_departure_conic_below_floor) {
                return std::nullopt;
            }
        }

        // What the arrival costs, as far as two-body arithmetic can see it from
        // here: v_infinity relative to the target, and the periapsis burn it
        // implies.
        const auto target_arrival = inputs_.provider->state(inputs_.target, t_depart + tof,
                                                            centered_);
        candidate.v_infinity_estimate =
            (solution.arrival_velocity - target_arrival.state.velocity).norm();
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
        candidate.proxy_cost =
            config_.cost.evaluate(candidate.delta_v, candidate.insertion_estimate, 0.0, 0.0,
                                  candidate.departure_conic_deficit);
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
            const Vec3 r1 = state.state.position - center.state.position;
            const auto tof = time::Duration::days(config_.pinned.time_of_flight_days);
            const Vec3 r2 = inputs_.provider->state(inputs_.target, t_depart + tof, centered_)
                                .state.position;
            ++rejections.considered;
            auto candidate = screen(t_depart, state, center, r1, r2, tof,
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
            const Vec3 r1 = state.state.position - center.state.position;
            for (const double tof_days : config_.time_of_flight_days) {
                const auto tof = time::Duration::days(tof_days);
                const Vec3 r2 = inputs_.provider
                                    ->state(inputs_.target, t_depart + tof, centered_)
                                    .state.position;
                for (const auto direction : directions) {
                    ++rejections.considered;
                    auto candidate =
                        screen(t_depart, state, center, r1, r2, tof, direction, rejections);
                    if (candidate.has_value()) {
                        candidates.push_back(std::move(*candidate));
                    }
                }
            }
        }
        return candidates;
    }

    // ---- flying -----------------------------------------------------------

    [[nodiscard]] FlightResult fly(const Candidate& candidate, const Vec3& departure_velocity,
                                   time::CoordinateTime until, propagation::Trajectory* arc,
                                   bool stop_on_impact,
                                   const std::optional<InsertionBurn>& capture = std::nullopt,
                                   time::CoordinateTime capture_at = {},
                                   double mass_at_capture = 0.0) {
        if (config_.execution == ExecutionModel::Impulsive) {
            return fly_impulsive(candidate, departure_velocity, until, arc, stop_on_impact,
                                 capture, capture_at);
        }
        if (config_.execution == ExecutionModel::Autopilot) {
            return fly_autopilot(candidate, departure_velocity, until, arc, stop_on_impact,
                                 capture, capture_at, mass_at_capture);
        }
        return fly_finite(candidate, departure_velocity, until, arc, stop_on_impact, capture,
                          capture_at, mass_at_capture);
    }

    // Impulsive: the velocity is SET, the mass is debited by the rocket equation,
    // and no engine runs.  This is the trajectory with no control error in it at
    // all, and it is the reference the other two are measured against
    // (section 11 of the brief).
    [[nodiscard]] FlightResult fly_impulsive(const Candidate& candidate,
                                             const Vec3& departure_velocity,
                                             time::CoordinateTime until,
                                             propagation::Trajectory* arc, bool stop_on_impact,
                                             const std::optional<InsertionBurn>& capture,
                                             time::CoordinateTime capture_at) {
        FlightResult out{};
        auto forces = build_forces(nullptr);
        propagation::DormandPrince54Propagator propagator{*forces,
                                                          integrator_for(stop_on_impact)};

        auto state = candidate.state;
        const double mass_at_departure = state.mass;
        const double injection_dv = (departure_velocity - state.state.velocity).norm();
        state.mass -= propellant_for(state.mass, injection_dv);
        state.state.velocity = departure_velocity;

        std::vector<time::CoordinateTime> breaks;
        const bool has_capture =
            capture.has_value() && capture_at > candidate.departure && capture_at < until;
        if (has_capture) {
            breaks.push_back(capture_at);
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
            if (has_capture && i + 1 < breaks.size()) {
                const auto body = inputs_.provider->state(inputs_.target, t, ssb_);
                const Vec3 relative = state.state.velocity - body.state.velocity;
                if (relative.norm() > 0.0) {
                    state.mass -= propellant_for(state.mass, capture->delta_v);
                    state.state.velocity -= relative.normalized() * capture->delta_v;
                }
                out.capture_ignition = t;
                out.capture_duration = 0.0;
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
                                          const std::optional<InsertionBurn>& capture,
                                          time::CoordinateTime capture_at,
                                          double mass_at_capture) {
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

            if (capture.has_value() && capture->delta_v > 0.0 && mass_at_capture > 0.0) {
                plan.add(maneuver_for_delta_v(*inputs_.craft, mass_at_capture, capture->delta_v,
                                              capture_at, GuidanceMode::Retrograde, inputs_.target,
                                              1.0, "insertion",
                                              BurnCentering::CenterOnIgnition));
            }
        } catch (const std::exception& e) {
            // The rocket equation refused: the ship cannot pay for this plan.
            out.ok = false;
            out.status = propagation::PropagationStatus::OutOfPropellant;
            out.message = e.what();
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
            }
        }
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
        const auto target_centered =
            inputs_.provider->state(inputs_.target, t_arrive, centered_);
        const Vec3 v_infinity = candidate.solution.arrival_velocity -
                                target_centered.state.velocity;
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
                    return TransferFailure::LunarImpact;
                }
                if (radius_center_ > 0.0 && to_center <= radius_center_ * 1.001) {
                    return TransferFailure::DepartureConicHitsCentralBody;
                }
                return TransferFailure::LunarImpact;
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
    const LunarTransferConfig& config_;
    coordinates::ReferenceFrame ssb_;
    coordinates::ReferenceFrame centered_;
    double gm_center_{0.0};
    double gm_target_{0.0};
    double radius_center_{0.0};
    double radius_target_{0.0};
    double wanted_periapsis_{0.0};
    propagation::Trajectory coast_{};
    std::string coast_message_;
    std::size_t steps_{0};
    std::size_t propagations_{0};
};

// ---------------------------------------------------------------------------

TransferRecord TransferSession::attempt(const Candidate& candidate) {
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
    const auto t_probe_end =
        t_arrive + time::Duration::seconds(candidate.tof_s * 0.25);

    // ---- stage 1: reach the flyby ----------------------------------------
    const Vec3 target_aim = aim_point(candidate, t_arrive);
    auto stage_one_config = config_.departure_targeting;
    const auto reach = correct_departure(
        [&](const Vec3& v) {
            return fly(candidate, v, t_arrive, nullptr, false).state.state.position;
        },
        candidate.departure_velocity, target_aim, stage_one_config);

    record.position_corrector_iterations = reach.iterations;
    record.position_corrector_evaluations = reach.evaluations;
    record.position_corrector_residual = reach.miss_distance;
    record.position_corrector_converged = reach.converged;
    record.position_corrector_message = reach.message;

    Vec3 departure_velocity = reach.departure_velocity;

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
    if (periapsis_altitude < config_.minimum_periapsis_altitude) {
        record.failure = periapsis_altitude <= 0.0 ? TransferFailure::LunarImpact
                                                   : TransferFailure::PeriapsisTooLow;
        record.detail = "flown periapsis altitude " + fixed(periapsis_altitude / 1000.0, 3) +
                        " km";
        return record;
    }
    if (periapsis_altitude > config_.maximum_periapsis_altitude) {
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
    record.required_capture_delta_v = burn.delta_v;

    // Where the burn sits relative to periapsis (section 12).  Zero is centred
    // on it; the campaign tool sweeps this.
    const auto capture_at =
        approach.time + time::Duration::seconds(config_.capture_burn_offset_seconds);
    record.burn_offset_from_periapsis_s = config_.capture_burn_offset_seconds;

    // The mass at the burn is not known until the coast has been flown, and
    // using the departure mass would ask the engine for a duration wrong by the
    // whole injection's propellant.  So: fly the coast once with no capture,
    // read the mass there, then plan the burn.
    const auto coast_to_burn = fly(candidate, departure_velocity, capture_at, nullptr, true);
    if (!coast_to_burn.ok) {
        record.failure = classify(coast_to_burn);
        record.detail = coast_to_burn.message;
        return record;
    }
    const double mass_at_capture = coast_to_burn.state.mass;
    record.available_capture_delta_v = inputs_.craft->delta_v_budget(mass_at_capture);
    if (record.required_capture_delta_v > record.available_capture_delta_v) {
        record.failure = TransferFailure::InsufficientCaptureDeltaV;
        record.detail = "needs " + fixed(record.required_capture_delta_v, 3) + " m/s, has " +
                        fixed(record.available_capture_delta_v, 3) + " m/s";
        return record;
    }

    // Two orbits past the burn, so the result is an ORBIT and not a lucky
    // instant: the elements are read after a full revolution.
    const auto t_end = capture_at + time::Duration::seconds(burn.period * 2.0);
    propagation::Trajectory captured_arc;
    const auto captured =
        fly(candidate, departure_velocity, t_end, config_.keep_trajectory ? &captured_arc : nullptr,
            true, burn, capture_at, mass_at_capture);
    if (!captured.ok) {
        record.failure = classify(captured);
        record.detail = captured.message;
        return record;
    }

    record.burn_start = captured.capture_ignition;
    record.burn_duration_s = captured.capture_duration;
    record.capture_pointing_error_mean_rad = captured.pointing_error_mean;
    record.capture_pointing_error_peak_rad = captured.pointing_error_peak;
    record.burn_end = captured.capture_ignition +
                      time::Duration::seconds(captured.capture_duration);
    record.propellant_used = captured.propellant_used;
    record.propellant_left = captured.state.mass - inputs_.craft->dry_mass();

    // Where the burn actually happened, which is what the Oberth question of
    // section 12 asks: the radius and speed at its MIDPOINT, not at its start.
    {
        const auto midpoint = captured.capture_ignition +
                              time::Duration::seconds(0.5 * captured.capture_duration);
        const auto probe = fly(candidate, departure_velocity, midpoint, nullptr, true, burn,
                               capture_at, mass_at_capture);
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

    record.success = true;
    record.failure = TransferFailure::None;
    record.cost = config_.cost.evaluate(record.departure_delta_v,
                                        record.required_capture_delta_v,
                                        record.actual_periapsis - wanted_periapsis_,
                                        record.correction_magnitude);
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
        record.failure = TransferFailure::NoFeasibleTrajectory;
        std::ostringstream os;
        os << rejections.considered << " geometries considered: " << rejections.no_lambert
           << " with no Lambert solution, " << rejections.transfer_angle
           << " in the degenerate transfer-angle band, " << rejections.departure_conic
           << " whose post-injection conic re-enters the central body, " << rejections.delta_v
           << " the ship cannot pay for";
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
        const auto t_arrive = candidate.departure + time::Duration::seconds(candidate.tof_s);
        const auto flight = fly(candidate, candidate.departure_velocity, t_arrive, nullptr, false);
        const auto target = inputs_.provider->state(inputs_.target, flight.time, ssb_);
        candidate.flown_miss =
            flight.ok ? (flight.state.state.position - target.state.position).norm()
                      : std::numeric_limits<double>::infinity();
        const double predicted_correction =
            candidate.flown_miss / std::max(config_.arrival_lever_arm, 1.0);
        candidate.selection_score =
            config_.cost.evaluate(candidate.delta_v, candidate.insertion_estimate, 0.0,
                                  predicted_correction, candidate.departure_conic_deficit);
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
        const Vec3 r1 = state.state.position - center.state.position;
        for (const double tof_days : config_.time_of_flight_days) {
            const auto tof = time::Duration::days(tof_days);
            const Vec3 r2 =
                inputs_.provider->state(inputs_.target, t_depart + tof, centered_).state.position;
            for (const auto direction : directions) {
                GridCell cell{};
                cell.departure_coast_s = (t_depart - inputs_.epoch).seconds();
                cell.time_of_flight_days = tof_days;
                cell.direction = direction;
                Rejections ignored{};
                (void)screen(t_depart, state, center, r1, r2, tof, direction, ignored, &cell);
                grid.push_back(cell);
            }
        }
    }
    return grid;
}

}  // namespace

// ---------------------------------------------------------------------------

TransferRecord plan_and_fly(const TransferInputs& inputs, const LunarTransferConfig& config) {
    TransferSession session{inputs, config};
    return session.run();
}

std::vector<GridCell> map_transfer_grid(const TransferInputs& inputs,
                                        const LunarTransferConfig& config) {
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
       << ", i = " << units::rad_to_deg(post_burn_inclination_rad) << " deg\n"
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
           "energy_before_j_kg,post_burn_energy_j_kg,post_burn_ecc,"
           "post_burn_periapsis_alt_m,post_burn_apoapsis_alt_m,post_burn_inclination_deg,"
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
       << "," << energy_before_burn << "," << post_burn_specific_energy
       << "," << post_burn_eccentricity << "," << post_burn_periapsis_altitude << ","
       << post_burn_apoapsis_altitude << "," << units::rad_to_deg(post_burn_inclination_rad)
       << "," << to_string(execution) << "," << (success ? "SUCCESS" : "FAILURE") << ","
       << to_string(failure) << "," << escape(detail) << "," << cost << "," << propellant_used
       << "," << propellant_left << "," << candidates_considered << "," << candidates_feasible
       << "," << candidates_flown << "," << rejected_no_lambert << ","
       << rejected_transfer_angle << "," << rejected_departure_conic << "," << rejected_delta_v
       << "," << propagations << "," << integrator_steps << "," << wall_time_seconds;
    return os.str();
}

}  // namespace sf::navigation
