#pragma once

// What colour a black body of temperature T is, and how much of its power lands
// in the band an eye can see.
//
// This is colorimetry, not relativity -- but it is what makes the relativity
// visible.  A Doppler-shifted black body is still a black body at T' = D T
// (docs/physics/relativistic-rendering.md section 4), so the entire colour part
// of the milestone reduces to: given T, what colour, and how bright IN BAND.
//
// Two results here carry the design:
//
//   1. The chromaticity CONVERGES as T -> infinity (the Rayleigh-Jeans limit,
//      x = 0.2401, y = 0.2340).  That is what lets the lookup table be indexed
//      by a bijection [0, inf) -> [0, 1) instead of a clamped range (rule 13).
//   2. The band efficiency eta(T) spans e^-17000 to e^-2, so it is carried in
//      LOGARITHM everywhere -- and the Planck integral itself is evaluated in
//      log space, or a 12 K black body underflows to 0/0.
//
// No Godot, no engine, no file formats: this produces numbers, and something
// else turns them into a texture (docs/architecture/relativistic-shaders.md).

#include <cstddef>
#include <vector>

namespace sf::render {

// Linear sRGB, NOT gamma-encoded, and not necessarily inside [0,1]: a saturated
// spectral colour is outside the sRGB gamut and says so with a negative
// component.  Clipping that is a display decision, taken later and elsewhere.
struct LinearRgb {
    double r{0.0};
    double g{0.0};
    double b{0.0};
};

struct CieXyz {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

// CIE 1931 2-degree colour matching functions, by the analytic multi-lobe fit of
// Wyman, Sloan & Shirley (2013), JCGT 2(2) 1-11.  Stated accuracy ~1% of peak;
// the consequence for us is measured against the published Planckian locus in
// tests/scientific/test_blackbody_colour.cpp and is better than 1.1e-3 in x,y
// above 3000 K.
[[nodiscard]] double cie_x_bar(double wavelength_nm);
[[nodiscard]] double cie_y_bar(double wavelength_nm);
[[nodiscard]] double cie_z_bar(double wavelength_nm);

// Planck's law, natural log of the spectral radiance [W / m^3 / sr].
//
// In LOG because the whole point is to survive the cold end: at T = 12 K the
// radiance at 550 nm is e^-2236, which is not a double.  The log form has no
// such problem and no special case -- ln(exp(x) - 1) is just x once x is large.
[[nodiscard]] double ln_planck_radiance(double wavelength_m, double temperature);

// Everything about one temperature, computed in one pass over the band.
struct BlackbodySample {
    CieXyz xyz{};             // on an ARBITRARY common scale (see ln_scale)
    LinearRgb rgb{};          // chromaticity-normalised: max component is 1
    double chromaticity_x{0.0};
    double chromaticity_y{0.0};

    // ln of the band efficiency: ln( photopic radiance / bolometric radiance ).
    // Dimensionless, always negative, and the reason it is a log is that it runs
    // from -17325 (T = 1 K) to -2.0 (T = 5772 K).
    double ln_band_efficiency{0.0};
};

// The integration band.  360-830 nm is the CIE tabulation range; outside it the
// colour matching functions are zero by definition, so this is the whole visible
// band and not a truncation.
inline constexpr double kBandLowNm = 360.0;
inline constexpr double kBandHighNm = 830.0;
inline constexpr int kBandSamples = 470;   // 1 nm steps, trapezoid

[[nodiscard]] BlackbodySample blackbody_sample(double temperature);

// CIE XYZ -> linear sRGB (IEC 61966-2-1, D65 primaries).
[[nodiscard]] LinearRgb xyz_to_linear_srgb(const CieXyz& xyz);

// ---------------------------------------------------------------------------
// The lookup table the shader samples.
//
// Index: u = T / (T + kTableReferenceTemperature).  A bijection [0, inf) -> [0,
// 1): no range, no clamp, no branch -- the structural limit that rule 13 asks
// for.  It loses resolution only as u -> 1, which is exactly where the
// chromaticity stops changing (section 9.2).
// ---------------------------------------------------------------------------

inline constexpr double kTableReferenceTemperature = 6000.0;   // [K]

[[nodiscard]] constexpr double temperature_to_table_index(double temperature) {
    return temperature / (temperature + kTableReferenceTemperature);
}

[[nodiscard]] constexpr double table_index_to_temperature(double u) {
    return kTableReferenceTemperature * u / (1.0 - u);
}

struct PlanckTable {
    // Four floats per entry: r, g, b, ln(eta).  Laid out for direct upload as a
    // width x 1 RGBA float texture.
    std::vector<float> texels;
    std::size_t width{0};

    [[nodiscard]] std::size_t byte_size() const { return texels.size() * sizeof(float); }

    // What the shader does, in C++, so that a test can check the two agree on the
    // quantity that matters rather than on the source text.
    [[nodiscard]] LinearRgb sample_rgb(double temperature) const;
    [[nodiscard]] double sample_ln_band_efficiency(double temperature) const;
};

// 1024 entries gives 8.7 K per texel at 1297 K, 45 K at 5800 K and 332 K at
// 25944 K -- everywhere far below the scale on which the chromaticity moves.
[[nodiscard]] PlanckTable build_planck_table(std::size_t width = 1024);

// The band-limited beaming factor of section 10.1:
//
//     F'_V / F_V  =  D^4 * eta(D T) / eta(T)
//
// Returned as a LOGARITHM because the aft sky at beta = 0.99 is e^-52 and the
// forward sky is e^5.5 -- and because the log turns the ratio of two eta into a
// subtraction, which is the whole reason eta is tabulated in log.
[[nodiscard]] double ln_band_limited_beaming(const PlanckTable& table, double rest_temperature,
                                             double doppler);

}  // namespace sf::render
