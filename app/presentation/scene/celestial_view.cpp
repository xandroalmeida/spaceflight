#include "app/presentation/scene/celestial_view.hpp"

#include "app/presentation/format.hpp"

#include <cmath>

namespace sf::app {

void CelestialView::build(const FlightSession& session) {
    names_.clear();
    surfaces_.clear();
    sun_index_ = -1;
    earth_index_ = -1;
    for (int i = 0; i < session.body_count(); ++i) {
        const auto name = session.body_name(i);
        names_.push_back(name);
        surfaces_.push_back(surface_for(name));
        if (name == "Sun") {
            sun_index_ = i;
        } else if (name == "Earth") {
            earth_index_ = i;
        }
    }
}

BodySurface CelestialView::surface_for(const std::string& name) {
    BodySurface s{};
    if (name == "Earth") {
        // The real maps, and the procedural placeholders when a file is missing
        // (rule 81): the scene never shows a bare sphere for want of an image.
        s.albedo_map = "textures/earth/earth_albedo.jpg|procedural:earth_albedo";
        s.cloud_map = "textures/earth/earth_clouds.jpg|procedural:earth_clouds";
        s.night_map = "textures/earth/earth_night.jpg|procedural:earth_night";
        // The limb: Rayleigh scattering is blue because the sky is blue, and the
        // strength is a drawing number. There is no atmospheric physics here and
        // rule 31 says there must not be.
        s.atmosphere_strength = 0.55;
        s.atmosphere_haze = 0.06;
        s.atmosphere_colour = Colour{0.30F, 0.52F, 0.95F};
    } else if (name == "Moon") {
        s.albedo_map = "textures/moon/moon_albedo.png|procedural:moon_albedo";
        // The relief is DERIVED from the topography LOLA measured
        // (scripts/make_moon_normal.py) and not painted.
        s.normal_map = "textures/moon/moon_normal.png";
    } else if (name == "Mars") {
        // The real albedo map, when it exists; the placeholder until then.
        s.albedo_map = "textures/mars/mars_albedo.png|procedural:mars_albedo";
        // Mars's atmosphere is 0.6 % of the Earth's pressure and the limb it
        // makes is an ochre thread, not a blue halo. ⚠️ DRAWING and not physics:
        // there is no aerocapture (rule 56), and this touches nothing but the
        // shader.
        s.atmosphere_strength = 0.12;
        s.atmosphere_colour = Colour{0.85F, 0.62F, 0.45F};
    }
    return s;
}

std::vector<BodyDraw> CelestialView::update(const FlightSession& session, const Vec3& camera_position) {
    std::vector<BodyDraw> out;
    if (!session.is_ready()) {
        return out;
    }
    Vec3 sun_direction{1.0, 0.0, 0.0};
    if (sun_index_ >= 0) {
        const auto p = session.body_observed_position(sun_index_, effect_retarded, effect_aberration);
        const Vec3 to_sun = widen(p) - camera_position;
        if (to_sun.norm() > 0.0) {
            sun_direction = to_sun.normalized();
        }
    }
    sun_direction_ = sun_direction;

    const int count = std::min(session.body_count(), static_cast<int>(names_.size()));
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        BodyDraw body{};
        body.index = i;
        body.name = names_[static_cast<std::size_t>(i)];
        const auto p = session.body_observed_position(i, effect_retarded, effect_aberration);
        body.position = widen(p);
        body.radius = session.body_radius(i);
        body.visible = body.radius > 0.0;
        // The orientation comes from the kernels; the scale from the radius.
        body.mesh_basis = mesh_basis(session.body_orientation(i)).scaled(Vec3{body.radius, body.radius, body.radius});
        body.reflectance = colour_for(body.name);
        body.self_luminous = body.name == "Sun";
        const double physical_doppler = session.body_doppler(i);
        body.doppler = effect_doppler ? physical_doppler : 1.0;
        body.beaming_doppler = effect_beaming ? physical_doppler : 1.0;
        const auto v = session.body_relative_velocity_scene(i);
        body.relative_velocity_scene = widen(v);
        body.light_speed_scene = session.light_speed_scene();
        body.apply_light_time = effect_retarded;
        body.half_saturation = exposure;
        body.observer_position_scene = camera_position;
        body.sun_direction_scene = sun_direction;
        body.cloud_offset = i == earth_index_ ? cloud_offset_ : 0.0;
        body.surface = surfaces_[static_cast<std::size_t>(i)];
        out.push_back(std::move(body));
    }
    return out;
}

void CelestialView::advance_clouds(double delta) { cloud_offset_ = std::fmod(cloud_offset_ + delta / 2400.0, 1.0); }

int CelestialView::index_of(const std::string& name) const {
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<std::string> CelestialView::camera_inside_bodies(const FlightSession& session,
                                                             const Vec3& camera_position,
                                                             const std::string& scale_label) const {
    std::vector<std::string> out;
    for (int i = 0; i < session.body_count(); ++i) {
        const double radius = session.body_radius(i);
        if (radius <= 0.0) {
            continue;
        }
        const auto p = session.body_observed_position(i, effect_retarded, effect_aberration);
        const double distance = (widen(p) - camera_position).norm();
        if (radius > distance) {
            out.push_back(fmt::format("camera is inside %s: drawn radius %.2f > distance %.2f units (%s)",
                                      session.body_name(i).c_str(), radius, distance, scale_label.c_str()));
        }
    }
    return out;
}

Basis CelestialView::mesh_basis(const BodyAxes& orientation) {
    return Basis{orientation.y * -1.0, orientation.z, orientation.x * -1.0};
}

Colour CelestialView::colour_for(const std::string& name) {
    if (name == "Sun") return Colour{1.0F, 0.92F, 0.6F};
    if (name == "Mercury") return Colour{0.55F, 0.52F, 0.49F};
    if (name == "Venus") return Colour{0.90F, 0.80F, 0.55F};
    if (name == "Earth") return Colour{0.25F, 0.45F, 0.85F};
    if (name == "Moon") return Colour{0.72F, 0.72F, 0.70F};
    if (name == "Mars") return Colour{0.72F, 0.44F, 0.28F};
    if (name == "Phobos" || name == "Deimos") return Colour{0.42F, 0.38F, 0.35F};
    if (name == "Jupiter") return Colour{0.85F, 0.72F, 0.55F};
    if (name == "Saturn") return Colour{0.89F, 0.81F, 0.62F};
    if (name == "Uranus") return Colour{0.62F, 0.84F, 0.86F};
    if (name == "Neptune") return Colour{0.30F, 0.44F, 0.78F};
    if (name == "Pluto") return Colour{0.68F, 0.60F, 0.52F};
    return Colour{0.6F, 0.6F, 0.65F};
}

}  // namespace sf::app
