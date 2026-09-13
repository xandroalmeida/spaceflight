#pragma once

// A reference frame here is (origin, axes).  Those are independent: changing the
// origin is a translation, changing the axes is a rotation, and conflating them
// is how velocity errors of ~30 km/s appear.
// See docs/architecture/coordinate-system.md.

#include "core/celestial/body_id.hpp"

#include <string>
#include <string_view>

namespace sf::coordinates {

// Axis orientations.  The names map onto SPICE frame names 1:1, which is the
// point: we never hand-build a rotation SPICE already knows.
enum class FrameAxes {
    J2000,        // EME2000, aligned with ICRF -- the integration axes
    ECLIPJ2000,   // ecliptic of J2000; presentation only
    IAU_EARTH,    // body-fixed, rotating -- I/O only, never integration
    IAU_MOON,
    IAU_SUN,
    IAU_MARS
};

constexpr std::string_view spice_frame_name(FrameAxes axes) {
    switch (axes) {
        case FrameAxes::J2000:      return "J2000";
        case FrameAxes::ECLIPJ2000: return "ECLIPJ2000";
        case FrameAxes::IAU_EARTH:  return "IAU_EARTH";
        case FrameAxes::IAU_MOON:   return "IAU_MOON";
        case FrameAxes::IAU_SUN:    return "IAU_SUN";
        case FrameAxes::IAU_MARS:   return "IAU_MARS";
    }
    return "J2000";
}

// True for axes that rotate with a body: such a frame is non-inertial and must
// never be used as the integration frame.
constexpr bool is_body_fixed(FrameAxes axes) {
    switch (axes) {
        case FrameAxes::J2000:
        case FrameAxes::ECLIPJ2000:
            return false;
        default:
            return true;
    }
}

struct ReferenceFrame {
    celestial::BodyId origin{celestial::bodies::solar_system_barycenter};
    FrameAxes axes{FrameAxes::J2000};

    static constexpr ReferenceFrame ssb_j2000() { return ReferenceFrame{}; }
    static constexpr ReferenceFrame centered_on(celestial::BodyId body, FrameAxes ax = FrameAxes::J2000) {
        return ReferenceFrame{body, ax};
    }

    [[nodiscard]] constexpr bool is_inertial() const { return !is_body_fixed(axes); }
    [[nodiscard]] std::string to_string() const {
        return origin.name() + " / " + std::string{spice_frame_name(axes)};
    }

    friend constexpr bool operator==(const ReferenceFrame&, const ReferenceFrame&) = default;
};

// Short alias used inside numeric code; the long name is what appears in public
// interfaces (EphemerisProvider::state).
using Frame = ReferenceFrame;

// Frames from which the FrameAxes enum cannot be built (arbitrary SPICE frame
// names) are intentionally not representable yet: nothing in Milestone 0 needs
// them, and an open string would defeat the type checking above.

}  // namespace sf::coordinates
