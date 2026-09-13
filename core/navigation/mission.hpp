#pragma once

// Runs a plan: propagates from one epoch to another, breaking the integration at
// every ignition, cutoff and propellant exhaustion so that the integrator never
// crosses a discontinuity in the derivative.
// See docs/architecture/navigation.md section 4.

#include "core/navigation/maneuver_executor.hpp"
#include "core/propagation/dense_output.hpp"
#include "core/propagation/dormand_prince_54.hpp"

#include <string>
#include <vector>

namespace sf::navigation {

// What a burn actually did, as opposed to what it was asked to do.
struct BurnReport {
    std::string name;
    time::CoordinateTime ignition{};
    time::Duration duration{};        // as executed: shorter if the tank ran dry
    double mass_before{0.0};
    double mass_after{0.0};
    double propellant_used{0.0};

    // Delta-v the engine delivered, from the rocket equation.  This is what the
    // propellant bought.
    double delta_v_rocket{0.0};

    // Change in speed relative to the maneuver's reference body.  For a prograde
    // burn this is delta_v_rocket MINUS the gravity loss, so the difference
    // between the two columns is the gravity loss, measured rather than assumed
    // (docs/architecture/navigation.md section 2).
    double speed_change{0.0};

    bool ran_dry{false};
};

struct MissionResult {
    propagation::PropagationState state{};
    time::CoordinateTime time{};
    propagation::PropagationStatus status{propagation::PropagationStatus::Success};
    propagation::IntegratorStats stats{};   // accumulated over all segments
    std::string message;
    std::vector<BurnReport> burns;
    std::size_t segments{0};                // integration legs, = switch points + 1

    [[nodiscard]] bool ok() const { return status == propagation::PropagationStatus::Success; }
    [[nodiscard]] std::string describe_burns() const;
};

// `propagator` must already be configured with a force model that INCLUDES the
// executor (gravity + thrust); the executor is passed separately so the runner
// can read the plan and, crucially, ARM it for each leg -- see
// ManeuverExecutor::arm().  If `trajectory` is non-null the dense
// output of every leg is appended to it, producing one continuous arc.
MissionResult run_mission(propagation::DormandPrince54Propagator& propagator,
                          ManeuverExecutor& executor,
                          const propagation::PropagationState& initial,
                          time::CoordinateTime from,
                          time::CoordinateTime to,
                          propagation::Trajectory* trajectory = nullptr);

}  // namespace sf::navigation
