#pragma once

// Coordinate time of the simulation: TDB seconds since the J2000 epoch
// (2000-01-01 12:00:00 TDB), stored in two parts.
//
// Why two parts: a single double holding ~8.4e8 s (year 2026) resolves only
// ~1.9e-7 s.  At 0.99c that is 56 m of position error per time ulp.  Splitting
// into an exactly-representable integer part plus a residual keeps the
// resolution near 1e-16 s at any epoch.
// See docs/architecture/coordinate-system.md section 5.

#include "core/time/duration.hpp"

#include <cmath>
#include <compare>
#include <string>

namespace sf::time {

class CoordinateTime {
public:
    constexpr CoordinateTime() = default;

    // Convenience for epochs where full resolution is not needed (kernel
    // boundaries, user input already limited to microseconds, ...).
    static CoordinateTime from_seconds_since_j2000(double seconds) {
        return normalized(std::trunc(seconds), seconds - std::trunc(seconds));
    }

    static CoordinateTime from_parts(double whole_seconds, double fractional_seconds) {
        return normalized(whole_seconds, fractional_seconds);
    }

    static CoordinateTime from_julian_date_tdb(double jd) {
        const double days_from_j2000 = jd - units::julian_date_j2000;
        const double whole_days = std::trunc(days_from_j2000);
        // 86400 is a power-of-two multiple of 675, so whole_days*86400 is exact.
        return normalized(whole_days * units::seconds_per_day,
                          (days_from_j2000 - whole_days) * units::seconds_per_day);
    }

    static constexpr CoordinateTime j2000() { return CoordinateTime{}; }

    // Collapsed value.  Loses the extra resolution; use only where the consumer
    // cannot take more (CSPICE ET, printing, coarse comparisons).
    [[nodiscard]] constexpr double seconds_since_j2000() const { return whole_ + frac_; }
    [[nodiscard]] constexpr double whole_seconds() const { return whole_; }
    [[nodiscard]] constexpr double fractional_seconds() const { return frac_; }

    [[nodiscard]] constexpr double julian_date_tdb() const {
        return units::julian_date_j2000 + whole_ / units::seconds_per_day + frac_ / units::seconds_per_day;
    }

    [[nodiscard]] bool is_finite() const { return std::isfinite(whole_) && std::isfinite(frac_); }

    // Exact difference: the whole parts cancel exactly (both are integers below
    // 2^53), so small differences between distant epochs keep full precision.
    [[nodiscard]] constexpr Duration operator-(const CoordinateTime& o) const {
        return Duration{(whole_ - o.whole_) + (frac_ - o.frac_)};
    }

    [[nodiscard]] CoordinateTime operator+(Duration d) const { return add(d.seconds()); }
    [[nodiscard]] CoordinateTime operator-(Duration d) const { return add(-d.seconds()); }

    CoordinateTime& operator+=(Duration d) { *this = add(d.seconds()); return *this; }
    CoordinateTime& operator-=(Duration d) { *this = add(-d.seconds()); return *this; }

    friend constexpr bool operator==(const CoordinateTime& a, const CoordinateTime& b) {
        return a.whole_ == b.whole_ && a.frac_ == b.frac_;
    }
    friend constexpr std::strong_ordering operator<=>(const CoordinateTime& a, const CoordinateTime& b) {
        if (a.whole_ != b.whole_) {
            return a.whole_ < b.whole_ ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        if (a.frac_ != b.frac_) {
            return a.frac_ < b.frac_ ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        return std::strong_ordering::equal;
    }

    // "J2000 + 8.2e8 s (TDB)" -- calendar formatting needs leap second data and
    // therefore lives in core/ephemeris/spice_time_converter.hpp.
    [[nodiscard]] std::string to_string() const;

private:
    constexpr CoordinateTime(double whole, double frac) : whole_(whole), frac_(frac) {}

    static CoordinateTime normalized(double whole, double frac) {
        const double carry = std::nearbyint(frac);
        return CoordinateTime{whole + carry, frac - carry};
    }

    [[nodiscard]] CoordinateTime add(double seconds) const {
        // Split the increment so that the integer part lands in the exact
        // accumulator and only the residual goes through the compensated sum.
        const double inc_whole = std::trunc(seconds);
        const double inc_frac = seconds - inc_whole;

        // two-sum (Knuth): s is the rounded sum, err the exact rounding residual.
        const double s = frac_ + inc_frac;
        const double b = s - frac_;
        const double err = (frac_ - (s - b)) + (inc_frac - b);

        const double carry = std::nearbyint(s);
        return CoordinateTime{whole_ + inc_whole + carry, (s - carry) + err};
    }

    double whole_{0.0};  // integral TDB seconds since J2000 (exact up to 2^53)
    double frac_{0.0};   // residual, |frac_| <= 0.5
};

}  // namespace sf::time
