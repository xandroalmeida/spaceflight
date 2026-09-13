#include "core/propagation/dormand_prince_54.hpp"

#include "core/relativity/kinematics.hpp"

#include <algorithm>
#include <iterator>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sf::propagation {
namespace {

// Dormand & Prince (1980), RK5(4)7M.  Node c7 == 1 and row a7 == b, which is
// what makes the method FSAL: the last stage of an accepted step is the first
// stage of the next one.
constexpr double c2 = 1.0 / 5.0;
constexpr double c3 = 3.0 / 10.0;
constexpr double c4 = 4.0 / 5.0;
constexpr double c5 = 8.0 / 9.0;

constexpr double a21 = 1.0 / 5.0;
constexpr double a31 = 3.0 / 40.0,        a32 = 9.0 / 40.0;
constexpr double a41 = 44.0 / 45.0,       a42 = -56.0 / 15.0,      a43 = 32.0 / 9.0;
constexpr double a51 = 19372.0 / 6561.0,  a52 = -25360.0 / 2187.0, a53 = 64448.0 / 6561.0,  a54 = -212.0 / 729.0;
constexpr double a61 = 9017.0 / 3168.0,   a62 = -355.0 / 33.0,     a63 = 46732.0 / 5247.0,  a64 = 49.0 / 176.0,      a65 = -5103.0 / 18656.0;

// 5th order solution (also row 7 of A).
constexpr double b1 = 35.0 / 384.0, b3 = 500.0 / 1113.0, b4 = 125.0 / 192.0, b5 = -2187.0 / 6784.0, b6 = 11.0 / 84.0;

// Difference between the 5th and the embedded 4th order solution.
constexpr double e1 = b1 - 5179.0 / 57600.0;
constexpr double e3 = b3 - 7571.0 / 16695.0;
constexpr double e4 = b4 - 393.0 / 640.0;
constexpr double e5 = b5 - (-92097.0 / 339200.0);
constexpr double e6 = b6 - 187.0 / 2100.0;
constexpr double e7 = -1.0 / 40.0;

// Coefficients of the 4th order continuous extension (Hairer's contd5).  They
// reuse the seven stages the step already computed, which is why dense output
// costs nothing.  See ADR-0006.
constexpr double d1 = -12715105075.0 / 11282082432.0;
constexpr double d3 = 87487479700.0 / 32700410799.0;
constexpr double d4 = -10690763975.0 / 1880347072.0;
constexpr double d5 = 701980252875.0 / 199316789632.0;
constexpr double d6 = -1453857185.0 / 822651844.0;
constexpr double d7 = 69997945.0 / 29380423.0;

}  // namespace

DormandPrince54Propagator::DormandPrince54Propagator(const gravity::ForceModel& forces,
                                                     IntegratorConfig config)
    : forces_(forces), config_(config) {
    set_config(config);
}

void DormandPrince54Propagator::set_config(IntegratorConfig config) {
    if (config.relative_tolerance <= 0.0) {
        throw std::invalid_argument("IntegratorConfig: relative_tolerance must be > 0");
    }
    if (config.absolute_tolerance_position <= 0.0 || config.absolute_tolerance_velocity <= 0.0) {
        throw std::invalid_argument("IntegratorConfig: absolute tolerances must be > 0");
    }
    if (config.min_step.seconds() <= 0.0 || config.max_step.seconds() <= 0.0) {
        throw std::invalid_argument("IntegratorConfig: step limits must be > 0");
    }
    if (config.min_step > config.max_step) {
        throw std::invalid_argument("IntegratorConfig: min_step > max_step");
    }
    config_ = config;
}

DormandPrince54Propagator::Vector DormandPrince54Propagator::derivative(
    const Vector& y, time::CoordinateTime t, gravity::ForceResult& out_force) const {
    const PropagationState s = state_from_array(y, config_.kinematics);
    out_force = forces_.evaluate(s, t);

    Vector dy{};

    // Thrust is reported in the rest frame; what it does to the trajectory is
    // decided here, by the kinematics.
    math::Vec3 velocity_derivative{};
    double inverse_gamma = 1.0;

    if (config_.kinematics == Kinematics::SpecialRelativistic) {
        const math::Vec3 u{y[3], y[4], y[5]};
        const double gamma = relativity::lorentz_factor(u);
        inverse_gamma = 1.0 / gamma;
        const math::Vec3 v = u * inverse_gamma;

        dy[0] = v.x;
        dy[1] = v.y;
        dy[2] = v.z;

        // du/dt = F * e_spatial / (m0 gamma), with e_spatial the boost of the
        // rest-frame unit vector:  e = n + (gamma - 1)(n.beta_hat) beta_hat.
        // Parallel to the motion this collapses to du/dt = F/m0 -- the proper
        // acceleration, with no gamma at all -- and perpendicular to it the
        // response is smaller by gamma. See relativistic-propulsion.md 2.3.
        const double thrust = out_force.proper_thrust.norm();
        if (thrust > 0.0 && s.mass > 0.0) {
            const math::Vec3 n = out_force.proper_thrust / thrust;
            const double speed = v.norm();
            math::Vec3 e_spatial = n;
            if (speed > 0.0) {
                const math::Vec3 beta_hat = v / speed;
                e_spatial += beta_hat * ((gamma - 1.0) * dot(n, beta_hat));
            }
            velocity_derivative = e_spatial * (thrust * inverse_gamma / s.mass);
        }
        // Any non-thrust coordinate acceleration is a flat-spacetime fiction
        // here; the propagate() loop refuses it unless the caller opted in.
        velocity_derivative += out_force.acceleration;
    } else {
        dy[0] = y[3];
        dy[1] = y[4];
        dy[2] = y[5];
        velocity_derivative = out_force.acceleration;
        if (s.mass > 0.0) {
            velocity_derivative += out_force.proper_thrust / s.mass;
        }
    }

    dy[3] = velocity_derivative.x;
    dy[4] = velocity_derivative.y;
    dy[5] = velocity_derivative.z;

    // dtau/dt = 1/gamma, which is exactly 1 in the Newtonian regime.
    dy[6] = inverse_gamma;
    // dm0/dt = (dm0/dtau)/gamma: the force models report consumption in the
    // rest frame, which is where it is measured.
    dy[7] = out_force.mass_flow_rate * inverse_gamma;

    if (inertia_ != nullptr) {
        // qdot = 1/2 q (x) (0, omega_body)
        const math::Quaternion q_dot =
            math::attitude_derivative(s.attitude.orientation, s.attitude.angular_velocity);
        // Body dynamics runs on the ship's own clock, so the coordinate-time
        // derivatives carry a factor 1/gamma. Thomas precession -- the rotation a
        // non-collinearly accelerated frame picks up -- is NOT included; it needs
        // its own derivation and belongs with the geodesic work.
        dy[8] = q_dot.w() * inverse_gamma;
        dy[9] = q_dot.x() * inverse_gamma;
        dy[10] = q_dot.y() * inverse_gamma;
        dy[11] = q_dot.z() * inverse_gamma;

        // Euler: omega_dot = I^-1 (tau - omega x (I omega)). The gyroscopic term
        // does no work but is responsible for precession, nutation and the
        // intermediate axis instability.
        const math::Vec3 momentum = inertia_->angular_momentum(s.attitude.angular_velocity);
        const math::Vec3 gyroscopic = cross(s.attitude.angular_velocity, momentum);
        const math::Vec3 alpha = inertia_->angular_acceleration(out_force.torque - gyroscopic);
        dy[12] = alpha.x * inverse_gamma;
        dy[13] = alpha.y * inverse_gamma;
        dy[14] = alpha.z * inverse_gamma;
    }
    return dy;
}

double DormandPrince54Propagator::error_norm(const Vector& y, const Vector& y_new,
                                             const Vector& err) const {
    // RMS of the componentwise error scaled by  atol + rtol*max(|y|,|y_new|),
    // over position, velocity and mass.
    //
    // Proper time (component 6) is excluded: its derivative is exact in this
    // regime, so including it would dilute the norm for no reason.  Mass
    // (component 7) IS included, because while an engine burns the acceleration
    // is F/m and an error in m propagates straight into the trajectory.
    constexpr std::size_t kControlled[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14};
    double sum = 0.0;
    std::size_t counted = 0;
    for (const std::size_t i : kControlled) {
        if (i >= 8 && inertia_ == nullptr) {
            continue;  // attitude is not being integrated
        }
        ++counted;
        double atol = config_.absolute_tolerance_velocity;
        if (i < 3) {
            atol = config_.absolute_tolerance_position;
        } else if (i == 7) {
            atol = config_.absolute_tolerance_mass;
        } else if (i >= 8 && i < 12) {
            atol = config_.absolute_tolerance_orientation;
        } else if (i >= 12) {
            atol = config_.absolute_tolerance_angular_velocity;
        }
        const double scale = atol + config_.relative_tolerance *
                                        std::max(std::abs(y[i]), std::abs(y_new[i]));
        const double ratio = err[i] / scale;
        sum += ratio * ratio;
    }
    return std::sqrt(sum / static_cast<double>(counted));
}

PropagationResult DormandPrince54Propagator::propagate(const PropagationState& initial,
                                                       time::CoordinateTime from,
                                                       time::CoordinateTime to) {
    using clock = std::chrono::steady_clock;
    const auto wall_start = clock::now();

    PropagationResult result{};
    result.state = initial;
    result.time = from;
    result.status = PropagationStatus::Success;

    if (recorder_ != nullptr) {
        recorder_->clear();
    }

    if (!initial.is_finite()) {
        result.status = PropagationStatus::NonFiniteState;
        result.message = "initial state is not finite";
        return result;
    }

    const double total = (to - from).seconds();
    if (total == 0.0) {
        return result;
    }
    const double direction = total > 0.0 ? 1.0 : -1.0;

    Vector y = array_from_state(initial, config_.kinematics);
    time::CoordinateTime t = from;

    double h = direction * std::min({std::abs(config_.initial_step.seconds()),
                                     config_.max_step.seconds(),
                                     std::abs(total)});
    if (std::abs(h) < config_.min_step.seconds()) {
        h = direction * config_.min_step.seconds();
    }

    gravity::ForceResult force{};
    Vector k1 = derivative(y, t, force);

    // The refusal promised by relativistic-propulsion.md section 8, checked once
    // on the first force evaluation rather than in the inner loop.
    if (config_.kinematics == Kinematics::SpecialRelativistic &&
        !config_.allow_gravity_with_relativistic_kinematics &&
        force.acceleration.norm_squared() > 0.0) {
        result.status = PropagationStatus::UnsupportedRegime;
        result.message =
            "special-relativistic kinematics was given a non-thrust acceleration of " +
            std::to_string(force.acceleration.norm()) +
            " m/s^2. That mixes flat-spacetime dynamics with a Newtonian field, and the error is "
            "silent. Set allow_gravity_with_relativistic_kinematics if the field really is weak "
            "and the speeds moderate";
        return result;
    }
    result.stats.force_evaluations = 1;

    double error_previous = 1.0e-4;  // seeds the PI controller
    double step_sum = 0.0;
    double min_h = std::numeric_limits<double>::infinity();
    double max_h = 0.0;
    bool last_step_was_rejected = false;

    while (true) {
        const double remaining = (to - t).seconds();
        if (remaining == 0.0 || direction * remaining <= 0.0) {
            break;
        }
        // Land exactly on `to` without letting the clipped step contaminate the
        // step size controller: `h` stays the size the error control asked for,
        // `h_step` is what this particular step actually spans.
        const bool clipped = std::abs(h) > std::abs(remaining);
        const double h_step = clipped ? remaining : h;

        if (result.stats.accepted_steps + result.stats.rejected_steps >= config_.max_steps) {
            result.status = PropagationStatus::MaxStepsExceeded;
            result.message = "exceeded max_steps (" + std::to_string(config_.max_steps) + ")";
            break;
        }

        Vector y2{}, y3{}, y4{}, y5{}, y6{}, y_new{}, err{};
        for (std::size_t i = 0; i < kDim; ++i) {
            y2[i] = y[i] + h_step * a21 * k1[i];
        }
        Vector k2 = derivative(y2, t + time::Duration{c2 * h_step}, force);

        for (std::size_t i = 0; i < kDim; ++i) {
            y3[i] = y[i] + h_step * (a31 * k1[i] + a32 * k2[i]);
        }
        Vector k3 = derivative(y3, t + time::Duration{c3 * h_step}, force);

        for (std::size_t i = 0; i < kDim; ++i) {
            y4[i] = y[i] + h_step * (a41 * k1[i] + a42 * k2[i] + a43 * k3[i]);
        }
        Vector k4 = derivative(y4, t + time::Duration{c4 * h_step}, force);

        for (std::size_t i = 0; i < kDim; ++i) {
            y5[i] = y[i] + h_step * (a51 * k1[i] + a52 * k2[i] + a53 * k3[i] + a54 * k4[i]);
        }
        Vector k5 = derivative(y5, t + time::Duration{c5 * h_step}, force);

        for (std::size_t i = 0; i < kDim; ++i) {
            y6[i] = y[i] + h_step * (a61 * k1[i] + a62 * k2[i] + a63 * k3[i] + a64 * k4[i] + a65 * k5[i]);
        }
        Vector k6 = derivative(y6, t + time::Duration{h_step}, force);

        for (std::size_t i = 0; i < kDim; ++i) {
            y_new[i] = y[i] + h_step * (b1 * k1[i] + b3 * k3[i] + b4 * k4[i] + b5 * k5[i] + b6 * k6[i]);
        }
        gravity::ForceResult force_new{};
        Vector k7 = derivative(y_new, t + time::Duration{h_step}, force_new);
        result.stats.force_evaluations += 6;

        for (std::size_t i = 0; i < kDim; ++i) {
            err[i] = h_step * (e1 * k1[i] + e3 * k3[i] + e4 * k4[i] + e5 * k5[i] + e6 * k6[i] + e7 * k7[i]);
        }

        bool finite = true;
        for (std::size_t i = 0; i < kDim; ++i) {
            finite = finite && std::isfinite(y_new[i]) && std::isfinite(err[i]);
        }

        const double error = finite ? error_norm(y, y_new, err)
                                    : std::numeric_limits<double>::infinity();
        result.stats.max_error_estimate = std::max(result.stats.max_error_estimate,
                                                   std::isfinite(error) ? error : 0.0);

        const bool accept = finite && error <= 1.0;

        if (accept) {
            if (recorder_ != nullptr) {
                DenseSegment segment{};
                segment.begin = t;
                segment.step_seconds = h_step;
                segment.kinematics = config_.kinematics;
                for (std::size_t i = 0; i < kDim; ++i) {
                    const double difference = y_new[i] - y[i];
                    const double bspl = h_step * k1[i] - difference;
                    segment.coefficients[0][i] = y[i];
                    segment.coefficients[1][i] = difference;
                    segment.coefficients[2][i] = bspl;
                    segment.coefficients[3][i] = difference - h_step * k7[i] - bspl;
                    segment.coefficients[4][i] =
                        h_step * (d1 * k1[i] + d3 * k3[i] + d4 * k4[i] + d5 * k5[i] +
                                  d6 * k6[i] + d7 * k7[i]);
                }
                recorder_->append(std::move(segment));
            }

            // A clipped step was defined as "cover exactly what is left", so the
            // arrival time IS `to`.  Snapping avoids a residual of a fraction of
            // an ulp that would otherwise cost one extra, absurdly small step.
            t = clipped ? to : t + time::Duration{h_step};
            y = y_new;

            if (inertia_ != nullptr) {
                const math::Quaternion q{y[8], y[9], y[10], y[11]};
                const double drift = std::abs(q.norm() - 1.0);
                result.stats.max_quaternion_drift =
                    std::max(result.stats.max_quaternion_drift, drift);
                const math::Quaternion unit = q.normalized();
                y[8] = unit.w();
                y[9] = unit.x();
                y[10] = unit.y();
                y[11] = unit.z();
            }
            k1 = k7;  // FSAL
            force = force_new;

            ++result.stats.accepted_steps;
            step_sum += std::abs(h_step);
            min_h = std::min(min_h, std::abs(h_step));
            max_h = std::max(max_h, std::abs(h_step));

            if (observer_) {
                observer_(StepInfo{t, state_from_array(y, config_.kinematics), h_step, error, true});
            }

            if (force_new.inside_body && config_.stop_inside_body) {
                result.status = PropagationStatus::InsideBody;
                result.message = "trajectory entered " + force_new.inside_of.name();
                break;
            }
        } else {
            ++result.stats.rejected_steps;
            if (observer_ && observe_rejected_) {
                observer_(StepInfo{t + time::Duration{h_step},
                                   state_from_array(y_new, config_.kinematics), h_step, error,
                                   false});
            }
            if (!finite) {
                result.status = PropagationStatus::NonFiniteState;
                result.message = "force model or state became non-finite";
                break;
            }
        }

        // PI controller (Hairer, Norsett & Wanner II.4).  On a rejected step the
        // integral term is dropped, which is what keeps the controller from
        // oscillating after a sudden tightening.
        const double alpha = 0.2 - 0.75 * config_.pi_beta;
        double factor = config_.safety_factor * std::pow(error, -alpha);
        if (accept && !last_step_was_rejected) {
            factor *= std::pow(error_previous, config_.pi_beta);
        }
        factor = std::clamp(factor, config_.min_shrink_factor, config_.max_growth_factor);
        if (!accept) {
            factor = std::min(factor, 1.0);  // never grow after a rejection
        }

        // When the step was clipped to land on `to`, the controller must grow
        // from the step it actually wanted, not from the stub.  Otherwise a
        // short final step would look like a collapsing step size and trip the
        // min_step check on a propagation that in fact succeeded.
        double h_next = (clipped ? h : h_step) * factor;
        if (std::abs(h_next) > config_.max_step.seconds()) {
            h_next = direction * config_.max_step.seconds();
        }

        if (std::abs(h_next) < config_.min_step.seconds()) {
            // The error control is asking for a step we refuse to take.  That is
            // a reported failure, not something to force through: taking the step
            // anyway would silently produce a result outside the requested
            // tolerance.  See ADR-0005.
            result.status = PropagationStatus::MinimumStepReached;
            result.message = "step control requested " + std::to_string(std::abs(h_next)) +
                             " s, below min_step " + std::to_string(config_.min_step.seconds()) + " s";
            break;
        }

        if (accept) {
            error_previous = std::max(error, 1.0e-4);
        }
        last_step_was_rejected = !accept;
        h = h_next;
    }

    result.state = state_from_array(y, config_.kinematics);
    result.time = t;

    result.stats.min_step_seconds = std::isfinite(min_h) ? min_h : 0.0;
    result.stats.max_step_seconds = max_h;
    result.stats.mean_step_seconds =
        result.stats.accepted_steps > 0
            ? step_sum / static_cast<double>(result.stats.accepted_steps)
            : 0.0;
    result.stats.wall_time_seconds =
        std::chrono::duration<double>(clock::now() - wall_start).count();

    return result;
}

}  // namespace sf::propagation
