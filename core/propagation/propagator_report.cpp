#include "core/propagation/spacecraft_propagator.hpp"

#include <iomanip>
#include <sstream>

namespace sf::propagation {

std::string to_string(PropagationStatus status) {
    switch (status) {
        case PropagationStatus::Success:            return "success";
        case PropagationStatus::MinimumStepReached: return "minimum step reached";
        case PropagationStatus::MaxStepsExceeded:   return "maximum step count exceeded";
        case PropagationStatus::NonFiniteState:     return "non-finite state";
        case PropagationStatus::InsideBody:         return "trajectory entered a body";
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
       << " | wall: " << wall_time_seconds << " s";
    return os.str();
}

}  // namespace sf::propagation
