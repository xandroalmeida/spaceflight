#include "core/navigation/trajectory_planner.hpp"

#include "core/units/constants.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace sf::navigation {
namespace {

void require_positive(double value, const char* what) {
    if (!(value > 0.0) || !std::isfinite(value)) {
        throw std::invalid_argument(std::string{what} + " must be finite and > 0");
    }
}

GuidanceMode reversed(GuidanceMode mode) {
    switch (mode) {
        case GuidanceMode::Prograde:   return GuidanceMode::Retrograde;
        case GuidanceMode::Retrograde: return GuidanceMode::Prograde;
        case GuidanceMode::Normal:     return GuidanceMode::AntiNormal;
        case GuidanceMode::AntiNormal: return GuidanceMode::Normal;
        case GuidanceMode::RadialOut:  return GuidanceMode::RadialIn;
        case GuidanceMode::RadialIn:   return GuidanceMode::RadialOut;
        case GuidanceMode::Inertial:   return GuidanceMode::Inertial;
    }
    return mode;
}

}  // namespace

HohmannTransfer plan_hohmann(double gm, double r1, double r2) {
    require_positive(gm, "gm");
    require_positive(r1, "r1");
    require_positive(r2, "r2");

    HohmannTransfer transfer{};
    transfer.transfer_semi_major_axis = 0.5 * (r1 + r2);

    const double a_t = transfer.transfer_semi_major_axis;
    transfer.delta_v_departure = std::sqrt(gm / r1) * (std::sqrt(2.0 * r2 / (r1 + r2)) - 1.0);
    transfer.delta_v_arrival = std::sqrt(gm / r2) * (1.0 - std::sqrt(2.0 * r1 / (r1 + r2)));
    transfer.total_delta_v = std::abs(transfer.delta_v_departure) + std::abs(transfer.delta_v_arrival);
    transfer.transfer_time = units::pi * std::sqrt(a_t * a_t * a_t / gm);
    return transfer;
}

double delta_v_to_change_apsis(double gm, double r, double current_speed, double target_apsis) {
    require_positive(gm, "gm");
    require_positive(r, "r");
    require_positive(target_apsis, "target_apsis");

    // vis-viva with a = (r + target)/2
    const double a = 0.5 * (r + target_apsis);
    const double v_required_squared = gm * (2.0 / r - 1.0 / a);
    if (v_required_squared <= 0.0) {
        throw std::invalid_argument("delta_v_to_change_apsis: the requested apsis is unreachable "
                                    "from this radius (negative vis-viva)");
    }
    return std::sqrt(v_required_squared) - current_speed;
}

double delta_v_to_escape(double gm, double r, double current_speed) {
    require_positive(gm, "gm");
    require_positive(r, "r");
    return std::sqrt(2.0 * gm / r) - current_speed;
}

Maneuver maneuver_for_delta_v(const spacecraft::Spacecraft& craft, double mass_at_ignition,
                              double delta_v, time::CoordinateTime ignition,
                              GuidanceMode guidance, celestial::BodyId reference, double throttle,
                              std::string name, BurnCentering centering) {
    require_positive(mass_at_ignition, "mass_at_ignition");
    if (!(throttle > 0.0) || !(throttle <= 1.0)) {
        throw std::invalid_argument("maneuver_for_delta_v: throttle must be in (0, 1]");
    }

    const double magnitude = std::abs(delta_v);
    if (!(magnitude > 0.0)) {
        throw std::invalid_argument("maneuver_for_delta_v: delta_v must be non-zero");
    }

    const auto& engine = craft.engine();
    const double propellant_needed = engine.propellant_for_delta_v(mass_at_ignition, magnitude);
    const double propellant_available = craft.propellant_at(mass_at_ignition);

    if (propellant_needed > propellant_available) {
        std::ostringstream os;
        os << "maneuver \"" << name << "\" needs " << propellant_needed << " kg of propellant for "
           << magnitude << " m/s, but only " << propellant_available << " kg is left (short by "
           << (propellant_needed - propellant_available) << " kg). The ship's remaining budget is "
           << craft.delta_v_budget(mass_at_ignition) << " m/s";
        throw std::invalid_argument(os.str());
    }

    const double duration = propellant_needed / engine.mass_flow_at(throttle);

    Maneuver maneuver{};
    maneuver.name = std::move(name);
    maneuver.duration = time::Duration::seconds(duration);
    // Centring the burn on the planned instant means half of it happens before
    // and half after, which roughly halves the along-track error relative to the
    // impulsive plan.  It does not eliminate the gravity loss -- nothing does but
    // a shorter burn (docs/architecture/navigation.md section 2).
    maneuver.ignition = centering == BurnCentering::CenterOnIgnition
                            ? ignition - time::Duration::seconds(0.5 * duration)
                            : ignition;
    maneuver.throttle = throttle;
    maneuver.guidance = delta_v >= 0.0 ? guidance : reversed(guidance);
    maneuver.reference = reference;
    maneuver.validate();
    return maneuver;
}

}  // namespace sf::navigation
