#include "core/spacecraft/spacecraft.hpp"

#include "core/units/constants.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sf::spacecraft {

Spacecraft::Spacecraft(std::string name, double dry_mass_kg, double initial_propellant_kg,
                       propulsion::EngineSpec engine)
    : Spacecraft(std::move(name), dry_mass_kg, initial_propellant_kg,
                 propulsion::MultiModeEngine{std::move(engine)}) {}

Spacecraft::Spacecraft(std::string name, double dry_mass_kg, double initial_propellant_kg,
                       propulsion::MultiModeEngine engine)
    : name_(std::move(name)),
      dry_mass_(dry_mass_kg),
      initial_propellant_(initial_propellant_kg),
      engine_(std::move(engine)) {
    if (!(dry_mass_ > 0.0)) {
        throw std::invalid_argument("spacecraft \"" + name_ + "\": dry_mass_kg must be > 0");
    }
    if (initial_propellant_ < 0.0) {
        throw std::invalid_argument("spacecraft \"" + name_ + "\": propellant mass must be >= 0");
    }
}

Spacecraft Spacecraft::from_json(const config::json::Value& value, const std::string& context) {
    const config::json::Value& engine = value.require("engine", context);
    return Spacecraft{value.string_or("name", "spacecraft"),
                      value.require("dry_mass_kg", context).as_number("dry_mass_kg"),
                      value.number_or("propellant_mass_kg", 0.0),
                      propulsion::MultiModeEngine::from_json(engine, context + ".engine")};
}

Spacecraft Spacecraft::from_file(const std::string& path) {
    const auto root = config::json::parse_file(path);
    const config::json::Value* craft = root.get("spacecraft");
    return from_json(craft != nullptr ? *craft : root, path);
}

double Spacecraft::propellant_at(double total_mass) const {
    return std::max(0.0, total_mass - dry_mass_);
}

bool Spacecraft::has_propellant(double total_mass) const { return propellant_at(total_mass) > 0.0; }

double Spacecraft::delta_v_budget(double total_mass) const {
    const double propellant = propellant_at(total_mass);
    if (propellant <= 0.0) {
        return 0.0;
    }
    return engine().delta_v_for_mass_ratio(total_mass, total_mass - propellant);
}

std::string Spacecraft::describe() const {
    std::ostringstream os;
    os << std::setprecision(8);
    os << name_ << ": dry " << dry_mass_ << " kg + propellant " << initial_propellant_ << " kg = "
       << initial_mass() << " kg"
       << "\n  delta-v budget   = " << delta_v_budget(initial_mass()) << " m/s"
       << " (" << delta_v_budget(initial_mass()) / units::c << " c) in mode \"" << mode_name()
       << "\""
       << "\n" << engine_.describe();
    return os.str();
}

}  // namespace sf::spacecraft
