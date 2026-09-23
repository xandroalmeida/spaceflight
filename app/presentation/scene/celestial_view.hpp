#pragma once

// The celestial bodies, drawn where the core says they APPEAR.
//
// Inherited from Milestone 5 without changing a sum: the position comes from
// body_observed_position() (light time against the real ephemeris, then
// aberration), the Doppler factor from core/relativity/optics.hpp, and the
// shader applies what D means without ever knowing what beta is.
//
// What Milestone 7 added is surface: texture, clouds, night side, limb -- and the
// body's rotation, which comes from body_orientation(), that is, from pxform_c
// over the same kernels. None of it feeds anything back. A textured Earth and a
// smooth blue Earth produce the same state, frame by frame.

#include "app/presentation/geometry.hpp"
#include "app/presentation/palette.hpp"
#include "app/session/flight_session.hpp"

#include <string>
#include <vector>

namespace sf::app {

// What a body's surface is made of. Empty paths mean "no map": the body is
// drawn in its mean colour. "procedural:<name>" is a placeholder the renderer
// asks PlanetTextures for, until the real image exists (rule 81).
struct BodySurface {
    std::string albedo_map;
    std::string cloud_map;
    std::string night_map;
    std::string normal_map;
    double atmosphere_strength{0.0};
    double atmosphere_haze{0.0};
    Colour atmosphere_colour{0.35F, 0.55F, 0.95F};
};

// Everything the body shader needs for one body, one frame.
struct BodyDraw {
    int index{-1};
    std::string name;
    bool visible{false};
    Vec3 position{};          // scene units, where the body APPEARS
    double radius{0.0};       // scene units
    Basis mesh_basis{};       // body axes -> mesh axes, times the radius
    Colour reflectance{};     // sRGB
    bool self_luminous{false};
    double doppler{1.0};
    double beaming_doppler{1.0};
    Vec3 relative_velocity_scene{};
    double light_speed_scene{299.792458};
    bool apply_light_time{true};
    double half_saturation{0.1584893};
    Vec3 observer_position_scene{};
    Vec3 sun_direction_scene{1.0, 0.0, 0.0};
    double cloud_offset{0.0};
    BodySurface surface{};
};

class CelestialView {
public:
    static constexpr double SUN_TEMPERATURE = 5772.0;

    void build(const FlightSession& session);

    // Positions, orientations and optics for every body, from the camera at
    // `camera_position` (scene units).
    [[nodiscard]] std::vector<BodyDraw> update(const FlightSession& session, const Vec3& camera_position);

    // One turn every forty minutes of wall time. It is DRAWING: there is no
    // atmospheric circulation in this project, and the alternative -- clouds
    // absolutely still over a turning planet -- reads as a rendering fault, which
    // is what it would be.
    void advance_clouds(double delta);

    // The direction the Sun's light comes FROM, as seen from the camera; the far
    // side goes dark as a consequence and not by adjustment (rule 29).
    [[nodiscard]] const Vec3& sun_direction() const { return sun_direction_; }

    [[nodiscard]] int index_of(const std::string& name) const;
    [[nodiscard]] int sun_index() const { return sun_index_; }

    // Messages for any body the camera is inside of.
    [[nodiscard]] std::vector<std::string> camera_inside_bodies(const FlightSession& session,
                                                                const Vec3& camera_position,
                                                                const std::string& scale_label) const;

    // From the body's axes to the MESH's axes.
    //
    // body_orientation() gives a basis whose columns are the body-fixed axes: +x
    // the prime meridian, +z the north pole. The sphere mesh's pole is local +y,
    // and its texture u runs from local +z at u = 0, with local +x at u = 0.25.
    // For an equirectangular texture with longitude -180 on the left edge:
    //
    //     u = 0     <-> longitude -180 <-> body -x   =>  mesh +z = -(body x)
    //     u = 0.25  <-> longitude  -90 <-> body -y   =>  mesh +x = -(body y)
    //     v = 0     <-> north pole     <-> body +z   =>  mesh +y = +(body z)
    //
    // The determinant stays +1 (two sign flips), so it is a rotation and not a
    // reflection -- which matters, because a reflection would mirror the
    // continents and the picture would still look plausible.
    [[nodiscard]] static Basis mesh_basis(const BodyAxes& orientation);

    // The base colour, used where there is no texture (rule 69). Not invented --
    // the mean colour of the disc in spacecraft images -- but not measured
    // photometrically either.
    [[nodiscard]] static Colour colour_for(const std::string& name);

    bool effect_retarded{true};
    bool effect_aberration{true};
    bool effect_doppler{true};
    bool effect_beaming{true};
    double exposure{0.1584893};

private:
    [[nodiscard]] static BodySurface surface_for(const std::string& name);

    std::vector<std::string> names_;
    std::vector<BodySurface> surfaces_;
    int sun_index_{-1};
    int earth_index_{-1};
    double cloud_offset_{0.0};
    Vec3 sun_direction_{1.0, 0.0, 0.0};
};

// The star field of Milestone 5: 8786 real stars from the Yale BSC5, drawn as
// points, coloured and dimmed by a shader that receives the Doppler factor ready
// and never learns what beta is. The aberration happens on the CPU, in
// core/render/relativistic_sky.cpp; the presentation carries a vector there and
// arrays back.
struct StarfieldView {
    // The star sphere sits inside the world camera's far plane (2e5) and beyond
    // the Sun at 1.47e5, so that the Sun still hides it.
    static constexpr double SKY_RADIUS = 1.9e5;

    bool effect_aberration{true};
    bool effect_doppler{true};
    bool effect_beaming{true};
};

}  // namespace sf::app
