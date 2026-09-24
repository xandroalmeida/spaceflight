#include "core/navigation/mission_planner.hpp"

#include "core/coordinates/reference_frame.hpp"
#include "core/navigation/direct_transfer.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sf::navigation {

std::string_view to_string(OptimizationObjective objective) {
    switch (objective) {
        case OptimizationObjective::Balanced:               return "BALANCED";
        case OptimizationObjective::MinimumTotalDeltaV:     return "MINIMUM_TOTAL_DV";
        case OptimizationObjective::MinimumInjectionDeltaV: return "MINIMUM_INJECTION_DV";
        case OptimizationObjective::ShortestTimeOfFlight:   return "SHORTEST_TOF";
    }
    return "UNKNOWN";
}

std::string_view to_string(MissionPlanStatus status) {
    switch (status) {
        case MissionPlanStatus::Planned:                return "PLANNED";
        case MissionPlanStatus::NoSolution:             return "NO_SOLUTION";
        case MissionPlanStatus::InsufficientPropellant: return "INSUFFICIENT_PROPELLANT";
        case MissionPlanStatus::ExecutionFailed:        return "EXECUTION_FAILED";
        case MissionPlanStatus::Invalid:                return "INVALID";
        case MissionPlanStatus::Cancelled:              return "CANCELLED";
    }
    return "UNKNOWN";
}

DurationRange DurationRange::linear(double from_days, double to_days, int samples) {
    DurationRange range{};
    range.days.clear();
    const int count = std::max(1, samples);
    if (count == 1) {
        range.days.push_back(from_days);
        return range;
    }
    for (int i = 0; i < count; ++i) {
        const double fraction = static_cast<double>(i) / static_cast<double>(count - 1);
        range.days.push_back(from_days + fraction * (to_days - from_days));
    }
    return range;
}

SearchSpace default_search_space(const ephemeris::EphemerisProvider& provider,
                                 celestial::BodyId origin, celestial::BodyId destination,
                                 time::CoordinateTime epoch) {
    const auto geometry = geometry_for(origin, destination);
    if (!geometry.has_value()) {
        throw std::invalid_argument("default_search_space: " + origin.name() + " and " +
                                    destination.name() +
                                    " have no common primary in the body directory");
    }

    SearchSpace space{};
    space.geometry = *geometry;

    // One revolution of a low parking orbit, in both geometries and for the same
    // reason: where the ship is when it leaves is what decides which way the
    // departure conic can point. Two hours covers a 400 km orbit's 92 minutes
    // with margin.
    space.departure_window.span = time::Duration::hours(2.0);

    if (space.geometry == TransferGeometry::Local) {
        // EXACTLY the values docs/validation/lunar-navigation-hardening.md
        // qualified, left as constants rather than re-derived. Deriving them
        // would move the grid the 365/365 campaign measured, which is a new
        // campaign and not a tidy-up.
        space.departure_window.samples = 16;
        space.time_of_flight = DurationRange{};
        return space;
    }

    // Interplanetary. The departure point matters more here than it does for a
    // lunar transfer -- the required asymptote can be anywhere on the sky, and
    // only part of a parking orbit can reach it cheaply -- so the window is
    // sampled more finely even though it is the same length.
    space.departure_window.samples = 24;

    // The Hohmann time between the two orbits, as the SCALE of the problem. Not
    // as the answer: rule 39 is explicit that 259 days must not be assumed to be
    // the only option, and the sweep below deliberately runs from a quarter of it
    // to well past it so that the trade-off between flight time and delta-v is
    // something the pilot can see rather than something the planner decided.
    const auto primary = celestial::common_primary(origin, destination);
    const auto frame = coordinates::ReferenceFrame::centered_on(*primary);
    const double gm = provider.gravitational_parameter(*primary);

    const auto semi_major_axis = [&](celestial::BodyId body) {
        const auto state = provider.state(body, epoch, frame);
        return trajectory::elements_from_state(state.state, gm).semi_major_axis;
    };
    const double a1 = semi_major_axis(origin);
    const double a2 = semi_major_axis(destination);

    double hohmann_days = 259.0;
    if (a1 > 0.0 && a2 > 0.0 && gm > 0.0) {
        const double a_transfer = 0.5 * (a1 + a2);
        hohmann_days = units::pi * std::sqrt(a_transfer * a_transfer * a_transfer / gm) / 86400.0;
    }

    // Sixteen samples from a quarter of the Hohmann time to 1.6 times it. The
    // lower end is where the cost curve turns vertical (60 days costs 117 km/s
    // against 52 at 150) and the upper end is past the minimum, so the sweep
    // brackets the trade-off instead of sitting on one side of it.
    space.time_of_flight = DurationRange::linear(0.25 * hohmann_days, 1.6 * hohmann_days, 16);
    return space;
}

namespace {

// The weights each objective means (section 14).
//
// Only Balanced is qualified.  The other three exist because the question they
// answer is a real one, and each one carries the measurement that says what it
// costs -- see the note on TransferCost in transfer_planner.hpp.
TransferCost cost_for(OptimizationObjective objective) {
    TransferCost cost{};
    switch (objective) {
        case OptimizationObjective::Balanced:
            return cost;
        case OptimizationObjective::MinimumTotalDeltaV:
            // Correction effort almost unpriced.  Measured on this codebase
            // before the weights existed: the cheapest candidate saved 150 m/s a
            // ship with kilometres per second of budget does not need, and left
            // the corrector stalled at 53 000 km.  Kept as an option, not a
            // default, and the ablation is in
            // docs/validation/lunar-navigation-hardening.md.
            cost.correction_magnitude = 0.25;
            cost.periapsis_error = 0.01;
            return cost;
        case OptimizationObjective::MinimumInjectionDeltaV:
            cost.insertion_delta_v = 0.0;
            cost.correction_magnitude = 0.25;
            return cost;
        case OptimizationObjective::ShortestTimeOfFlight:
            // 200 per day against delta-v in m/s: a quarter of a day has to be
            // worth about 50 m/s before it wins.  A tie-break, not an override.
            cost.time_of_flight = 200.0;
            return cost;
    }
    return cost;
}

[[nodiscard]] MissionPlanStatus status_for(const TransferRecord& record) {
    if (record.success) {
        return MissionPlanStatus::Planned;
    }
    switch (record.failure) {
        case TransferFailure::InsufficientCaptureDeltaV:
        case TransferFailure::InsufficientDepartureDeltaV:
            return MissionPlanStatus::InsufficientPropellant;

        // The trajectory was found and the FLIGHT of it did not end where the
        // plan said.  Distinguishing these from "no trajectory exists" is what
        // section 19 needs in order to say PLANNER FAILURE against AUTOPILOT
        // FAILURE against PROPULSION FAILURE.
        case TransferFailure::TargetImpact:
        case TransferFailure::PeriapsisTooHigh:
        case TransferFailure::PeriapsisTooLow:
        case TransferFailure::PostBurnHyperbolic:
        case TransferFailure::TargetOrbitNotAchieved:
        case TransferFailure::CaptureBurnTooEarly:
        case TransferFailure::CaptureBurnTooLate:
        case TransferFailure::DepartureConicHitsCentralBody:
            return MissionPlanStatus::ExecutionFailed;

        case TransferFailure::Cancelled:
            return MissionPlanStatus::Cancelled;

        default:
            return MissionPlanStatus::NoSolution;
    }
}

MissionAlternative alternative_from(const TransferRecord& record) {
    MissionAlternative alternative{};
    alternative.label = record.lambert_solution_id;
    alternative.feasible = record.success;
    alternative.departure = record.departure_epoch;
    alternative.time_of_flight_s = record.time_of_flight_s;
    alternative.departure_coast_s = record.departure_coast_s;
    alternative.branch = record.lambert_direction;
    alternative.injection_delta_v = record.departure_delta_v;
    alternative.capture_delta_v = record.required_capture_delta_v;
    alternative.total_delta_v = record.departure_delta_v + record.required_capture_delta_v;
    alternative.cost = record.cost;
    alternative.predicted_periapsis_altitude = record.post_burn_periapsis_altitude;
    alternative.predicted_apoapsis_altitude = record.post_burn_apoapsis_altitude;
    alternative.predicted_eccentricity = record.post_burn_eccentricity;
    alternative.predicted_inclination = units::Angle::radians(record.post_burn_inclination_rad);
    alternative.predicted_raan = units::Angle::radians(record.post_burn_raan_rad);
    alternative.failure = record.failure;
    return alternative;
}

// How long each named burn runs, read off the plan rather than recomputed.
double duration_of(const ManeuverPlan& plan, std::string_view name) {
    for (const auto& maneuver : plan.maneuvers()) {
        if (maneuver.name == name) {
            return maneuver.duration.seconds();
        }
    }
    return 0.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// the translation
// ---------------------------------------------------------------------------

TransferConfig config_for(const MissionRequest& request) {
    TransferConfig config{};

    config.departure_window = request.departure_window.span;
    config.departure_samples = request.departure_window.samples;
    config.time_of_flight_days = request.time_of_flight.days;

    // The flyby is aimed at the periapsis the mission asked for.  Not a separate
    // knob: aiming the flyby somewhere other than where the ship is meant to end
    // up is how a "100 km orbit" arrives at 300 km and nobody can say which
    // number was wrong.
    config.flyby_altitude = request.target_orbit.periapsis_altitude;
    config.b_plane_angle = request.effort.b_plane_angle;

    const auto geometry =
        geometry_for(request.origin, request.destination).value_or(TransferGeometry::Local);

    // How precisely the flyby has to be AIMED, in metres of periapsis.
    //
    // 2 km for a lunar transfer, which is what the campaign qualified, and 50 km
    // for an interplanetary one -- and the reason is arithmetic rather than
    // ambition.
    //
    // The B-plane corrector estimates its Jacobian by moving the departure
    // velocity by 1e-3 m/s. Over a four-day trans-lunar arc that probe moves the
    // arrival by about 1 km, so a 2 km tolerance is something it can see. Over a
    // two-hundred-day arc the lever arm is about 3.4e7 m per m/s, so the SAME
    // probe moves the arrival by 34 kilometres: asking for 2 km would be asking
    // the corrector to resolve a sixteenth of its own finite-difference step. It
    // does not converge, it grinds -- measured at more than ten minutes per
    // attempt before this line existed.
    //
    // What makes 50 km acceptable is that for an interplanetary capture the aim
    // is no longer what decides the final orbit. The two capture burns are SOLVED
    // against the flown trajectory -- one for the apoapsis, one for the periapsis
    // -- so a flyby that comes in 50 km high still ends in the orbit that was
    // asked for. The aim only has to be good enough to keep the intermediate
    // ellipse above the surface, and 50 km against a 1000 km droop is well inside
    // that.
    config.periapsis_tolerance = request.effort.periapsis_tolerance;
    if (geometry == TransferGeometry::Interplanetary &&
        request.effort.periapsis_tolerance <= 2.0e3) {
        config.periapsis_tolerance = 50.0e3;
    }

    // Two B-plane passes instead of four, for the same reason and with the same
    // caveat: the outer loop exists to prove that v_infinity barely moves when
    // the aim does, and each extra pass costs a full correction over an arc fifty
    // times longer than a lunar one.
    config.b_plane_passes =
        geometry == TransferGeometry::Interplanetary
            ? std::min(request.effort.b_plane_passes, 2)
            : request.effort.b_plane_passes;

    // The band the ACHIEVED flyby periapsis has to land in before the capture
    // burn is even attempted -- outside it the case is PERIAPSIS_TOO_HIGH or
    // PERIAPSIS_TOO_LOW rather than a near miss.
    //
    // It has to be relative to what was ASKED for.  TransferConfig's own defaults
    // are 20 to 400 km, which are the right numbers for the 100 km lunar orbit
    // the campaign qualified and are silently the wrong ones for anything else:
    // Milestone 8's first Earth-Mars plan aimed a 500 km Mars periapsis, hit it
    // to within 446 metres, and was then classified PERIAPSIS_TOO_HIGH because
    // 500 is more than 400.
    //
    // The OFFSETS are what the lunar defaults actually encode -- 80 km below the
    // request and 300 km above it -- so a 100 km request still produces exactly
    // 20 to 400 and the campaign's classification does not move by a metre.
    config.minimum_periapsis_altitude =
        std::max(0.0, request.target_orbit.periapsis_altitude - 80.0e3);
    config.maximum_periapsis_altitude = request.target_orbit.periapsis_altitude + 300.0e3;

    config.target_orbit.mean_altitude =
        0.5 * (request.target_orbit.periapsis_altitude + request.target_orbit.apoapsis_altitude);
    config.target_orbit.max_eccentricity = request.target_orbit.maximum_eccentricity;
    config.target_orbit.min_periapsis_altitude = request.target_orbit.minimum_periapsis_altitude;
    config.target_orbit.max_apoapsis_altitude = request.target_orbit.maximum_apoapsis_altitude;

    config.cost = cost_for(request.objective);

    config.minimum_departure_perigee_altitude =
        request.effort.minimum_departure_perigee_altitude;
    config.refuse_departure_conic_below_floor =
        request.effort.refuse_departure_conic_below_floor;
    config.screened_candidates = request.effort.screened_candidates;
    config.flown_candidates = request.effort.flown_candidates;
    config.step_budget = request.effort.budget_for(
        request.spacecraft.execution,
        geometry_for(request.origin, request.destination).value_or(TransferGeometry::Local));
    config.approach_bracket_samples = request.effort.approach_bracket_samples;
    config.capture_burn_offset_seconds = request.effort.capture_burn_offset_seconds;
    config.settling_threshold = request.effort.settling_threshold;

    config.execution = request.spacecraft.execution;
    config.autopilot_hull_mass = request.spacecraft.hull_mass;
    config.autopilot_hull_size = request.spacecraft.hull_size;
    config.autopilot_rcs_arm = request.spacecraft.rcs_arm;
    config.autopilot_rcs_mass_flow = request.spacecraft.rcs_mass_flow;
    config.autopilot_rcs_exhaust_fraction_c = request.spacecraft.rcs_exhaust_fraction_c;
    config.autopilot_gains = request.spacecraft.pointing;

    config.cancelled = request.effort.cancelled;
    config.on_progress = request.effort.on_progress;
    config.pinned = request.pinned;
    config.keep_trajectory = request.want_trajectory;
    return config;
}

TransferInputs inputs_for(const SimulationState& state, const MissionRequest& request) {
    TransferInputs inputs{};
    inputs.provider = state.provider;
    inputs.orientation = state.orientation;
    inputs.craft = request.spacecraft.vehicle;
    inputs.catalog = state.catalog;
    inputs.j2_bodies = state.j2_bodies;
    inputs.center = request.origin;
    inputs.target = request.destination;
    inputs.parking = state.vehicle;
    inputs.epoch = state.epoch;
    inputs.mass = state.mass;
    inputs.integrator = state.integrator;
    return inputs;
}

// ---------------------------------------------------------------------------

MissionPlanResult plan_mission(const SimulationState& state,
                                      const MissionRequest& request) {
    if (state.provider == nullptr) {
        throw std::invalid_argument(
            "plan_mission: no ephemeris provider; this is not a question about the "
            "Solar System");
    }
    if (request.spacecraft.vehicle == nullptr) {
        throw std::invalid_argument(
            "plan_mission: no spacecraft; a transfer is a property of a ship as much as "
            "of a geometry");
    }
    if (request.kind == TransferKind::Direct) {
        return plan_direct_mission(state, request);
    }
    if (request.time_of_flight.days.empty()) {
        throw std::invalid_argument(
            "plan_mission: the time-of-flight grid is empty; a search with nothing to "
            "search is not a search");
    }

    const auto inputs = inputs_for(state, request);
    auto config = config_for(request);

    MissionPlanResult result{};
    config.on_attempt = [&result](const TransferRecord& attempt) {
        result.alternatives.push_back(alternative_from(attempt));
    };

    const TransferRecord record = plan_and_fly(inputs, config);
    result.diagnostics = record;
    result.status = status_for(record);

    if (!record.success) {
        result.failure = FailureReason{record.failure, record.detail};
        // No maneuvers on a failure, ever.  The GDExtension's old planner wrote
        // trial burns into the ship's live plan as it searched and relied on the
        // caller to clear them; a plan that failed therefore left an armed burn
        // behind, and the ship would fly it four simulated days from a target it
        // was never going to reach.  Here there is nothing to clear.
        return result;
    }

    result.maneuvers = record.flight_plan;

    auto& metrics = result.metrics;
    metrics.origin = request.origin;
    metrics.destination = request.destination;
    metrics.mass_at_departure = record.mass_at_departure;
    metrics.departure = record.departure_epoch;
    metrics.arrival = record.closest_approach_epoch;
    metrics.capture_ignition = record.burn_start;
    metrics.capture_cutoff = record.burn_end;
    metrics.time_of_flight_s = record.time_of_flight_s;
    metrics.branch = record.lambert_direction;
    metrics.transfer_angle = units::Angle::radians(record.transfer_angle_rad);

    metrics.injection_delta_v = record.departure_delta_v;
    metrics.midcourse_delta_v = record.correction_magnitude;
    metrics.capture_delta_v = record.required_capture_delta_v;
    // Injection plus capture, and NOT plus the correction: the corrector's
    // authority is already inside the injection it produced.  Adding it again
    // would double-count the largest single number on the readout.
    metrics.total_delta_v = record.departure_delta_v + record.required_capture_delta_v;

    metrics.injection_duration_s = duration_of(record.flight_plan, "injection");
    metrics.capture_duration_s = record.burn_duration_s;

    metrics.v_infinity = record.v_infinity;
    metrics.b_plane_target = record.bplane_target;
    metrics.predicted_flyby_periapsis = record.actual_periapsis;

    metrics.requested_periapsis_altitude = request.target_orbit.periapsis_altitude;
    metrics.requested_apoapsis_altitude = request.target_orbit.apoapsis_altitude;
    metrics.predicted_periapsis_altitude = record.post_burn_periapsis_altitude;
    metrics.predicted_apoapsis_altitude = record.post_burn_apoapsis_altitude;
    metrics.predicted_eccentricity = record.post_burn_eccentricity;
    metrics.predicted_inclination = units::Angle::radians(record.post_burn_inclination_rad);
    metrics.predicted_raan = units::Angle::radians(record.post_burn_raan_rad);

    metrics.propellant_required = record.propellant_used;
    metrics.propellant_remaining = record.propellant_left;
    metrics.delta_v_available = request.spacecraft.vehicle->delta_v_budget(
        record.mass_at_departure > 0.0 ? record.mass_at_departure
                                       : request.spacecraft.vehicle->initial_mass());

    metrics.pointing_error_mean =
        units::Angle::radians(record.capture_pointing_error_mean_rad);
    metrics.pointing_error_peak =
        units::Angle::radians(record.capture_pointing_error_peak_rad);
    metrics.rcs_propellant = record.capture_rcs_propellant;
    metrics.rcs_duty_cycle = record.capture_rcs_duty_cycle;
    metrics.torque_saturation = record.capture_torque_saturation;
    metrics.settling_s = record.capture_settling_s;
    metrics.angular_rate_peak = record.capture_angular_rate_peak;

    if (request.want_trajectory && record.trajectory != nullptr &&
        !record.trajectory->empty()) {
        const auto frame = coordinates::ReferenceFrame::ssb_j2000();
        const auto samples =
            record.trajectory->sample(static_cast<std::size_t>(
                std::max(2, request.trajectory_samples)));
        result.trajectory.samples.reserve(samples.size());
        for (const auto& [t, sampled] : samples) {
            const auto origin = state.provider->state(request.origin, t, frame);
            const auto destination = state.provider->state(request.destination, t, frame);
            result.trajectory.samples.push_back(TrajectoryPrediction::Sample{
                t, sampled.state.position - origin.state.position,
                sampled.state.position - destination.state.position});
        }
    }

    return result;
}

}  // namespace sf::navigation
