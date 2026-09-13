#pragma once

// The spacecraft's physical configuration: what it weighs and what it burns.
//
// Note what is NOT here: the current mass.  That lives in PropagationState,
// because it is integrated (docs/physics/propulsion-model.md section 6.1).
// Keeping a second copy would let the two disagree, and the one in the state
// vector is the one the acceleration actually uses.

#include "core/config/json.hpp"
#include "core/propulsion/engine.hpp"

#include <string>

namespace sf::spacecraft {

class Spacecraft {
public:
    Spacecraft(std::string name, double dry_mass_kg, double initial_propellant_kg,
               propulsion::EngineSpec engine);

    static Spacecraft from_json(const config::json::Value& value, const std::string& context);
    static Spacecraft from_file(const std::string& path);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] double dry_mass() const noexcept { return dry_mass_; }
    [[nodiscard]] double initial_propellant() const noexcept { return initial_propellant_; }
    [[nodiscard]] double initial_mass() const noexcept { return dry_mass_ + initial_propellant_; }
    [[nodiscard]] const propulsion::EngineSpec& engine() const noexcept { return engine_; }

    // Propellant left when the integrated total mass is `total_mass`.  Clamped at
    // zero: a negative reading would mean the burn overran the tank, which the
    // mission runner prevents by splitting the burn at the exhaustion instant.
    [[nodiscard]] double propellant_at(double total_mass) const;
    [[nodiscard]] bool has_propellant(double total_mass) const;

    // Total delta-v available from a given total mass, by Tsiolkovsky.
    [[nodiscard]] double delta_v_budget(double total_mass) const;

    [[nodiscard]] std::string describe() const;

private:
    std::string name_;
    double dry_mass_{0.0};
    double initial_propellant_{0.0};
    propulsion::EngineSpec engine_;
};

}  // namespace sf::spacecraft
