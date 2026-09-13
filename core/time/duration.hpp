#pragma once

// A time interval in SI seconds.
//
// Strong type on purpose: a `double` that means "seconds of coordinate time"
// and a `double` that means "an epoch" are different things, and mixing them
// silently is one of the classic ways to lose an orbit.

#include "core/units/constants.hpp"

#include <cmath>
#include <compare>

namespace sf::time {

class Duration {
public:
    constexpr Duration() = default;
    explicit constexpr Duration(double seconds) : seconds_(seconds) {}

    static constexpr Duration seconds(double s) { return Duration{s}; }
    static constexpr Duration minutes(double m) { return Duration{m * units::seconds_per_minute}; }
    static constexpr Duration hours(double h) { return Duration{h * units::seconds_per_hour}; }
    static constexpr Duration days(double d) { return Duration{d * units::seconds_per_day}; }
    static constexpr Duration julian_years(double y) { return Duration{y * units::seconds_per_julian_year}; }
    static constexpr Duration zero() { return Duration{0.0}; }

    [[nodiscard]] constexpr double seconds() const { return seconds_; }
    [[nodiscard]] constexpr double minutes() const { return seconds_ / units::seconds_per_minute; }
    [[nodiscard]] constexpr double hours() const { return seconds_ / units::seconds_per_hour; }
    [[nodiscard]] constexpr double days() const { return seconds_ / units::seconds_per_day; }

    [[nodiscard]] constexpr Duration abs() const { return Duration{seconds_ < 0.0 ? -seconds_ : seconds_}; }
    [[nodiscard]] constexpr int sign() const { return seconds_ > 0.0 ? 1 : (seconds_ < 0.0 ? -1 : 0); }
    [[nodiscard]] bool is_finite() const { return std::isfinite(seconds_); }

    constexpr Duration& operator+=(Duration o) { seconds_ += o.seconds_; return *this; }
    constexpr Duration& operator-=(Duration o) { seconds_ -= o.seconds_; return *this; }
    constexpr Duration& operator*=(double s) { seconds_ *= s; return *this; }
    constexpr Duration& operator/=(double s) { seconds_ /= s; return *this; }

    friend constexpr Duration operator+(Duration a, Duration b) { return a += b; }
    friend constexpr Duration operator-(Duration a, Duration b) { return a -= b; }
    friend constexpr Duration operator-(Duration a) { return Duration{-a.seconds_}; }
    friend constexpr Duration operator*(Duration a, double s) { return a *= s; }
    friend constexpr Duration operator*(double s, Duration a) { return a *= s; }
    friend constexpr Duration operator/(Duration a, double s) { return a /= s; }
    friend constexpr double operator/(Duration a, Duration b) { return a.seconds_ / b.seconds_; }

    friend constexpr auto operator<=>(Duration a, Duration b) = default;

private:
    double seconds_{0.0};
};

}  // namespace sf::time
