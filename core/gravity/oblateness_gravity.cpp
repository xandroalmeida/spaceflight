#include "core/gravity/oblateness_gravity.hpp"

#include <cmath>
#include <stdexcept>

namespace sf::gravity {

OblatenessGravity::OblatenessGravity(const ephemeris::EphemerisProvider& provider,
                                     const celestial::BodyOrientationProvider& orientation,
                                     celestial::BodyId body,
                                     celestial::GeopotentialModel geopotential,
                                     coordinates::ReferenceFrame frame)
    : provider_(provider),
      orientation_(orientation),
      body_(body),
      geopotential_(geopotential),
      frame_(frame),
      gm_(provider.gravitational_parameter(body)),
      name_("OblatenessGravity(" + body.name() + ", J2)") {
    if (!frame_.is_inertial()) {
        throw std::invalid_argument("OblatenessGravity: integration frame must be inertial, got " +
                                    frame_.to_string());
    }
    if (!geopotential_.valid()) {
        throw std::invalid_argument("OblatenessGravity: J2 and reference radius must both be set "
                                    "for " + body.name() + " (they are a single physical pair)");
    }
}

std::unique_ptr<OblatenessGravity> OblatenessGravity::for_body(
    const ephemeris::EphemerisProvider& provider,
    const celestial::BodyOrientationProvider& orientation,
    celestial::BodyId body,
    coordinates::ReferenceFrame frame) {
    const auto model = celestial::geopotential_for(body);
    if (!model.has_value()) {
        throw std::invalid_argument("no documented J2 for " + body.name() +
                                    "; see core/celestial/oblateness.cpp");
    }
    return std::make_unique<OblatenessGravity>(provider, orientation, body, *model, frame);
}

ForceResult OblatenessGravity::evaluate(const propagation::PropagationState& spacecraft,
                                        time::CoordinateTime t) const {
    ForceResult result{};

    const math::Vec3 body_position = provider_.position(body_, t, frame_);
    const math::Vec3 r = spacecraft.state.position - body_position;

    const double r2 = r.norm_squared();
    if (r2 <= 0.0) {
        result.inside_body = true;
        result.inside_of = body_;
        return result;
    }
    const double radius = std::sqrt(r2);

    // The spherical harmonic expansion converges only outside the Brillouin
    // sphere.  Inside, the series is not merely inaccurate -- it is divergent, so
    // the result is reported as invalid rather than returned as a number.
    if (radius < geopotential_.reference_radius) {
        result.inside_body = true;
        result.inside_of = body_;
    }

    const math::Vec3 pole = orientation_.pole_direction(body_, t, frame_.axes);
    const double s = dot(r, pole);

    const double factor = 1.5 * geopotential_.j2 * gm_ *
                          geopotential_.reference_radius * geopotential_.reference_radius /
                          (r2 * r2 * radius);
    const double shape = 1.0 - 5.0 * (s * s) / r2;

    result.acceleration = -(r * shape + pole * (2.0 * s)) * factor;
    return result;
}

}  // namespace sf::gravity
