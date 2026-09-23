#pragma once

// Numbers as a person reads them (rule 74).
//
// The cockpit never shows `384000000.000 m`. Choosing the unit is this file's
// job, and it is stateless because the choice depends on nothing: the same
// distance gives the same string on every instrument.
//
// The technical read-out does NOT go through here on purpose. There the raw
// number with every digit is what is wanted, because it is what gets checked
// against a tolerance.
//
// The strings are the ones the Godot cockpit printed, character for character:
// the manual's captures read "400.0 km" and "1.0e-4", with the trailing zero, and
// so does this.

#include <string>

namespace sf::app::fmt {

inline constexpr double AU_M = 1.495978707e11;
inline constexpr double C_MS = 299792458.0;

// printf("%.*f") -- the fixed-point form, trailing zeros kept.
[[nodiscard]] std::string fixed(double value, int decimals);
// The same, with Godot's String.num spellings for the non-finite values.
[[nodiscard]] std::string num(double value, int decimals);
// printf-style formatting into a std::string.
[[nodiscard]] std::string format(const char* pattern, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

// Significant digits, and plain decimal notation inside the range that reads
// without decoding: sixteen digits of a quantity of order 1e-13 are not
// precision, they are noise.
[[nodiscard]] std::string sci(double value, int digits = 4);

// m -> km -> AU. The step to AU is at 0.01 AU (1.5 million km).
[[nodiscard]] std::string distance(double metres);
// m/s -> km/s -> fraction of c. The step to c is at 0.001 c (300 km/s).
[[nodiscard]] std::string speed(double ms);
// A duration from seconds to years without changing instrument; above two
// hours it is TWO units -- `92d 14h`, not `92.58 d` (rule 96).
[[nodiscard]] std::string duration(double seconds);
// A large count, readable at a glance: `213k`, `2.4M`.
[[nodiscard]] std::string count(long long value);
// Mission countdown: `T-01:42:17`. Days appear only when they exist.
[[nodiscard]] std::string countdown(double seconds);
[[nodiscard]] std::string mass(double kg);
[[nodiscard]] std::string force(double newtons);
[[nodiscard]] std::string percent(double fraction);
[[nodiscard]] std::string angle(double degrees);
// Warp with a thousands separator, because `100000x` and `1000000x` are
// indistinguishable at a glance and that is exactly where the difference matters.
[[nodiscard]] std::string warp(double factor);

// Thousands grouped with a space: groups without turning into a comma.
[[nodiscard]] std::string group(double value, int decimals);

// Text helpers the read-outs share.
[[nodiscard]] std::string upper(std::string text);
[[nodiscard]] std::string rpad(const std::string& text, std::size_t width);
[[nodiscard]] std::string on_off(bool value);
// Width in characters, counting a UTF-8 multi-byte sequence once.
[[nodiscard]] std::size_t display_width(const std::string& text);

}  // namespace sf::app::fmt
