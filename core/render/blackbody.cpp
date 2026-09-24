#include "core/render/blackbody.hpp"

#include "core/units/constants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sf::render {
namespace {

// Planck / Boltzmann, SI, exact by the 2019 redefinition.  Not in
// core/units/constants.hpp because nothing in the DYNAMICS needs them: they
// belong to the image, and the constants header is deliberately about the state.
constexpr double kPlanckH = 6.62607015e-34;        // [J s]
constexpr double kBoltzmannK = 1.380649e-23;       // [J/K]
constexpr double kStefanBoltzmann = 5.670374419e-8;  // [W m^-2 K^-4]

// One lobe of the Wyman/Sloan/Shirley fit: a Gaussian with a different width on
// each side of the peak.
double piecewise_gaussian(double x, double mu, double sigma_low, double sigma_high) {
    const double sigma = x < mu ? sigma_low : sigma_high;
    const double t = (x - mu) / sigma;
    return std::exp(-0.5 * t * t);
}

}  // namespace

double cie_x_bar(double wavelength_nm) {
    return 1.056 * piecewise_gaussian(wavelength_nm, 599.8, 37.9, 31.0) +
           0.362 * piecewise_gaussian(wavelength_nm, 442.0, 16.0, 26.7) -
           0.065 * piecewise_gaussian(wavelength_nm, 501.1, 20.4, 26.2);
}

double cie_y_bar(double wavelength_nm) {
    return 0.821 * piecewise_gaussian(wavelength_nm, 568.8, 46.9, 40.5) +
           0.286 * piecewise_gaussian(wavelength_nm, 530.9, 16.3, 31.1);
}

double cie_z_bar(double wavelength_nm) {
    return 1.217 * piecewise_gaussian(wavelength_nm, 437.0, 11.8, 36.0) +
           0.681 * piecewise_gaussian(wavelength_nm, 459.0, 26.0, 13.8);
}

double ln_planck_radiance(double wavelength_m, double temperature) {
    const double x = kPlanckH * units::c / (wavelength_m * kBoltzmannK * temperature);
    // ln(e^x - 1).  For x > 30 the -1 is below the last bit of e^x, so the answer
    // IS x -- which is the Wien limit, and which is what keeps a 12 K black body
    // representable instead of underflowing to zero.
    const double ln_expm1 = x > 30.0 ? x : std::log(std::expm1(x));
    return std::log(2.0 * kPlanckH * units::c * units::c) - 5.0 * std::log(wavelength_m) -
           ln_expm1;
}

LinearRgb xyz_to_linear_srgb(const CieXyz& xyz) {
    // IEC 61966-2-1 sRGB primaries with a D65 white point.
    return LinearRgb{
        3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z,
        -0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z,
        0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z};
}

BlackbodySample blackbody_sample(double temperature) {
    if (!(temperature > 0.0) || !std::isfinite(temperature)) {
        throw std::invalid_argument("blackbody_sample: temperature must be finite and > 0");
    }

    // Pass 1: the log radiance at every sample, and its maximum.  Factoring the
    // maximum out is what makes the cold end survive: every term below becomes
    // exp(ln B - M) <= 1, and the common factor e^M cancels in the chromaticity
    // and is added back in log for the efficiency.
    const double step_nm = (kBandHighNm - kBandLowNm) / static_cast<double>(kBandSamples);
    double ln_values[kBandSamples + 1];
    double max_ln = -std::numeric_limits<double>::infinity();
    for (int i = 0; i <= kBandSamples; ++i) {
        const double nm = kBandLowNm + step_nm * static_cast<double>(i);
        ln_values[i] = ln_planck_radiance(nm * 1.0e-9, temperature);
        max_ln = std::max(max_ln, ln_values[i]);
    }

    // Pass 2: the trapezoid, in units of e^max_ln.
    CieXyz xyz{};
    for (int i = 0; i <= kBandSamples; ++i) {
        const double nm = kBandLowNm + step_nm * static_cast<double>(i);
        const double weight =
            step_nm * 1.0e-9 * ((i == 0 || i == kBandSamples) ? 0.5 : 1.0);
        const double relative = std::exp(ln_values[i] - max_ln);
        xyz.x += relative * cie_x_bar(nm) * weight;
        xyz.y += relative * cie_y_bar(nm) * weight;
        xyz.z += relative * cie_z_bar(nm) * weight;
    }

    BlackbodySample sample{};
    sample.xyz = xyz;

    const double sum = xyz.x + xyz.y + xyz.z;
    sample.chromaticity_x = sum > 0.0 ? xyz.x / sum : 0.0;
    sample.chromaticity_y = sum > 0.0 ? xyz.y / sum : 0.0;

    // Y is the photopic radiance in units of e^max_ln; the bolometric radiance is
    // sigma T^4 / pi.  Both logs, so the ratio is a difference.
    const double ln_bolometric =
        std::log(kStefanBoltzmann) + 4.0 * std::log(temperature) - std::log(units::pi);
    sample.ln_band_efficiency = max_ln + std::log(xyz.y) - ln_bolometric;

    // Normalise the chromaticity so the brightest channel is 1: the table carries
    // COLOUR, and the brightness arrives separately as D^4 and eta.  Multiplying
    // the two in the shader is what section 10 describes.
    const LinearRgb rgb = xyz_to_linear_srgb(
        CieXyz{sample.chromaticity_x, sample.chromaticity_y,
               1.0 - sample.chromaticity_x - sample.chromaticity_y});
    const double peak = std::max({rgb.r, rgb.g, rgb.b});
    if (peak > 0.0) {
        // Negative components mean the colour is outside the sRGB gamut, which a
        // black body below ~1500 K genuinely is.  Lifting to the gamut boundary is
        // a DISPLAY decision -- the alternative, a negative pixel, is not a colour
        // anyone can show -- and it is taken here, once, rather than in a shader.
        sample.rgb = LinearRgb{std::max(0.0, rgb.r) / peak, std::max(0.0, rgb.g) / peak,
                               std::max(0.0, rgb.b) / peak};
    }
    return sample;
}

PlanckTable build_planck_table(std::size_t width) {
    if (width < 2) {
        throw std::invalid_argument("build_planck_table: width must be at least 2");
    }

    PlanckTable table{};
    table.width = width;
    table.texels.resize(width * 4);

    for (std::size_t i = 0; i < width; ++i) {
        // Texel centres, so that a GPU sampler with linear filtering lands on the
        // values we computed rather than half a texel off.
        const double u = (static_cast<double>(i) + 0.5) / static_cast<double>(width);
        const double temperature = table_index_to_temperature(u);
        const auto sample = blackbody_sample(temperature);
        table.texels[i * 4 + 0] = static_cast<float>(sample.rgb.r);
        table.texels[i * 4 + 1] = static_cast<float>(sample.rgb.g);
        table.texels[i * 4 + 2] = static_cast<float>(sample.rgb.b);
        table.texels[i * 4 + 3] = static_cast<float>(sample.ln_band_efficiency);
    }
    return table;
}

namespace {

// Linear interpolation between texel centres -- the same thing a GPU sampler in
// LINEAR/CLAMP mode does, written out so that a test can compare the table
// against the exact integral through the identical path the shader takes.
double fetch(const PlanckTable& table, double temperature, int channel) {
    const double u = temperature_to_table_index(temperature);
    const double position = u * static_cast<double>(table.width) - 0.5;
    const double floor_position = std::floor(position);
    const double fraction = position - floor_position;

    const auto last = static_cast<long>(table.width) - 1;
    // The edges repeat the end texel, which is what CLAMP_TO_EDGE does.  It is not
    // a clamp on the physics: u is already a bijection onto [0,1), and the ends
    // are T = 0 (no light at all) and T = infinity (the Rayleigh-Jeans limit,
    // where the value has converged).
    const long i0 = std::clamp(static_cast<long>(floor_position), 0L, last);
    const long i1 = std::clamp(i0 + 1, 0L, last);

    const auto ch = static_cast<std::size_t>(channel);
    const auto a = static_cast<double>(table.texels[static_cast<std::size_t>(i0) * 4 + ch]);
    const auto b = static_cast<double>(table.texels[static_cast<std::size_t>(i1) * 4 + ch]);
    return a + (b - a) * fraction;
}

}  // namespace

LinearRgb PlanckTable::sample_rgb(double temperature) const {
    return LinearRgb{fetch(*this, temperature, 0), fetch(*this, temperature, 1),
                     fetch(*this, temperature, 2)};
}

double PlanckTable::sample_ln_band_efficiency(double temperature) const {
    return fetch(*this, temperature, 3);
}

double ln_band_limited_beaming(const PlanckTable& table, double rest_temperature,
                               double doppler) {
    // D^4 in log, plus eta(DT)/eta(T) as a difference of two tabulated logs.
    return 4.0 * std::log(doppler) +
           table.sample_ln_band_efficiency(rest_temperature * doppler) -
           table.sample_ln_band_efficiency(rest_temperature);
}

double ln_band_limited_steady_jet(double rest_temperature, double doppler) {
    return 3.0 * std::log(doppler) + blackbody_sample(rest_temperature * doppler).ln_band_efficiency -
           blackbody_sample(rest_temperature).ln_band_efficiency;
}

}  // namespace sf::render
