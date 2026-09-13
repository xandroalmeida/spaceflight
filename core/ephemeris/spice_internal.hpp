#pragma once

// Shared plumbing for the CSPICE boundary.  Included only by files in
// core/ephemeris that actually talk to the toolkit; it does not pull in
// SpiceUsr.h itself, so no public header ever sees the C API.

#include <mutex>
#include <string>

namespace sf::ephemeris::detail {

// CSPICE is not thread safe (global kernel pool, global error state).  Every
// toolkit call in this project is made while holding this mutex.
std::mutex& spice_mutex();

// Puts the toolkit in RETURN error mode with output suppressed.  Idempotent.
void ensure_spice_error_handling();

// If the toolkit is in a failed state, resets it and throws SpiceError (or
// EphemerisUnavailable for the "insufficient data" family).  Call with the mutex
// held, immediately after the toolkit call.
void throw_if_spice_failed(const std::string& context);

}  // namespace sf::ephemeris::detail
