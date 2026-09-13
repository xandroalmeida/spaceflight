#pragma once

// Sum of force models.  This is how gravity, propulsion and (later)
// relativistic corrections coexist without any of them knowing about the others,
// and without the integrator knowing about any of them.

#include "core/gravity/force_model.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sf::gravity {

class CompositeForceModel final : public ForceModel {
public:
    CompositeForceModel() = default;

    // Takes ownership.
    CompositeForceModel& add(std::unique_ptr<ForceModel> model);

    // Borrows.  Needed when the caller must keep talking to the model after
    // adding it -- the mission runner reads the plan out of the ManeuverExecutor
    // while the integrator is calling it as a force.  The referenced model must
    // outlive this object.
    CompositeForceModel& add_reference(const ForceModel& model);

    [[nodiscard]] ForceResult evaluate(const propagation::PropagationState& spacecraft,
                                       time::CoordinateTime t) const override;

    [[nodiscard]] std::string_view name() const override { return "CompositeForceModel"; }

    [[nodiscard]] std::size_t size() const noexcept { return models_.size(); }
    [[nodiscard]] std::string describe() const;

private:
    // Insertion order is preserved across both kinds, so the summation order --
    // and therefore the result, bit for bit -- is deterministic.
    std::vector<std::unique_ptr<ForceModel>> owned_;
    std::vector<const ForceModel*> models_;
};

}  // namespace sf::gravity
