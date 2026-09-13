#pragma once

// Dormand-Prince 5(4), seven stages, FSAL, with a PI step size controller.
// See ADR-0005 for why this integrator and not a symplectic one.

#include "core/attitude/inertia.hpp"
#include "core/gravity/force_model.hpp"
#include "core/gravity/weak_field_metric.hpp"
#include "core/propagation/dense_output.hpp"
#include "core/propagation/spacecraft_propagator.hpp"

#include <array>
#include <cstddef>

namespace sf::propagation {

class DormandPrince54Propagator final : public SpacecraftPropagator {
public:
    // `forces` must outlive the propagator.
    explicit DormandPrince54Propagator(const gravity::ForceModel& forces,
                                       IntegratorConfig config = {});

    PropagationResult propagate(const PropagationState& initial,
                                time::CoordinateTime from,
                                time::CoordinateTime to) override;

    void set_step_observer(StepObserver observer) { observer_ = std::move(observer); }
    void observe_rejected_steps(bool yes) { observe_rejected_ = yes; }

    // Records a dense segment per accepted step into `sink` (cleared first), so
    // the arc can be queried at arbitrary epochs afterwards.  Pass nullptr to
    // stop recording.  Recording costs no force evaluations and does not change
    // the trajectory by a single bit -- tests/scientific/test_dense_output.cpp
    // asserts exactly that.  See ADR-0006.
    void set_trajectory_recorder(Trajectory* sink) { recorder_ = sink; }

    // Attitude is integrated only when an inertia tensor is available: without
    // one there is no way to turn torque into angular acceleration, and a
    // scenario with no attitude should not pay for seven unused state
    // components. `inertia` must outlive the propagator; nullptr disables.
    void set_inertia(const attitude::InertiaTensor* inertia) { inertia_ = inertia; }

    // Required by Kinematics::WeakFieldStaticMetric and ignored otherwise. The
    // metric carries gravity, so the force model must carry only thrust.
    void set_metric(const gravity::WeakFieldMetric* metric) { metric_ = metric; }
    [[nodiscard]] const gravity::WeakFieldMetric* metric() const noexcept { return metric_; }
    [[nodiscard]] const attitude::InertiaTensor* inertia() const noexcept { return inertia_; }
    [[nodiscard]] Trajectory* trajectory_recorder() const noexcept { return recorder_; }

    [[nodiscard]] const IntegratorConfig& config() const noexcept { return config_; }
    void set_config(IntegratorConfig config);

private:
    // State layout: [x y z vx vy vz tau mass q omega]. Proper time rides along
    // as component 6 and has its own error floor because its derivative varies
    // in the relativistic modes. Milestone 4 turns components 3..5 into
    // u = gamma*v.
    static constexpr std::size_t kDim = kStateDimension;
    using Vector = StateArray;

    [[nodiscard]] Vector derivative(const Vector& y, time::CoordinateTime t,
                                    gravity::ForceResult& out_force) const;

    [[nodiscard]] double error_norm(const Vector& y, const Vector& y_new, const Vector& err) const;

    const gravity::ForceModel& forces_;
    IntegratorConfig config_;
    StepObserver observer_;
    Trajectory* recorder_{nullptr};
    const attitude::InertiaTensor* inertia_{nullptr};
    const gravity::WeakFieldMetric* metric_{nullptr};
    bool observe_rejected_{false};
};

}  // namespace sf::propagation
