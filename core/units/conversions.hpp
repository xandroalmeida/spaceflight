#pragma once

// Unit conversions concentrated in one place.
//
// The core speaks SI everywhere.  CSPICE speaks kilometres.  That boundary is
// crossed exactly once, inside core/ephemeris/spice_ephemeris_provider.cpp, using
// the helpers below -- never by a bare `* 1000.0` scattered through the code.

#include "core/math/vec3.hpp"
#include "core/units/constants.hpp"

namespace sf::units {

inline constexpr double km_to_m(double km) { return km * 1000.0; }
inline constexpr double m_to_km(double m) { return m * 0.001; }

inline constexpr math::Vec3 km_to_m(const math::Vec3& v) { return v * 1000.0; }
inline constexpr math::Vec3 m_to_km(const math::Vec3& v) { return v * 0.001; }

inline constexpr double deg_to_rad(double deg) { return deg * (pi / 180.0); }
inline constexpr double rad_to_deg(double rad) { return rad * (180.0 / pi); }

inline constexpr double au_to_m(double a) { return a * au; }
inline constexpr double m_to_au(double m) { return m / au; }

inline constexpr double days_to_seconds(double d) { return d * seconds_per_day; }
inline constexpr double seconds_to_days(double s) { return s / seconds_per_day; }

}  // namespace sf::units
