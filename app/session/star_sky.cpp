#include "app/session/star_sky.hpp"

#include "core/relativity/optics.hpp"
#include "core/render/blackbody.hpp"
#include "core/units/constants.hpp"

#include <exception>
#include <stdexcept>
#include <string>

namespace sf::app {
namespace {

SkyDiagnosticsView to_view(const render::SkyDiagnostics& d, std::size_t star_count) {
    SkyDiagnosticsView out{};
    out.valid = true;
    out.beta = d.beta;
    out.lorentz_factor = d.lorentz_factor;
    out.forward_cone_deg = d.forward_cone_half_angle * 180.0 / units::pi;
    out.stars_in_forward_cone = static_cast<long long>(d.stars_in_forward_cone);
    out.fraction_in_forward_cone = d.fraction_in_forward_cone;
    out.max_doppler = d.max_doppler;
    out.min_doppler = d.min_doppler;
    out.brightest_response = d.brightest_response;
    out.faintest_response = d.faintest_response;
    out.reference_forward_visible = d.reference_forward_visible;
    out.reference_aft_visible = d.reference_aft_visible;
    out.star_count = static_cast<long long>(star_count);
    return out;
}

}  // namespace

StarSky::StarSky() = default;
StarSky::~StarSky() = default;

bool StarSky::load_catalogue(const std::string& path) {
    try {
        const std::string file = path.empty() ? render::StarCatalog::default_path() : path;
        auto catalogue = render::StarCatalog::from_bsc5_file(file);
        catalogue.keep_brighter_than(magnitude_limit_);

        // 1024 entries: 45 K per texel at 5800 K, far below the scale on which the
        // chromaticity moves (docs/architecture/relativistic-shaders.md 3.2).
        sky_ = std::make_unique<render::RelativisticSky>(std::move(catalogue),
                                                         render::build_planck_table(1024));
        sky_->set_half_saturation(half_saturation_);
        sky_->update(beta_);
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string{"load_catalogue: "} + e.what();
        return false;
    }
}

bool StarSky::load_synthetic(const std::vector<math::Vec3>& directions,
                             const std::vector<double>& temperatures,
                             const std::vector<double>& magnitudes) {
    try {
        if (temperatures.size() != directions.size() || magnitudes.size() != directions.size()) {
            throw std::invalid_argument(
                "directions, temperatures and magnitudes must be the same length");
        }
        if (directions.empty()) {
            throw std::invalid_argument("no stars given");
        }
        render::StarCatalog catalogue;
        for (std::size_t i = 0; i < directions.size(); ++i) {
            catalogue.add_star(directions[i], temperatures[i], magnitudes[i],
                               "synthetic " + std::to_string(i));
        }
        sky_ = std::make_unique<render::RelativisticSky>(std::move(catalogue),
                                                         render::build_planck_table(1024));
        sky_->set_half_saturation(half_saturation_);
        sky_->update(beta_);
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string{"load_synthetic: "} + e.what();
        return false;
    }
}

std::string StarSky::describe_catalogue() const {
    return sky_ == nullptr ? std::string{"no catalogue loaded"} : sky_->catalog().describe();
}

int StarSky::star_count() const { return sky_ == nullptr ? 0 : static_cast<int>(sky_->star_count()); }

bool StarSky::set_half_saturation(double half_saturation) {
    if (!(half_saturation > 0.0)) {
        last_error_ = "set_half_saturation: half saturation must be > 0";
        return false;
    }
    half_saturation_ = half_saturation;
    if (sky_ != nullptr) {
        sky_->set_half_saturation(half_saturation);
    }
    return true;
}

const render::PlanckTable* StarSky::planck_table() const {
    return sky_ == nullptr ? nullptr : &sky_->planck_table();
}

double StarSky::planck_table_reference_temperature() const { return render::kTableReferenceTemperature; }

void StarSky::update(const math::Vec3& beta, double sky_radius, bool aberration, bool doppler,
                     bool beaming) {
    if (sky_ == nullptr) {
        return;
    }
    try {
        beta_ = beta;
        sky_radius_ = sky_radius;
        sky_->update(beta_, aberration, doppler, beaming);
    } catch (const std::exception& e) {
        last_error_ = std::string{"update: "} + e.what();
    }
}

std::vector<StarVertex> StarSky::vertices() const {
    std::vector<StarVertex> out;
    if (sky_ == nullptr || sky_->star_count() == 0) {
        return out;
    }
    const auto& frame = sky_->frame();
    const auto& stars = sky_->catalog().stars();
    out.resize(sky_->star_count());
    const auto radius = static_cast<float>(sky_radius_);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto& d = frame.apparent_direction[i];
        auto& v = out[i];
        v.position[0] = static_cast<float>(d.x) * radius;
        v.position[1] = static_cast<float>(d.y) * radius;
        v.position[2] = static_cast<float>(d.z) * radius;
        v.custom[0] = static_cast<float>(stars[i].temperature);
        v.custom[1] = static_cast<float>(stars[i].rest_flux);
        v.custom[2] = frame.doppler[i];
        v.custom[3] = frame.beaming[i];
    }
    return out;
}

SkyDiagnosticsView StarSky::diagnostics() const {
    if (sky_ == nullptr) {
        return {};
    }
    return to_view(sky_->diagnostics(beta_), sky_->star_count());
}

SkyDiagnosticsView StarSky::diagnostics_at(const math::Vec3& beta) {
    if (sky_ == nullptr) {
        return {};
    }
    // Restores the frame to the beta the ship is actually at, so the next thing
    // drawn is the real sky and not the projection.
    return to_view(sky_->diagnostics_at(beta, beta_), sky_->star_count());
}

double StarSky::doppler_in_direction(const math::Vec3& to_source) const {
    if (to_source.norm_squared() <= 0.0) {
        return 1.0;
    }
    return relativity::doppler_factor_to_source(to_source, beta_);
}

std::size_t StarSky::checked_index(int index, const char* what) const {
    if (sky_ == nullptr) {
        throw std::runtime_error(std::string{what} + ": no catalogue loaded");
    }
    if (index < 0 || static_cast<std::size_t>(index) >= sky_->star_count()) {
        throw std::out_of_range(std::string{what} + ": star index out of range");
    }
    return static_cast<std::size_t>(index);
}

math::Vec3 StarSky::apparent_direction(int index) const {
    return sky_->frame().apparent_direction[checked_index(index, "apparent_direction")];
}

math::Vec3 StarSky::rest_direction(int index) const {
    return sky_->catalog().stars()[checked_index(index, "rest_direction")].direction;
}

double StarSky::expected_response(int index) const {
    return sky_->response_of(checked_index(index, "expected_response"));
}

LinearColour StarSky::expected_colour(int index) const {
    const auto i = checked_index(index, "expected_colour");
    const auto& star = sky_->catalog().stars()[i];
    const double doppler = static_cast<double>(sky_->frame().doppler[i]);
    // The same two lines the fragment stage runs: the shifted chromaticity, scaled
    // by the response.  Written here in double precision so that a disagreement
    // with the GPU is a disagreement about the PIPELINE and not about float.
    const auto rgb = sky_->planck_table().sample_rgb(star.temperature * doppler);
    const double response = sky_->response_of(i);
    return LinearColour{rgb.r * response, rgb.g * response, rgb.b * response};
}

double StarSky::doppler_of(int index) const {
    return static_cast<double>(sky_->frame().doppler[checked_index(index, "doppler_of")]);
}

double StarSky::beaming_of(int index) const {
    return static_cast<double>(sky_->frame().beaming[checked_index(index, "beaming_of")]);
}

double StarSky::star_temperature(int index) const {
    return sky_->catalog().stars()[checked_index(index, "star_temperature")].temperature;
}

double StarSky::star_magnitude(int index) const {
    return sky_->catalog().stars()[checked_index(index, "star_magnitude")].visual_magnitude;
}

}  // namespace sf::app
