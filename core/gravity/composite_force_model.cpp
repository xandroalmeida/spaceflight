#include "core/gravity/composite_force_model.hpp"

#include <stdexcept>

namespace sf::gravity {

CompositeForceModel& CompositeForceModel::add(std::unique_ptr<ForceModel> model) {
    if (!model) {
        throw std::invalid_argument("CompositeForceModel::add(nullptr)");
    }
    models_.push_back(std::move(model));
    return *this;
}

ForceResult CompositeForceModel::evaluate(const propagation::PropagationState& spacecraft,
                                          time::CoordinateTime t) const {
    ForceResult total{};
    for (const auto& model : models_) {
        const ForceResult r = model->evaluate(spacecraft, t);
        total.acceleration += r.acceleration;
        if (r.inside_body) {
            total.inside_body = true;
            total.inside_of = r.inside_of;
        }
    }
    return total;
}

std::string CompositeForceModel::describe() const {
    std::string out;
    for (const auto& model : models_) {
        if (!out.empty()) {
            out += " + ";
        }
        out += std::string{model->name()};
    }
    return out.empty() ? std::string{"(none)"} : out;
}

}  // namespace sf::gravity
