#pragma once

// Error types crossing the SPICE boundary.
//
// CSPICE signals failure through global state and, by default, prints to stdout
// and aborts the process.  Both behaviours are unacceptable in a library: the
// toolkit is switched to RETURN mode and every call site converts failure into
// one of the exceptions below.  See ADR-0003.

#include <stdexcept>
#include <string>

namespace sf::ephemeris {

class SpaceflightError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A SPICE call failed.  Carries the toolkit's short message (e.g.
// "SPICE(SPKINSUFFDATA)") separately, because that is what callers can branch on.
class SpiceError : public SpaceflightError {
public:
    SpiceError(std::string short_message, std::string long_message, std::string context)
        : SpaceflightError(context + ": " + short_message + " -- " + long_message),
          short_message_(std::move(short_message)),
          long_message_(std::move(long_message)),
          context_(std::move(context)) {}

    [[nodiscard]] const std::string& short_message() const noexcept { return short_message_; }
    [[nodiscard]] const std::string& long_message() const noexcept { return long_message_; }
    [[nodiscard]] const std::string& context() const noexcept { return context_; }

private:
    std::string short_message_;
    std::string long_message_;
    std::string context_;
};

// The requested body/epoch is outside the loaded data.  Distinct from SpiceError
// because it is an expected, recoverable condition (asking for Mars in 1600 with
// de440s loaded), not a defect.  Never answered by extrapolation.
class EphemerisUnavailable : public SpaceflightError {
public:
    using SpaceflightError::SpaceflightError;
};

class KernelLoadError : public SpaceflightError {
public:
    using SpaceflightError::SpaceflightError;
};

}  // namespace sf::ephemeris
