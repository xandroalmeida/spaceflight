#include "core/render/relativistic_sky.hpp"

#include "core/relativity/optics.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sf::render {

RelativisticSky::RelativisticSky(StarCatalog catalog, PlanckTable table)
    : catalog_(std::move(catalog)), table_(std::move(table)) {
    const std::size_t n = catalog_.size();
    frame_.apparent_direction.resize(n);
    frame_.doppler.assign(n, 1.0F);
    frame_.star_count = n;
    update(math::Vec3{});
}

void RelativisticSky::update(const math::Vec3& beta) {
    const auto& stars = catalog_.stars();
    for (std::size_t i = 0; i < stars.size(); ++i) {
        const math::Vec3& to_source = stars[i].direction;
        // Order matters and is the order the photon experiences: the star's
        // direction is aberrated into the ship frame, and the Doppler factor is
        // taken from the direction in the COORDINATE frame -- which is why the
        // second call takes `to_source` and not the aberrated result.  Feeding it
        // the aberrated direction would apply the boost twice.
        frame_.apparent_direction[i] = relativity::aberrate_source_direction(to_source, beta);
        frame_.doppler[i] =
            static_cast<float>(relativity::doppler_factor_to_source(to_source, beta));
    }
}

void RelativisticSky::set_half_saturation(double half_saturation) {
    if (!(half_saturation > 0.0) || !std::isfinite(half_saturation)) {
        throw std::invalid_argument("RelativisticSky: half saturation must be finite and > 0");
    }
    half_saturation_ = half_saturation;
}

double RelativisticSky::response_of(std::size_t index) const {
    if (index >= catalog_.size()) {
        throw std::out_of_range("RelativisticSky::response_of: index out of range");
    }
    const auto& star = catalog_.stars()[index];
    const double doppler = static_cast<double>(frame_.doppler[index]);

    // Everything in log, all the way to the response curve: the aft sky at
    // beta = 0.99 is e^-52 of its rest flux, and forming the number would flush
    // it to zero before the curve ever saw it.
    const double ln_luminance = std::log(star.rest_flux) +
                                ln_band_limited_beaming(table_, star.temperature, doppler);
    return detector_response_from_ln(ln_luminance, half_saturation_);
}

SkyDiagnostics RelativisticSky::diagnostics_at(const math::Vec3& beta,
                                               const math::Vec3& restore_to) {
    update(beta);
    const auto result = diagnostics(beta);
    update(restore_to);
    return result;
}

SkyDiagnostics RelativisticSky::diagnostics(const math::Vec3& beta) const {
    SkyDiagnostics d{};
    d.beta = beta.norm();
    d.lorentz_factor = relativity::gamma_from_beta(beta);

    // arccos(beta) is where a source at 90 degrees lands, and the cone of that
    // half-angle has solid-angle fraction (1 - beta)/2 -- so a uniform sky puts
    // exactly half its sources inside it (physics document section 3).
    // The min is float hygiene, not a limit: |v| < c is structural (rule 13), so
    // beta can only reach 1 by rounding, and acos(1 + 1e-16) is NaN while
    // acos(1) is 0. Nothing here decides anything about the physics.
    d.forward_cone_half_angle = std::acos(std::min(1.0, d.beta));
    d.max_doppler = d.lorentz_factor * (1.0 + d.beta);
    d.min_doppler = d.lorentz_factor * (1.0 - d.beta);

    const math::Vec3 heading = d.beta > 0.0 ? beta / d.beta : math::Vec3::unit_x();
    const double cos_cone = std::cos(d.forward_cone_half_angle);

    d.brightest_response = 0.0;
    d.faintest_response = 1.0;
    for (std::size_t i = 0; i < catalog_.size(); ++i) {
        if (dot(frame_.apparent_direction[i], heading) >= cos_cone) {
            ++d.stars_in_forward_cone;
        }
        const double response = response_of(i);
        d.brightest_response = std::max(d.brightest_response, response);
        d.faintest_response = std::min(d.faintest_response, response);
    }
    d.fraction_in_forward_cone =
        catalog_.empty()
            ? 0.0
            : static_cast<double>(d.stars_in_forward_cone) / static_cast<double>(catalog_.size());

    // The 5800 K row of section 10.2, live.
    constexpr double kReferenceTemperature = 5800.0;
    d.reference_forward_visible =
        std::exp(ln_band_limited_beaming(table_, kReferenceTemperature, d.max_doppler));
    d.reference_aft_visible =
        std::exp(ln_band_limited_beaming(table_, kReferenceTemperature, d.min_doppler));
    return d;
}

}  // namespace sf::render
