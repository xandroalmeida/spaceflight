#pragma once

// Physical and astronomical constants, SI, with provenance.
//
// Rule: a constant that a SPICE kernel provides (GM, body radii, flattening) is
// NOT duplicated here.  Duplicating GM would let the mass used in our dynamics
// drift away from the mass used to generate the ephemeris we integrate against.

#include <numbers>

namespace sf::units {

// Exact by definition (SI, 2019 redefinition).
inline constexpr double c = 299792458.0;                  // [m/s] speed of light
inline constexpr double c_squared = c * c;                // [m^2/s^2]

// CODATA 2018.  Relative standard uncertainty 2.2e-5 -- by far the least
// precisely known constant in this simulator.  We never use G alone: the
// ephemerides provide GM directly, which is known to ~1e-10 relative.
inline constexpr double G = 6.67430e-11;                  // [m^3 kg^-1 s^-2]

// Exact by definition (CGPM 1901), used only to express specific impulse.
inline constexpr double g0 = 9.80665;                     // [m/s^2]

// IAU 2012 Resolution B2: the astronomical unit is exactly 149597870700 m.
inline constexpr double au = 149597870700.0;              // [m]

// Time
inline constexpr double seconds_per_minute = 60.0;
inline constexpr double seconds_per_hour = 3600.0;
inline constexpr double seconds_per_day = 86400.0;        // SI day, exact
inline constexpr double seconds_per_julian_year = 365.25 * seconds_per_day;
inline constexpr double julian_date_j2000 = 2451545.0;    // JD (TDB) of the J2000 epoch

// TT - TAI is exact by definition.
inline constexpr double tt_minus_tai = 32.184;            // [s]

inline constexpr double pi = std::numbers::pi;
inline constexpr double two_pi = 2.0 * std::numbers::pi;

}  // namespace sf::units
