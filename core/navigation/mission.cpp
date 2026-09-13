#include "core/navigation/mission.hpp"

#include "core/ephemeris/ephemeris_provider.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace sf::navigation {
namespace {

void accumulate(propagation::IntegratorStats& total, const propagation::IntegratorStats& leg) {
    total.accepted_steps += leg.accepted_steps;
    total.rejected_steps += leg.rejected_steps;
    total.force_evaluations += leg.force_evaluations;
    total.max_error_estimate = std::max(total.max_error_estimate, leg.max_error_estimate);
    total.wall_time_seconds += leg.wall_time_seconds;
    total.max_step_seconds = std::max(total.max_step_seconds, leg.max_step_seconds);
    total.min_step_seconds = total.min_step_seconds == 0.0
                                 ? leg.min_step_seconds
                                 : std::min(total.min_step_seconds, leg.min_step_seconds);
}

}  // namespace

MissionResult run_mission(propagation::DormandPrince54Propagator& propagator,
                          ManeuverExecutor& executor,
                          const propagation::PropagationState& initial,
                          time::CoordinateTime from, time::CoordinateTime to,
                          propagation::Trajectory* trajectory) {
    MissionResult result{};
    result.state = initial;
    result.time = from;

    if (trajectory != nullptr) {
        trajectory->clear();
    }

    const auto& plan = executor.plan();
    const auto& craft = executor.craft();

    // Break points: every ignition and cutoff inside the interval.  Propellant
    // exhaustion is found on the fly, because it depends on the mass at ignition.
    std::vector<time::CoordinateTime> breaks = plan.switch_times(from, to);
    breaks.push_back(to);

    propagation::Trajectory leg_arc;
    propagation::Trajectory* leg_sink = trajectory != nullptr ? &leg_arc : nullptr;
    propagation::Trajectory* previous_recorder = propagator.trajectory_recorder();

    propagation::PropagationState state = initial;
    time::CoordinateTime t = from;

    for (std::size_t i = 0; i < breaks.size(); ++i) {
        time::CoordinateTime target = breaks[i];
        if (target == t) {
            continue;
        }

        // Does the tank run dry inside this leg?  During a leg the throttle is
        // constant, so the exhaustion instant is exact, not searched for.
        const Maneuver* active = plan.active_at(t);
        bool dry_cut = false;
        if (active != nullptr) {
            const double flow = craft.engine().mass_flow_at(active->throttle);
            const double propellant = craft.propellant_at(state.mass);
            if (flow > 0.0) {
                const double seconds_left = propellant / flow;
                if (seconds_left < (target - t).seconds()) {
                    target = t + time::Duration::seconds(seconds_left);
                    dry_cut = true;
                }
            }
        }

        const double mass_before = state.mass;
        const double speed_before =
            active != nullptr ? executor.speed_relative_to(state, t, active->reference) : 0.0;

        // Freeze the force for this leg: constant thrust, or none at all.  This
        // is what keeps the integrator from meeting a discontinuity inside a step.
        executor.arm(active);

        propagator.set_trajectory_recorder(leg_sink);
        const auto leg = propagator.propagate(state, t, target);
        if (leg_sink != nullptr && trajectory != nullptr) {
            for (const auto& segment : leg_arc.segments()) {
                trajectory->append(segment);
            }
        }

        accumulate(result.stats, leg.stats);
        ++result.segments;

        state = leg.state;
        t = leg.time;

        if (active != nullptr) {
            BurnReport report{};
            report.name = active->name;
            report.ignition = active->ignition;
            report.duration = t - active->ignition;
            report.mass_before = mass_before;
            report.mass_after = state.mass;
            report.propellant_used = mass_before - state.mass;
            report.ran_dry = dry_cut;
            report.speed_change =
                executor.speed_relative_to(state, t, active->reference) - speed_before;
            if (report.propellant_used > 0.0 && state.mass > 0.0) {
                report.delta_v_rocket =
                    craft.engine().delta_v_for_mass_ratio(mass_before, state.mass);
            }
            // Merge consecutive legs of the same maneuver (a burn split by a dry
            // cut produces two legs but is one burn).
            if (!result.burns.empty() && result.burns.back().name == report.name &&
                result.burns.back().ignition == report.ignition) {
                auto& previous = result.burns.back();
                previous.duration = report.duration;
                previous.mass_after = report.mass_after;
                previous.propellant_used = previous.mass_before - report.mass_after;
                previous.delta_v_rocket =
                    craft.engine().delta_v_for_mass_ratio(previous.mass_before, report.mass_after);
                previous.speed_change += report.speed_change;
                previous.ran_dry = previous.ran_dry || report.ran_dry;
            } else {
                result.burns.push_back(std::move(report));
            }
        }

        if (!leg.ok()) {
            result.status = leg.status;
            result.message = leg.message;
            break;
        }

        if (dry_cut) {
            result.status = propagation::PropagationStatus::OutOfPropellant;
            result.message = "propellant exhausted during \"" +
                             (active != nullptr ? active->name : std::string{"burn"}) +
                             "\"; the remainder of the plan cannot be executed as written";
            break;
        }
    }

    executor.disarm();
    propagator.set_trajectory_recorder(previous_recorder);

    result.state = state;
    result.time = t;
    // The per-leg means cannot simply be averaged; recompute over the whole run.
    result.stats.mean_step_seconds =
        result.stats.accepted_steps > 0
            ? std::abs((t - from).seconds()) / static_cast<double>(result.stats.accepted_steps)
            : 0.0;
    return result;
}

std::string MissionResult::describe_burns() const {
    if (burns.empty()) {
        return "  (no burns executed)";
    }
    std::ostringstream os;
    os << std::setprecision(10);
    for (std::size_t i = 0; i < burns.size(); ++i) {
        const auto& b = burns[i];
        os << (i > 0 ? "\n" : "") << "  " << b.name << ": " << b.duration.seconds() << " s, "
           << b.propellant_used << " kg, engine delta-v " << b.delta_v_rocket
           << " m/s, speed change " << b.speed_change << " m/s (difference = gravity loss)"
           << (b.ran_dry ? "  [TANK RAN DRY]" : "");
    }
    return os.str();
}

}  // namespace sf::navigation
