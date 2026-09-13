#include "core/ephemeris/spice_time_converter.hpp"

#include "core/ephemeris/errors.hpp"
#include "core/ephemeris/spice_internal.hpp"

extern "C" {
#include "SpiceUsr.h"
}

#include <array>
#include <algorithm>
#include <string>

namespace sf::ephemeris {

SpiceTimeConverter::SpiceTimeConverter(std::shared_ptr<const SpiceKernelSet> kernels)
    : kernels_(std::move(kernels)) {
    if (!kernels_) {
        throw KernelLoadError("SpiceTimeConverter requires a loaded kernel set");
    }
    if (!kernels_->has_leapseconds()) {
        throw KernelLoadError("no leapseconds kernel (*.tls) loaded; UTC conversion impossible");
    }
    detail::ensure_spice_error_handling();
}

time::CoordinateTime SpiceTimeConverter::parse(const std::string& text) const {
    SpiceDouble et = 0.0;
    {
        const std::lock_guard lock{detail::spice_mutex()};
        str2et_c(text.c_str(), &et);
        detail::throw_if_spice_failed("str2et(\"" + text + "\")");
    }
    return time::CoordinateTime::from_seconds_since_j2000(et);
}

std::string SpiceTimeConverter::to_utc_string(time::CoordinateTime t, int decimals) const {
    std::array<SpiceChar, 128> buffer{};
    const SpiceInt prec = static_cast<SpiceInt>(std::clamp(decimals, 0, 9));
    {
        const std::lock_guard lock{detail::spice_mutex()};
        et2utc_c(t.seconds_since_j2000(), "ISOC", prec,
                 static_cast<SpiceInt>(buffer.size()), buffer.data());
        detail::throw_if_spice_failed("et2utc");
    }
    return std::string{buffer.data()};
}

std::string SpiceTimeConverter::to_tdb_string(time::CoordinateTime t, int decimals) const {
    const int d = std::clamp(decimals, 0, 9);
    std::string picture = "YYYY-MM-DDTHR:MN:SC";
    if (d > 0) {
        picture += "." + std::string(static_cast<std::size_t>(d), '#');
    }
    picture += " ::TDB";

    std::array<SpiceChar, 128> buffer{};
    {
        const std::lock_guard lock{detail::spice_mutex()};
        timout_c(t.seconds_since_j2000(), picture.c_str(),
                 static_cast<SpiceInt>(buffer.size()), buffer.data());
        detail::throw_if_spice_failed("timout");
    }
    return std::string{buffer.data()};
}

double SpiceTimeConverter::seconds_in_scale(time::CoordinateTime t, time::TimeScale scale) const {
    if (scale == time::TimeScale::TDB) {
        return t.seconds_since_j2000();
    }

    // UTC is not a uniform scale, so unitim_c does not handle it; deltet_c
    // returns ET-UTC (leap seconds plus the TT-TAI offset plus the periodic term)
    // from the leapseconds kernel.
    if (scale == time::TimeScale::UTC) {
        SpiceDouble delta = 0.0;
        const std::lock_guard lock{detail::spice_mutex()};
        deltet_c(t.seconds_since_j2000(), "ET", &delta);
        detail::throw_if_spice_failed("deltet(ET -> UTC)");
        return t.seconds_since_j2000() - delta;
    }

    // "TDT" is the toolkit's name for Terrestrial Time.
    const char* target = scale == time::TimeScale::TT ? "TDT" : "TAI";

    SpiceDouble converted = 0.0;
    {
        const std::lock_guard lock{detail::spice_mutex()};
        converted = unitim_c(t.seconds_since_j2000(), "TDB", target);
        detail::throw_if_spice_failed("unitim(TDB -> " + std::string{target} + ")");
    }
    return converted;
}

}  // namespace sf::ephemeris
