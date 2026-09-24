#pragma once

// The engine, as derived in docs/physics/propulsion-model.md.
//
//   F = eta * q * w            thrust                 [N]
//   dm/dt = -q                 rest mass consumption  [kg/s]
//   v_eff = eta * w            effective exhaust velocity (Newtonian limit)
//   P = q c^2                  power drawn from rest mass
//
// The engine is fictional.  The relations above are not: they follow from energy
// and momentum conservation, and they are what stops thrust and consumption from
// drifting apart (rule section 17).

#include "core/config/json.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace sf::propulsion {

class EngineSpec {
public:
    // Throws std::invalid_argument unless 0 < w/c <= 1, 0 < eta <= 1, q_max > 0.
    // An exhaust velocity above c is not a better engine, it is an invalid model.
    EngineSpec(std::string name, double max_mass_flow_kg_s, double exhaust_velocity_fraction_c,
               double efficiency);

    static EngineSpec from_json(const config::json::Value& value, const std::string& context);
    static EngineSpec from_file(const std::string& path);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] double max_mass_flow() const noexcept { return max_mass_flow_; }         // q_max
    [[nodiscard]] double exhaust_velocity() const noexcept { return exhaust_velocity_; }   // w
    [[nodiscard]] double exhaust_velocity_fraction_c() const noexcept { return fraction_c_; }
    [[nodiscard]] double efficiency() const noexcept { return efficiency_; }               // eta

    // v_eff = eta*w.  In the Newtonian limit this is the Tsiolkovsky exhaust
    // velocity; see propulsion-model.md section 4.2.
    [[nodiscard]] double effective_exhaust_velocity() const noexcept {
        return efficiency_ * exhaust_velocity_;
    }
    [[nodiscard]] double specific_impulse() const noexcept;  // [s]

    [[nodiscard]] double mass_flow_at(double throttle) const;   // [kg/s]
    [[nodiscard]] double thrust_at(double throttle) const;      // [N]
    [[nodiscard]] double max_thrust() const { return thrust_at(1.0); }

    // 1/gamma_w = sqrt(1 - (w/c)^2).  Stored as the inverse so that w = c (the
    // photon rocket) is a finite, exact zero instead of an infinity.
    [[nodiscard]] double inverse_jet_gamma() const noexcept { return inverse_gamma_w_; }

    // Rest mass leaving as jet, mu = eta*q/gamma_w.  The rest of the consumed
    // mass is converted to energy.
    [[nodiscard]] double jet_mass_flow_at(double throttle) const;

    // --- Energy bookkeeping -------------------------------------------------
    //
    //   rest_energy_flux  = q c^2                     gross rest energy passing through
    //   converted_power   = q c^2 (1 - eta/gamma_w)   rest mass actually turned into energy
    //   jet_kinetic_power = eta q c^2 (1 - 1/gamma_w) energy that ends up as jet motion
    //   waste_power       = (1 - eta) q c^2           radiated isotropically, no thrust
    //
    // and converted_power = jet_kinetic_power + waste_power identically.
    // See docs/physics/propulsion-model.md section 4.4: these are the numbers that
    // reveal what a given (w, eta) pair actually costs.
    [[nodiscard]] double rest_energy_flux_at(double throttle) const;
    [[nodiscard]] double converted_power_at(double throttle) const;
    [[nodiscard]] double jet_kinetic_power_at(double throttle) const;
    [[nodiscard]] double waste_power_at(double throttle) const;

    // Tsiolkovsky, both directions.  These are PLANNING helpers: the actual burn
    // is integrated, not applied (docs/architecture/navigation.md section 2).
    [[nodiscard]] double delta_v_for_mass_ratio(double mass_before, double mass_after) const;
    [[nodiscard]] double propellant_for_delta_v(double mass_before, double delta_v) const;
    [[nodiscard]] double burn_duration_for_delta_v(double mass_before, double delta_v,
                                                   double throttle = 1.0) const;

    [[nodiscard]] std::string describe() const;

private:
    std::string name_;
    double max_mass_flow_{0.0};
    double fraction_c_{0.0};
    double exhaust_velocity_{0.0};
    double efficiency_{0.0};
    double inverse_gamma_w_{1.0};
};

// One power plant, several operating points.
//
// A real variable-specific-impulse engine (VASIMR and its relatives) does not
// choose thrust and exhaust velocity independently: it has a reactor of a given
// power, and it trades one against the other. Holding the converted power fixed,
//
//     P = q c^2 (1 - eta/gamma_w)     is the same in every mode
//
// so raising w forces q down, and the thrust F = eta q w falls with it. That is
// the whole physics of "two modes": there is no setting that is better at both.
//
// The equal-power invariant is CHECKED, not assumed. Two operating points that
// draw different power are not one engine with a switch -- they are two engines,
// and the constructor says so.
//
// A ship may carry more than one power plant behind the same nozzle, and then
// each mode names the plant it runs on (`plant`, "main" unless said). The
// invariant holds WITHIN a plant: IMPULSE and CRUISE share the fusion plant and
// must draw the same power; a mode on a second plant draws that plant's power,
// and saying which plant it is is what stops a mode from conjuring power.
// See docs/physics/propulsion-model.md sections 4.5 and 4.7.
class MultiModeEngine {
public:
    struct Mode {
        std::string name;
        EngineSpec spec;
        std::string plant{"main"};
    };

    // Throws std::invalid_argument if two modes of the same plant draw different
    // converted power, if there are none, or if two share a name.
    explicit MultiModeEngine(std::vector<Mode> modes, std::size_t initial = 0);

    // Convenience for the common case of an engine with a single setting.
    explicit MultiModeEngine(EngineSpec spec);

    static MultiModeEngine from_json(const config::json::Value& value, const std::string& context);

    [[nodiscard]] const EngineSpec& current() const { return modes_.at(selected_).spec; }
    [[nodiscard]] const std::string& current_mode() const { return modes_.at(selected_).name; }
    [[nodiscard]] std::size_t selected() const noexcept { return selected_; }
    [[nodiscard]] const std::vector<Mode>& modes() const noexcept { return modes_; }
    [[nodiscard]] std::size_t size() const noexcept { return modes_.size(); }

    // Switching is a step change in thrust and mass flow, so it belongs BETWEEN
    // integration steps -- the same rule as an ignition
    // (docs/architecture/navigation.md section 4). A cockpit changing it once per
    // frame satisfies that naturally.
    void select(std::size_t index);
    bool select(const std::string& name);
    void cycle();

    // The converted power of the plant the current mode runs on [W].
    [[nodiscard]] double power() const;
    // The converted power of a named plant [W]; throws if no mode runs on it.
    [[nodiscard]] double power(const std::string& plant) const;
    [[nodiscard]] const std::string& current_plant() const { return modes_.at(selected_).plant; }

    [[nodiscard]] std::string describe() const;

private:
    void validate() const;

    std::vector<Mode> modes_;
    std::size_t selected_{0};
};

}  // namespace sf::propulsion
