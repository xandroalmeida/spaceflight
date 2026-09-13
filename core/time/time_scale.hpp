#pragma once

#include <string_view>

namespace sf::time {

// Time scales that appear at the boundaries of the system.  The simulation's
// coordinate time is always TDB; the others exist for input and display.
// See docs/architecture/coordinate-system.md section 4.
enum class TimeScale {
    TDB,  // Barycentric Dynamical Time -- SPICE "ET", the integration variable
    TT,   // Terrestrial Time
    TAI,  // International Atomic Time
    UTC   // Coordinated Universal Time -- leap seconds, human I/O only
};

constexpr std::string_view to_string(TimeScale s) {
    switch (s) {
        case TimeScale::TDB: return "TDB";
        case TimeScale::TT:  return "TT";
        case TimeScale::TAI: return "TAI";
        case TimeScale::UTC: return "UTC";
    }
    return "?";
}

}  // namespace sf::time
