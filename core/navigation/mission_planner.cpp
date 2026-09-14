#include "core/navigation/mission_planner.hpp"

#include "core/coordinates/reference_frame.hpp"
#include "core/trajectory/orbital_elements.hpp"

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

namespace {

// The weights each objective means (section 14).
//
// Only Balanced is qualified.  The other three exist because the question they
// answer is a real one, and each one carries the measurement that says what it
// costs -- see the note on TransferCost in lunar_transfer.hpp.
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
        case TransferFailure::LunarImpact:
        case TransferFailure::PeriapsisTooHigh:
        case TransferFailure::PeriapsisTooLow:
        case TransferFailure::PostBurnHyperbolic:
        case TransferFailure::TargetOrbitNotAchieved:
        case TransferFailure::CaptureBurnTooEarly:
        case TransferFailure::CaptureBurnTooLate:
        case TransferFailure::DepartureConicHitsCentralBody:
            return MissionPlanStatus::ExecutionFailed;

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

LunarTransferConfig config_for(const LunarTransferRequest& request) {
    LunarTransferConfig config{};

    config.departure_window = request.departure_window.span;
    config.departure_samples = request.departure_window.samples;
    config.time_of_flight_days = request.time_of_flight.days;

    // The flyby is aimed at the periapsis the mission asked for.  Not a separate
    // knob: aiming the flyby somewhere other than where the ship is meant to end
    // up is how a "100 km orbit" arrives at 300 km and nobody can say which
    // number was wrong.
    config.flyby_altitude = request.target_orbit.periapsis_altitude;
    config.periapsis_tolerance = request.effort.periapsis_tolerance;
    config.b_plane_angle = request.effort.b_plane_angle;

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
    config.b_plane_passes = request.effort.b_plane_passes;
    config.step_budget = request.effort.budget_for(request.spacecraft.execution);
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

    config.pinned = request.pinned;
    config.keep_trajectory = request.want_trajectory;
    return config;
}

TransferInputs inputs_for(const SimulationState& state, const LunarTransferRequest& request) {
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
    inputs.integrator = state.integrator;
    return inputs;
}

// ---------------------------------------------------------------------------

MissionPlanResult plan_lunar_transfer(const SimulationState& state,
                                      const LunarTransferRequest& request) {
    if (state.provider == nullptr) {
        throw std::invalid_argument(
            "plan_lunar_transfer: no ephemeris provider; this is not a question about the "
            "Solar System");
    }
    if (request.spacecraft.vehicle == nullptr) {
        throw std::invalid_argument(
            "plan_lunar_transfer: no spacecraft; a transfer is a property of a ship as much as "
            "of a geometry");
    }
    if (request.time_of_flight.days.empty()) {
        throw std::invalid_argument(
            "plan_lunar_transfer: the time-of-flight grid is empty; a search with nothing to "
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
