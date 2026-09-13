#include "core/gravity/point_mass_gravity.hpp"

#include "core/ephemeris/errors.hpp"

#include <stdexcept>

namespace sf::gravity {

PointMassGravity::PointMassGravity(const ephemeris::EphemerisProvider& provider,
                                   const celestial::BodyCatalog& catalog,
                                   coordinates::ReferenceFrame frame)
    : provider_(provider), catalog_(catalog), frame_(frame) {
    if (!frame_.is_inertial()) {
        throw std::invalid_argument(
            "PointMassGravity: integration frame must be inertial, got " + frame_.to_string());
    }
    if (catalog_.empty()) {
        throw std::invalid_argument("PointMassGravity: empty body catalog");
    }
}

ForceResult PointMassGravity::evaluate(const propagation::PropagationState& spacecraft,
                                       time::CoordinateTime t) const {
    ForceResult result{};
    const math::Vec3& r = spacecraft.state.position;

    // Fixed summation order (the catalog order), so that two runs of the same
    // scenario produce bit-identical results.  See gravity-model.md section 6.
    for (const auto& body : catalog_.bodies()) {
        const math::Vec3 r_body = provider_.position(body.id, t, frame_);
        const math::Vec3 d = r - r_body;
        const double d2 = d.norm_squared();
        const double dist = std::sqrt(d2);

        if (dist <= 0.0) {
            // Exactly at a point mass: the model has no answer.  Report rather
            // than invent one.
            result.inside_body = true;
            result.inside_of = body.id;
            continue;
        }

        result.acceleration -= d * (body.gm / (d2 * dist));

        if (body.radius > 0.0 && dist < body.radius) {
            result.inside_body = true;
            result.inside_of = body.id;
        }
    }

    return result;
}

std::vector<GravityContribution> PointMassGravity::contributions(const math::Vec3& position,
                                                                 time::CoordinateTime t) const {
    std::vector<GravityContribution> out;
    out.reserve(catalog_.size());
    for (const auto& body : catalog_.bodies()) {
        const math::Vec3 r_body = provider_.position(body.id, t, frame_);
        const math::Vec3 d = position - r_body;
        const double d2 = d.norm_squared();
        const double dist = std::sqrt(d2);
        GravityContribution c{};
        c.body = body.id;
        c.distance = dist;
        c.acceleration = dist > 0.0 ? -d * (body.gm / (d2 * dist)) : math::Vec3{};
        out.push_back(c);
    }
    return out;
}

}  // namespace sf::gravity
