#include "core/propagation/spacecraft_propagator.hpp"

#include <iomanip>
#include <sstream>

namespace sf::propagation {

std::string to_string(PropagationStatus status) {
    switch (status) {
        case PropagationStatus::Success:            return "success";
        case PropagationStatus::OutOfPropellant:    return "propellant exhausted";
        case PropagationStatus::MinimumStepReached: return "minimum step reached";
        case PropagationStatus::MaxStepsExceeded:   return "maximum step count exceeded";
        case PropagationStatus::NonFiniteState:     return "non-finite state";
        case PropagationStatus::InvariantViolation: return "runtime invariant violation";
        case PropagationStatus::InsideBody:         return "trajectory entered a body";
        case PropagationStatus::UnsupportedRegime:
            return "relativistic kinematics with a gravity field (see "
                   "docs/physics/relativistic-propulsion.md section 8)";
    }
    return "unknown";
}

std::string IntegratorStats::to_string() const {
    std::ostringstream os;
    os << std::setprecision(6);
    os << "steps: " << accepted_steps << " accepted, " << rejected_steps << " rejected"
       << " | force evals: " << force_evaluations
       << " | h [s]: min " << min_step_seconds
       << ", mean " << mean_step_seconds
       << ", max " << max_step_seconds
       << " | max scaled error: " << max_error_estimate
       << " | max |q|-1: " << max_quaternion_drift
       << " | wall: " << wall_time_seconds << " s";
    return os.str();
}

}  // namespace sf::propagation
