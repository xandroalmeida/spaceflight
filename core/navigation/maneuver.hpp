#pragma once

// A maneuver is DATA: when to burn, for how long, at what throttle, pointing
// which way.  It never touches the spacecraft state -- the executor turns it into
// a force and the integrator does the rest.
// See docs/architecture/navigation.md section 1.

#include "core/celestial/body_id.hpp"
#include "core/math/vec3.hpp"
#include "core/time/coordinate_time.hpp"
#include "core/time/duration.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sf::navigation {

// The guidance modes that do not require an attitude model.  POINT_TARGET,
// MATCH_VELOCITY and friends (rule section 28) join them in Milestone 3, when
// AttitudeState exists -- pretending the hull has an orientation before then
// would be inventing state.
enum class GuidanceMode {
    Inertial,    // fixed direction in the integration frame
    Prograde,    // along +v relative to the reference body
    Retrograde,
    Normal,      // along +h = r x v
    AntiNormal,
    RadialOut,   // along +r from the reference body
    RadialIn
};

std::string_view to_string(GuidanceMode mode);
std::optional<GuidanceMode> guidance_from_string(std::string_view name);

struct Maneuver {
    std::string name{"burn"};
    time::CoordinateTime ignition{};
    time::Duration duration{};
    double throttle{1.0};
    GuidanceMode guidance{GuidanceMode::Prograde};
    celestial::BodyId reference{celestial::bodies::earth};
    math::Vec3 inertial_direction{1.0, 0.0, 0.0};

    [[nodiscard]] time::CoordinateTime cutoff() const { return ignition + duration; }

    // Half-open [ignition, cutoff): a maneuver that ends exactly when the next
    // begins must not have both active for one instant.
    [[nodiscard]] bool active_at(time::CoordinateTime t) const {
        return t >= ignition && t < cutoff();
    }

    void validate() const;  // throws std::invalid_argument
};

class ManeuverPlan {
public:
    ManeuverPlan() = default;

    // Keeps the plan sorted by ignition and refuses overlaps: two engines firing
    // in different directions at once is not a plan, it is a bug.
    ManeuverPlan& add(Maneuver maneuver);

    [[nodiscard]] const Maneuver* active_at(time::CoordinateTime t) const;
    [[nodiscard]] const std::vector<Maneuver>& maneuvers() const noexcept { return maneuvers_; }
    [[nodiscard]] bool empty() const noexcept { return maneuvers_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return maneuvers_.size(); }

    // Ignition and cutoff epochs strictly inside (from, to), in chronological
    // order.  The mission runner breaks the propagation at these instants so the
    // integrator never has to cross a discontinuity in the derivative
    // (docs/architecture/navigation.md section 4).
    [[nodiscard]] std::vector<time::CoordinateTime> switch_times(time::CoordinateTime from,
                                                                 time::CoordinateTime to) const;

    [[nodiscard]] time::Duration total_burn_time() const;
    [[nodiscard]] std::string describe() const;

private:
    std::vector<Maneuver> maneuvers_;
};

}  // namespace sf::navigation
