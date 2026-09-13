#include "core/navigation/targeting.hpp"

#include "core/math/mat3.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace sf::navigation {

using math::Mat3;
using math::Vec3;

TargetingResult correct_departure(const std::function<Vec3(const Vec3&)>& arrival,
                                  const Vec3& initial_guess, const Vec3& target,
                                  TargetingConfig config) {
    if (!arrival) {
        throw std::invalid_argument("correct_departure: no arrival function");
    }
    if (!(config.position_tolerance > 0.0) || !(config.velocity_step > 0.0) ||
        config.max_iterations < 1) {
        throw std::invalid_argument("correct_departure: invalid configuration");
    }

    TargetingResult result{};
    result.initial_guess = initial_guess;
    result.departure_velocity = initial_guess;

    Vec3 velocity = initial_guess;
    Vec3 miss = arrival(velocity) - target;
    ++result.evaluations;
    result.initial_miss = miss.norm();
    result.miss_distance = result.initial_miss;

    for (int iteration = 0; iteration < config.max_iterations; ++iteration) {
        if (miss.norm() <= config.position_tolerance) {
            result.converged = true;
            result.message = "converged";
            break;
        }

        // Jacobian by forward differences: three extra trajectories, one per
        // component of the departure velocity.  Forward rather than central
        // because the arrival map is smooth here and central would double the
        // cost for accuracy the Newton step does not need.
        const Vec3 basis[3] = {Vec3::unit_x(), Vec3::unit_y(), Vec3::unit_z()};
        Vec3 columns[3];
        bool usable = true;
        for (int i = 0; i < 3; ++i) {
            const Vec3 perturbed = velocity + basis[i] * config.velocity_step;
            const Vec3 shifted = arrival(perturbed) - target;
            ++result.evaluations;
            columns[i] = (shifted - miss) / config.velocity_step;
            if (!columns[i].is_finite()) {
                usable = false;
            }
        }

        if (!usable) {
            result.message = "the trajectory model returned a non-finite arrival; the transfer is "
                             "probably hitting a body";
            break;
        }

        const Mat3 jacobian = Mat3::from_columns(columns[0], columns[1], columns[2]);
        const auto solution = jacobian.solve(-miss);
        if (!solution.ok) {
            result.message = "the arrival is insensitive to the departure velocity in some "
                             "direction (singular Jacobian); a different transfer geometry is "
                             "needed";
            break;
        }

        Vec3 correction = solution.x;
        const double magnitude = correction.norm();
        if (magnitude > config.max_correction) {
            // Newton overshoots badly when the initial guess is far off; damping
            // the step keeps the iteration inside the regime where the linear
            // model is meaningful.
            correction = correction * (config.max_correction / magnitude);
        }

        // Backtracking line search.  The Jacobian is honest about the local
        // slope; it is the step LENGTH that a strongly nonlinear map punishes.
        double lambda = 1.0;
        bool improved = false;
        Vec3 candidate{};
        Vec3 candidate_miss{};
        for (int attempt = 0; attempt <= config.max_backtracks; ++attempt) {
            candidate = velocity + correction * lambda;
            candidate_miss = arrival(candidate) - target;
            ++result.evaluations;
            if (candidate_miss.is_finite() && candidate_miss.norm() < miss.norm()) {
                improved = true;
                break;
            }
            lambda *= 0.5;
        }

        if (!improved) {
            // Not an error: the linear model has stopped helping even at 1/256 of
            // the step.  Report the best point reached rather than wandering.
            std::ostringstream os;
            os << "stalled after " << iteration << " iteration(s): no step along the Newton "
               << "direction reduces the miss (best " << candidate_miss.norm() << " m vs "
               << miss.norm() << " m)";
            result.message = os.str();
            break;
        }

        velocity = candidate;
        miss = candidate_miss;
        result.iterations = iteration + 1;
        result.departure_velocity = velocity;
        result.miss_distance = miss.norm();
    }

    if (result.message.empty()) {
        result.message = "iteration limit reached";
    }
    if (miss.norm() <= config.position_tolerance) {
        result.converged = true;
        result.message = "converged";
    }
    result.miss_distance = miss.norm();
    result.departure_velocity = velocity;
    return result;
}

}  // namespace sf::navigation
