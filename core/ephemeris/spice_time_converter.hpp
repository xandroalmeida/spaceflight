#pragma once

// Calendar <-> coordinate time conversions.
//
// Leap seconds are data (naif0012.tls), not a table we maintain, so every
// UTC-facing conversion goes through SPICE.  This is the reason the converter
// lives under core/ephemeris and not under core/time: core/time must stay free
// of external dependencies.

#include "core/ephemeris/spice_kernel_set.hpp"
#include "core/time/coordinate_time.hpp"
#include "core/time/time_scale.hpp"

#include <memory>
#include <string>

namespace sf::ephemeris {

class SpiceTimeConverter {
public:
    // Requires a kernel set containing a leapseconds kernel.
    explicit SpiceTimeConverter(std::shared_ptr<const SpiceKernelSet> kernels);

    // Accepts everything str2et_c does: "2026-01-01", "2026-01-01T00:00:00",
    // "2026 JAN 1 12:00:00 TDB", "JD 2451545.0".  A bare timestamp with no scale
    // is interpreted as UTC, matching SPICE.
    [[nodiscard]] time::CoordinateTime parse(const std::string& text) const;

    // ISO-8601 "YYYY-MM-DDTHH:MM:SS.sss"; `decimals` digits of second.
    [[nodiscard]] std::string to_utc_string(time::CoordinateTime t, int decimals = 3) const;
    [[nodiscard]] std::string to_tdb_string(time::CoordinateTime t, int decimals = 6) const;

    // Seconds of the given scale, expressed relative to the same J2000 epoch.
    // Useful for reporting TDB-TT and similar offsets.
    [[nodiscard]] double seconds_in_scale(time::CoordinateTime t, time::TimeScale scale) const;

private:
    std::shared_ptr<const SpiceKernelSet> kernels_;
};

}  // namespace sf::ephemeris
