#pragma once

// The structural limit on brightness (rule 13).
//
// Beaming is D^4, which at beta = 0.9048 is 400 and at beta = 0.99 is 39601.
// The display has two decades.  The forbidden answer is
//
//     if (brightness > 1.0) brightness = 1.0;
//
// which is the same shape as the `if (v > c) v = c` that rule 13 rules out, and
// wrong for the same reason: it destroys the ordering.  Two stars four decades
// apart both become white, and the image stops carrying the information the
// physics produced.
//
// The right answer is that a real detector SATURATES, and its response is
// measured: Naka & Rushton (1966), J. Physiol. 185, 536-555, fitted the response
// of vertebrate photoreceptors as
//
//     R / R_max = L^n / (L^n + L_half^n)
//
// which is a bijection [0, inf) -> [0, 1): monotone, smooth, branch-free, and
// order-preserving.  Nothing is cut off; the scale is compressed.  The limit is
// the sensor, which is a structure, not a condition.
//
// See docs/physics/relativistic-rendering.md section 10.4.

#include <cmath>
#include <stdexcept>

namespace sf::render {

// The exposure: the luminance that maps to half scale.  A PRESENTATION
// parameter, named and explicit like `body_scale_exaggeration`
// (docs/architecture/rendering.md section 3) rather than a number hidden in a
// shader.  The default puts a magnitude 2 star -- an ordinary constellation
// star -- at mid scale.
inline constexpr double kDefaultHalfSaturation = 0.15848931924611134;  // 10^(-0.4 * 2)

// n = 1 is the Michaelis-Menten form and the one Naka-Rushton reduces to for
// rods; cones measure nearer 0.7-1.0.  Kept as a parameter rather than folded
// away, because it is the knob that says how hard the scale compresses.
inline constexpr double kDefaultResponseExponent = 1.0;

[[nodiscard]] inline double detector_response(double luminance,
                                              double half_saturation = kDefaultHalfSaturation,
                                              double exponent = kDefaultResponseExponent) {
    if (!(half_saturation > 0.0)) {
        throw std::invalid_argument("detector_response: half saturation must be > 0");
    }
    // Negative luminance is not dim, it is meaningless; it can only arrive from a
    // caller error, and returning 0 would hide it.
    if (!(luminance >= 0.0)) {
        throw std::invalid_argument("detector_response: luminance must be >= 0");
    }
    if (luminance == 0.0) {
        return 0.0;
    }
    const double l = exponent == 1.0 ? luminance : std::pow(luminance, exponent);
    const double h = exponent == 1.0 ? half_saturation : std::pow(half_saturation, exponent);
    return l / (l + h);
}

// The same curve from a LOGARITHMIC luminance, which is how the beaming arrives
// (core/render/blackbody.hpp: ln_band_limited_beaming).  Worth its own function
// because exp(ln_luminance) alone underflows to zero at ln = -270 -- the aft sky
// at beta = 0.99 -- while this form stays exact there:
//
//     L / (L + H) = 1 / (1 + exp(ln H - ln L))
//
// which for a very dim source is exp(ln L - ln H), evaluated without ever
// forming L.
[[nodiscard]] inline double detector_response_from_ln(
    double ln_luminance, double half_saturation = kDefaultHalfSaturation) {
    if (!(half_saturation > 0.0)) {
        throw std::invalid_argument("detector_response_from_ln: half saturation must be > 0");
    }
    const double delta = std::log(half_saturation) - ln_luminance;
    // exp() overflows to +inf for delta > 709, and 1/(1+inf) = 0 is exactly the
    // right answer for a source that dim.  The IEEE result IS the physics here.
    return 1.0 / (1.0 + std::exp(delta));
}

// Magnitude to relative flux, the definition of the magnitude scale:
// five magnitudes is a factor of 100, so one magnitude is 100^(1/5).
[[nodiscard]] inline double flux_from_magnitude(double magnitude) {
    return std::pow(10.0, -0.4 * magnitude);
}

[[nodiscard]] inline double magnitude_from_flux(double flux) {
    return -2.5 * std::log10(flux);
}

}  // namespace sf::render
