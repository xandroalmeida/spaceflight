#include "core/ephemeris/spice_internal.hpp"

#include "core/ephemeris/errors.hpp"

extern "C" {
#include "SpiceUsr.h"
}

#include <array>

namespace sf::ephemeris::detail {

std::mutex& spice_mutex() {
    static std::mutex m;
    return m;
}

void ensure_spice_error_handling() {
    static const bool configured = [] {
        // RETURN: the toolkit records the error and becomes a no-op until reset,
        // instead of calling exit().  NULL device: nothing is written to stdout.
        erract_c(const_cast<char*>("SET"), 0, const_cast<char*>("RETURN"));
        errdev_c(const_cast<char*>("SET"), 0, const_cast<char*>("NULL"));
        errprt_c(const_cast<char*>("SET"), 0, const_cast<char*>("NONE"));
        return true;
    }();
    (void)configured;
}

void throw_if_spice_failed(const std::string& context) {
    if (!failed_c()) {
        return;
    }

    std::array<SpiceChar, 64> short_msg{};
    std::array<SpiceChar, 1841> long_msg{};
    getmsg_c(const_cast<char*>("SHORT"), static_cast<SpiceInt>(short_msg.size()), short_msg.data());
    getmsg_c(const_cast<char*>("LONG"), static_cast<SpiceInt>(long_msg.size()), long_msg.data());
    reset_c();

    const std::string short_str{short_msg.data()};
    const std::string long_str{long_msg.data()};

    // Missing coverage is a normal, recoverable answer ("we have no data there"),
    // not a defect.  Everything else is a real SPICE error.
    if (short_str.find("INSUFFDATA") != std::string::npos ||
        short_str.find("NOFRAMECONNECT") != std::string::npos ||
        short_str.find("SPKINSUFFDATA") != std::string::npos ||
        short_str.find("NOLOADEDFILES") != std::string::npos) {
        throw EphemerisUnavailable(context + ": " + short_str + " -- " + long_str);
    }

    throw SpiceError(short_str, long_str, context);
}

}  // namespace sf::ephemeris::detail
