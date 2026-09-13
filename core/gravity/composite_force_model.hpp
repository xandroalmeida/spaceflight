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

    CompositeForceModel& add(std::unique_ptr<ForceModel> model);

    [[nodiscard]] ForceResult evaluate(const propagation::PropagationState& spacecraft,
                                       time::CoordinateTime t) const override;

    [[nodiscard]] std::string_view name() const override { return "CompositeForceModel"; }

    [[nodiscard]] std::size_t size() const noexcept { return models_.size(); }
    [[nodiscard]] std::string describe() const;

private:
    std::vector<std::unique_ptr<ForceModel>> models_;
};

}  // namespace sf::gravity
