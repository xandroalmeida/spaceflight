#pragma once

// Orientation of a celestial body, reduced to what the dynamics actually needs.
//
// An axially symmetric gravity field (J2 and the other zonal terms) depends only
// on the direction of the body's figure axis -- NOT on how far the body has
// rotated about it.  So this interface deliberately exposes the pole direction
// and nothing else: asking for a full body-fixed rotation would invite someone
// to transform the spacecraft into a rotating frame inside the force evaluation,
// which is exactly what docs/architecture/coordinate-system.md section 2 forbids.
//
// See docs/physics/geopotential.md section 3.1.

#include "core/celestial/body_id.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/math/vec3.hpp"
#include "core/time/coordinate_time.hpp"

namespace sf::celestial {

class BodyOrientationProvider {
public:
    BodyOrientationProvider() = default;
    virtual ~BodyOrientationProvider() = default;
    BodyOrientationProvider(const BodyOrientationProvider&) = delete;
    BodyOrientationProvider& operator=(const BodyOrientationProvider&) = delete;

    // Unit vector along the body's north pole (the +z axis of its body-fixed
    // frame), expressed in `axes`.
    [[nodiscard]] virtual math::Vec3 pole_direction(BodyId body,
                                                    time::CoordinateTime t,
                                                    coordinates::FrameAxes axes) const = 0;
};

}  // namespace sf::celestial
