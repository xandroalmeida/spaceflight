#include "core/propulsion/engine.hpp"

#include "core/units/constants.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sf::propulsion {

EngineSpec::EngineSpec(std::string name, double max_mass_flow_kg_s,
                       double exhaust_velocity_fraction_c, double efficiency)
    : name_(std::move(name)),
      max_mass_flow_(max_mass_flow_kg_s),
      fraction_c_(exhaust_velocity_fraction_c),
      exhaust_velocity_(exhaust_velocity_fraction_c * units::c),
      efficiency_(efficiency),
      inverse_gamma_w_(std::sqrt(std::max(0.0, 1.0 - exhaust_velocity_fraction_c *
                                                        exhaust_velocity_fraction_c))) {
    if (!(max_mass_flow_ > 0.0) || !std::isfinite(max_mass_flow_)) {
        throw std::invalid_argument("engine \"" + name_ + "\": max_mass_flow_kg_s must be > 0");
    }
    if (!(fraction_c_ > 0.0) || !(fraction_c_ <= 1.0)) {
        throw std::invalid_argument(
            "engine \"" + name_ + "\": exhaust_velocity_fraction_c must be in (0, 1]; "
            "an exhaust velocity above c is not a faster engine, it is an invalid model "
            "(docs/physics/propulsion-model.md section 2)");
    }
    if (!(efficiency_ > 0.0) || !(efficiency_ <= 1.0)) {
        throw std::invalid_argument("engine \"" + name_ + "\": efficiency must be in (0, 1]");
    }
}

EngineSpec EngineSpec::from_json(const config::json::Value& value, const std::string& context) {
    return EngineSpec{
        value.string_or("name", "unnamed engine"),
        value.require("max_mass_flow_kg_s", context).as_number("max_mass_flow_kg_s"),
        value.require("exhaust_velocity_fraction_c", context)
            .as_number("exhaust_velocity_fraction_c"),
        value.require("efficiency", context).as_number("efficiency")};
}

EngineSpec EngineSpec::from_file(const std::string& path) {
    const auto root = config::json::parse_file(path);
    const config::json::Value* engine = root.get("engine");
    return from_json(engine != nullptr ? *engine : root, path);
}

double EngineSpec::specific_impulse() const noexcept {
    return effective_exhaust_velocity() / units::g0;
}

double EngineSpec::mass_flow_at(double throttle) const {
    return std::clamp(throttle, 0.0, 1.0) * max_mass_flow_;
}

double EngineSpec::thrust_at(double throttle) const {
    // F = eta * q * w.  Linear in throttle BECAUSE the consumption is.
    return efficiency_ * mass_flow_at(throttle) * exhaust_velocity_;
}

double EngineSpec::jet_mass_flow_at(double throttle) const {
    return efficiency_ * mass_flow_at(throttle) * inverse_gamma_w_;
}

double EngineSpec::rest_energy_flux_at(double throttle) const {
    return mass_flow_at(throttle) * units::c_squared;
}

double EngineSpec::converted_power_at(double throttle) const {
    // (q - mu) c^2 : the rest mass that stops being rest mass.
    return rest_energy_flux_at(throttle) * (1.0 - efficiency_ * inverse_gamma_w_);
}

double EngineSpec::jet_kinetic_power_at(double throttle) const {
    // (gamma_w - 1) mu c^2, written so that w = c stays finite.
    return efficiency_ * rest_energy_flux_at(throttle) * (1.0 - inverse_gamma_w_);
}

double EngineSpec::waste_power_at(double throttle) const {
    return (1.0 - efficiency_) * rest_energy_flux_at(throttle);
}

double EngineSpec::delta_v_for_mass_ratio(double mass_before, double mass_after) const {
    if (!(mass_before > 0.0) || !(mass_after > 0.0) || mass_after > mass_before) {
        throw std::invalid_argument("delta_v_for_mass_ratio: need 0 < mass_after <= mass_before");
    }
    return effective_exhaust_velocity() * std::log(mass_before / mass_after);
}

double EngineSpec::propellant_for_delta_v(double mass_before, double delta_v) const {
    if (!(mass_before > 0.0)) {
        throw std::invalid_argument("propellant_for_delta_v: mass_before must be > 0");
    }
    if (delta_v < 0.0) {
        throw std::invalid_argument("propellant_for_delta_v: delta_v must be >= 0 "
                                    "(direction is the maneuver's business, not the engine's)");
    }
    return mass_before * (1.0 - std::exp(-delta_v / effective_exhaust_velocity()));
}

double EngineSpec::burn_duration_for_delta_v(double mass_before, double delta_v,
                                             double throttle) const {
    const double flow = mass_flow_at(throttle);
    if (!(flow > 0.0)) {
        throw std::invalid_argument("burn_duration_for_delta_v: throttle must be > 0");
    }
    return propellant_for_delta_v(mass_before, delta_v) / flow;
}

std::string EngineSpec::describe() const {
    std::ostringstream os;
    os << std::setprecision(6);
    os << name_ << ": q_max = " << max_mass_flow_ << " kg/s, w = " << fraction_c_ << " c = "
       << exhaust_velocity_ << " m/s, eta = " << efficiency_
       << "\n  thrust (full)    = " << max_thrust() << " N"
       << "\n  v_eff = eta*w    = " << effective_exhaust_velocity() << " m/s"
       << "\n  Isp              = " << specific_impulse() << " s"
       << "\n  mass converted   = " << converted_power_at(1.0) / units::c_squared << " kg/s of "
       << max_mass_flow_ << " kg/s consumed"
       << "\n  power converted  = " << converted_power_at(1.0) << " W"
       << "\n    into the jet   = " << jet_kinetic_power_at(1.0) << " W"
       << "\n    wasted as heat = " << waste_power_at(1.0) << " W";
    return os.str();
}

}  // namespace sf::propulsion
