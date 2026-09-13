#include "sky_node.hpp"

#include "core/relativity/optics.hpp"
#include "core/render/blackbody.hpp"
#include "core/units/constants.hpp"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>
#include <exception>

namespace spaceflight_godot {
namespace {

using godot::D_METHOD;

// The same guard as simulation_node.cpp, for the same reason: an exception must
// not cross GDExtension's C ABI.
template <typename Fn>
bool guarded(std::string& error_slot, const char* what, Fn&& fn) {
    try {
        fn();
        return true;
    } catch (const std::exception& e) {
        error_slot = std::string{what} + ": " + e.what();
        godot::UtilityFunctions::push_error(godot::String{error_slot.c_str()});
        return false;
    } catch (...) {
        error_slot = std::string{what} + ": unknown error";
        godot::UtilityFunctions::push_error(godot::String{error_slot.c_str()});
        return false;
    }
}

}  // namespace

SpaceflightSky::SpaceflightSky() = default;
SpaceflightSky::~SpaceflightSky() = default;

void SpaceflightSky::_bind_methods() {
    godot::ClassDB::bind_method(D_METHOD("load_catalogue", "path"),
                                &SpaceflightSky::load_catalogue);
    godot::ClassDB::bind_method(D_METHOD("is_ready"), &SpaceflightSky::is_ready);
    godot::ClassDB::bind_method(D_METHOD("get_last_error"), &SpaceflightSky::get_last_error);
    godot::ClassDB::bind_method(D_METHOD("describe_catalogue"),
                                &SpaceflightSky::describe_catalogue);
    godot::ClassDB::bind_method(D_METHOD("get_star_count"), &SpaceflightSky::get_star_count);
    godot::ClassDB::bind_method(D_METHOD("set_magnitude_limit", "limit"),
                                &SpaceflightSky::set_magnitude_limit);
    godot::ClassDB::bind_method(D_METHOD("get_magnitude_limit"),
                                &SpaceflightSky::get_magnitude_limit);
    godot::ClassDB::bind_method(D_METHOD("set_half_saturation", "half_saturation"),
                                &SpaceflightSky::set_half_saturation);
    godot::ClassDB::bind_method(D_METHOD("get_half_saturation"),
                                &SpaceflightSky::get_half_saturation);
    godot::ClassDB::bind_method(D_METHOD("get_planck_table_image"),
                                &SpaceflightSky::get_planck_table_image);
    godot::ClassDB::bind_method(D_METHOD("get_planck_table_reference_temperature"),
                                &SpaceflightSky::get_planck_table_reference_temperature);
    godot::ClassDB::bind_method(D_METHOD("update_sky", "beta", "sky_radius"),
                                &SpaceflightSky::update_sky);
    godot::ClassDB::bind_method(
        D_METHOD("update_sky_effects", "beta", "sky_radius", "aberration", "doppler", "beaming"),
        &SpaceflightSky::update_sky_effects);
    godot::ClassDB::bind_method(D_METHOD("get_surface_arrays"),
                                &SpaceflightSky::get_surface_arrays);
    godot::ClassDB::bind_method(D_METHOD("get_diagnostics"), &SpaceflightSky::get_diagnostics);
    godot::ClassDB::bind_method(D_METHOD("get_diagnostics_at", "beta"),
                                &SpaceflightSky::get_diagnostics_at);
    godot::ClassDB::bind_method(D_METHOD("get_doppler_in_direction", "to_source"),
                                &SpaceflightSky::get_doppler_in_direction);
}

bool SpaceflightSky::load_catalogue(const godot::String& path) {
    return guarded(last_error_, "load_catalogue", [&] {
        const std::string given{path.utf8().get_data()};
        const std::string file =
            given.empty() ? sf::render::StarCatalog::default_path() : given;

        auto catalogue = sf::render::StarCatalog::from_bsc5_file(file);
        catalogue.keep_brighter_than(magnitude_limit_);

        // 1024 entries: 45 K per texel at 5800 K, far below the scale on which the
        // chromaticity moves (docs/architecture/relativistic-shaders.md 3.2).
        sky_ = std::make_unique<sf::render::RelativisticSky>(
            std::move(catalogue), sf::render::build_planck_table(1024));
        sky_->set_half_saturation(half_saturation_);
        sky_->update(beta_);
    });
}

godot::String SpaceflightSky::get_last_error() const {
    return godot::String{last_error_.c_str()};
}

godot::String SpaceflightSky::describe_catalogue() const {
    if (sky_ == nullptr) {
        return godot::String{"no catalogue loaded"};
    }
    return godot::String{sky_->catalog().describe().c_str()};
}

int SpaceflightSky::get_star_count() const {
    return sky_ == nullptr ? 0 : static_cast<int>(sky_->star_count());
}

void SpaceflightSky::set_magnitude_limit(double limit) { magnitude_limit_ = limit; }
double SpaceflightSky::get_magnitude_limit() const { return magnitude_limit_; }

void SpaceflightSky::set_half_saturation(double half_saturation) {
    guarded(last_error_, "set_half_saturation", [&] {
        if (!(half_saturation > 0.0)) {
            throw std::invalid_argument("half saturation must be > 0");
        }
        half_saturation_ = half_saturation;
        if (sky_ != nullptr) {
            sky_->set_half_saturation(half_saturation);
        }
    });
}

double SpaceflightSky::get_half_saturation() const { return half_saturation_; }

godot::Ref<godot::Image> SpaceflightSky::get_planck_table_image() const {
    if (sky_ == nullptr) {
        return {};
    }
    const auto& table = sky_->planck_table();

    godot::PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(table.byte_size()));
    std::memcpy(bytes.ptrw(), table.texels.data(), table.byte_size());

    // RGBAF, not RGBA8: alpha carries ln(eta), which runs from -17000 to -2 and
    // has no business in a byte.
    return godot::Image::create_from_data(static_cast<int32_t>(table.width), 1, false,
                                          godot::Image::FORMAT_RGBAF, bytes);
}

double SpaceflightSky::get_planck_table_reference_temperature() const {
    return sf::render::kTableReferenceTemperature;
}

void SpaceflightSky::update_sky(const godot::Vector3& beta, double sky_radius) {
    update_sky_effects(beta, sky_radius, true, true, true);
}

void SpaceflightSky::update_sky_effects(const godot::Vector3& beta, double sky_radius,
                                        bool aberration, bool doppler, bool beaming) {
    if (sky_ == nullptr) {
        return;
    }
    guarded(last_error_, "update_sky", [&] {
        beta_ = sf::math::Vec3{beta.x, beta.y, beta.z};
        sky_radius_ = sky_radius;
        sky_->update(beta_, aberration, doppler, beaming);
    });
}

godot::Dictionary SpaceflightSky::get_surface_arrays() const {
    godot::Dictionary out;
    if (sky_ == nullptr || sky_->star_count() == 0) {
        return out;
    }

    const auto& frame = sky_->frame();
    const auto& stars = sky_->catalog().stars();
    const auto count = static_cast<int64_t>(sky_->star_count());

    godot::PackedVector3Array vertices;
    vertices.resize(count);
    godot::PackedFloat32Array custom;
    custom.resize(count * 4);

    auto* v = vertices.ptrw();
    auto* c = custom.ptrw();
    const auto radius = static_cast<float>(sky_radius_);
    for (int64_t i = 0; i < count; ++i) {
        const auto index = static_cast<std::size_t>(i);
        const auto& d = frame.apparent_direction[index];
        v[i] = godot::Vector3{static_cast<float>(d.x) * radius, static_cast<float>(d.y) * radius,
                              static_cast<float>(d.z) * radius};
        c[i * 4 + 0] = static_cast<float>(stars[index].temperature);
        c[i * 4 + 1] = static_cast<float>(stars[index].rest_flux);
        c[i * 4 + 2] = frame.doppler[index];
        c[i * 4 + 3] = frame.beaming[index];
    }

    godot::Array arrays;
    arrays.resize(godot::Mesh::ARRAY_MAX);
    arrays[godot::Mesh::ARRAY_VERTEX] = vertices;
    arrays[godot::Mesh::ARRAY_CUSTOM0] = custom;
    out["arrays"] = arrays;

    // Without this flag CUSTOM0 is four BYTES, and a temperature would be
    // quantised to [0,1] -- every star the same colour, and no error anywhere.
    out["format"] = static_cast<int64_t>(godot::Mesh::ARRAY_CUSTOM_RGBA_FLOAT)
                    << godot::Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
    return out;
}

namespace {

godot::Dictionary to_dictionary(const sf::render::SkyDiagnostics& d, std::size_t star_count) {
    godot::Dictionary out;
    out["beta"] = d.beta;
    out["lorentz_factor"] = d.lorentz_factor;
    out["forward_cone_deg"] = d.forward_cone_half_angle * 180.0 / sf::units::pi;
    out["stars_in_forward_cone"] = static_cast<int64_t>(d.stars_in_forward_cone);
    out["fraction_in_forward_cone"] = d.fraction_in_forward_cone;
    out["max_doppler"] = d.max_doppler;
    out["min_doppler"] = d.min_doppler;
    out["brightest_response"] = d.brightest_response;
    out["faintest_response"] = d.faintest_response;
    out["reference_forward_visible"] = d.reference_forward_visible;
    out["reference_aft_visible"] = d.reference_aft_visible;
    out["star_count"] = static_cast<int64_t>(star_count);
    return out;
}

}  // namespace

double SpaceflightSky::get_doppler_in_direction(const godot::Vector3& to_source) const {
    const sf::math::Vec3 direction{to_source.x, to_source.y, to_source.z};
    if (direction.norm_squared() <= 0.0) {
        return 1.0;
    }
    return sf::relativity::doppler_factor_to_source(direction, beta_);
}

godot::Dictionary SpaceflightSky::get_diagnostics() const {
    if (sky_ == nullptr) {
        return godot::Dictionary{};
    }
    return to_dictionary(sky_->diagnostics(beta_), sky_->star_count());
}

godot::Dictionary SpaceflightSky::get_diagnostics_at(const godot::Vector3& beta) {
    if (sky_ == nullptr) {
        return godot::Dictionary{};
    }
    // Restores the frame to the beta the ship is actually at, so the next thing
    // drawn is the real sky and not the projection.
    return to_dictionary(
        sky_->diagnostics_at(sf::math::Vec3{beta.x, beta.y, beta.z}, beta_),
        sky_->star_count());
}

}  // namespace spaceflight_godot
