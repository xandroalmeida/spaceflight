#pragma once

// Absolute astronomical position  ->  camera-relative  ->  single precision.
//
// This is the only place in the project where a double becomes a float, and it
// happens AFTER the camera origin has been subtracted, which is the whole point:
// a float holds 7 significant digits, so 1.5e11 m resolves to 18 km, while
// 1.5e4 m (the same point, camera-relative) resolves to 2 mm.
//
// The transform READS the state.  There is no write path, by construction: every
// method is const and takes the position by value.  Rule 23:
//
//     "Nunca modifique a posicao fisica para executar floating origin."
//
// See docs/architecture/rendering.md.

#include "core/math/vec3.hpp"

#include <string>

namespace sf::render {

// The single-precision boundary.  Deliberately a distinct type from math::Vec3:
// assigning one to the other has to be a conversion someone wrote on purpose.
struct RenderVec3 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};

    [[nodiscard]] float length() const;
};

class RenderTransform {
public:
    // `scale` maps metres to scene units.  1e-7 puts 1 au at 1.5e4 units, which
    // fits comfortably inside a camera's near/far range.
    explicit RenderTransform(double scale = 1.0e-7);

    // Floating origin: where the camera is, in absolute metres.  Move it as the
    // camera moves; nothing else in the system notices.
    void set_camera_origin(const math::Vec3& absolute_position);
    [[nodiscard]] const math::Vec3& camera_origin() const noexcept { return origin_; }

    void set_scale(double scale);
    [[nodiscard]] double scale() const noexcept { return scale_; }

    // Bodies drawn to true scale are invisible dots at Solar System distances.
    // Exaggerating them is a legitimate presentation choice and therefore an
    // explicit, named parameter -- not a fudge factor hidden in the renderer.
    void set_body_scale_exaggeration(double factor);
    [[nodiscard]] double body_scale_exaggeration() const noexcept { return exaggeration_; }

    // Position: subtract the origin in double, then convert.
    [[nodiscard]] RenderVec3 to_render(const math::Vec3& absolute_position) const;

    // Direction or velocity: scaled, but NOT translated.  Subtracting the camera
    // origin from a velocity would be a category error, and a common one.
    [[nodiscard]] RenderVec3 vector_to_render(const math::Vec3& vector) const;

    // A body's drawn radius, in scene units.
    [[nodiscard]] float radius_to_render(double radius_metres) const;

    // Inverse, for picking and for tests.  Lossy by exactly the amount that the
    // float conversion lost.
    [[nodiscard]] math::Vec3 to_absolute(const RenderVec3& render_position) const;

    // Metres per float ulp at a given absolute position: what the projection
    // costs at that distance from the camera.  A diagnostic worth printing when
    // someone reports that a distant object jitters.
    [[nodiscard]] double resolution_at(const math::Vec3& absolute_position) const;

    // True when the camera has drifted far enough from the origin that the
    // projection is losing precision; the caller responds by re-centring.
    // `threshold_metres` is in absolute metres.
    [[nodiscard]] bool should_recenter(const math::Vec3& camera_absolute,
                                       double threshold_metres) const;

    [[nodiscard]] std::string describe() const;

private:
    math::Vec3 origin_{};
    double scale_{1.0e-7};
    double exaggeration_{1.0};
};

}  // namespace sf::render
