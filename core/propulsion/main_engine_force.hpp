#pragma once

// The main engine under interactive control: thrust along the ship's nose, at
// whatever throttle the pilot is holding.
//
// ManeuverExecutor covers PLANNED burns -- ignition epoch, duration, guidance
// mode. This covers the other half: a hand on the throttle, pointing wherever
// the attitude happens to point. Both produce force the same way, from the same
// F = eta*q*w, out of the same tank.
//
// The thrust direction is the body +x axis rotated into the integration frame.
// That is the coupling that makes a cockpit a cockpit: you aim with the RCS and
// then burn, and if the attitude is wrong the burn goes the wrong way. Nothing
// here consults a guidance mode.

#include "core/gravity/force_model.hpp"
#include "core/spacecraft/spacecraft.hpp"

namespace sf::propulsion {

class MainEngineForce final : public gravity::ForceModel {
public:
    // `craft` must outlive the force model.
    explicit MainEngineForce(const spacecraft::Spacecraft& craft);

    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState& state,
                                                time::CoordinateTime t) const override;
    [[nodiscard]] std::string_view name() const override { return "MainEngineForce"; }

    // Clamped to [0, 1]. Changing it mid-propagation is a discontinuity in the
    // derivative, so the caller should change it between steps -- which is what
    // a per-frame cockpit does naturally.
    void set_throttle(double throttle);
    [[nodiscard]] double throttle() const noexcept { return throttle_; }

    [[nodiscard]] double current_thrust() const;
    [[nodiscard]] const spacecraft::Spacecraft& craft() const noexcept { return craft_; }

private:
    const spacecraft::Spacecraft& craft_;
    double throttle_{0.0};
};

}  // namespace sf::propulsion
