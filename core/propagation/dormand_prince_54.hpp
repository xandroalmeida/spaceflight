#pragma once

// Dormand-Prince 5(4), seven stages, FSAL, with a PI step size controller.
// See ADR-0005 for why this integrator and not a symplectic one.

#include "core/gravity/force_model.hpp"
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

    [[nodiscard]] const IntegratorConfig& config() const noexcept { return config_; }
    void set_config(IntegratorConfig config);

private:
    // State layout: [x y z vx vy vz tau].  Proper time rides along as component
    // 6 and is not part of the error norm (its derivative is exact in the
    // Newtonian regime).  Milestone 4 turns components 3..5 into u = gamma*v.
    static constexpr std::size_t kDim = 7;
    using Vector = std::array<double, kDim>;

    [[nodiscard]] Vector derivative(const Vector& y, time::CoordinateTime t,
                                    double mass, gravity::ForceResult& out_force) const;

    [[nodiscard]] double error_norm(const Vector& y, const Vector& y_new, const Vector& err) const;

    const gravity::ForceModel& forces_;
    IntegratorConfig config_;
    StepObserver observer_;
    bool observe_rejected_{false};
};

}  // namespace sf::propagation
