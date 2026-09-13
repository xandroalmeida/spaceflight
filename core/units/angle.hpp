#pragma once

// Strong angular quantity for public interfaces where a bare double could be
// either radians or degrees. Internal trigonometry still consumes radians.

#include "core/units/constants.hpp"

namespace sf::units {

class Angle {
public:
    static constexpr Angle radians(double value) { return Angle{value}; }
    static constexpr Angle degrees(double value) { return Angle{value * (pi / 180.0)}; }

    [[nodiscard]] constexpr double radians() const { return radians_; }
    [[nodiscard]] constexpr double degrees() const { return radians_ * (180.0 / pi); }

    friend constexpr bool operator==(const Angle&, const Angle&) = default;

private:
    explicit constexpr Angle(double radians) : radians_(radians) {}
    double radians_{0.0};
};

}  // namespace sf::units
