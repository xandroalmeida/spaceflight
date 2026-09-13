#include "core/gravity/weak_field_metric.hpp"

#include "core/units/constants.hpp"

#include <cmath>
#include <stdexcept>

namespace sf::gravity {

using math::Vec3;

double MetricSample::time_component(const Vec3& proper_velocity) const {
    return std::sqrt((units::c_squared + b * proper_velocity.norm_squared()) / a);
}

Vec3 MetricSample::coordinate_velocity(const Vec3& proper_velocity) const {
    return proper_velocity * (units::c / time_component(proper_velocity));
}

double MetricSample::proper_time_rate(const Vec3& proper_velocity) const {
    return units::c / time_component(proper_velocity);
}

Vec3 MetricSample::geodesic_acceleration(const Vec3& proper_velocity) const {
    const double u0 = time_component(proper_velocity);
    const double u2 = proper_velocity.norm_squared();
    const Vec3 term = grad_a * (u0 * u0) + proper_velocity * (2.0 * dot(grad_b, proper_velocity)) -
                      grad_b * u2;
    return term * (-0.5 / b);
}

WeakFieldMetric::WeakFieldMetric(const ephemeris::EphemerisProvider& provider,
                                 const celestial::BodyCatalog& catalog,
                                 coordinates::ReferenceFrame frame)
    : provider_(provider), catalog_(catalog), frame_(frame) {
    if (!frame_.is_inertial()) {
        throw std::invalid_argument("WeakFieldMetric: the frame must be inertial, got " +
                                    frame_.to_string());
    }
    if (catalog_.empty()) {
        throw std::invalid_argument("WeakFieldMetric: empty body catalogue");
    }
}

MetricSample WeakFieldMetric::sample(const Vec3& position, time::CoordinateTime t) const {
    MetricSample out{};

    // Fixed summation order, as in PointMassGravity: two runs of the same
    // scenario must give bit-identical results.
    for (const auto& body : catalog_.bodies()) {
        const Vec3 body_position = provider_.position(body.id, t, frame_);
        const Vec3 d = position - body_position;
        const double d2 = d.norm_squared();
        const double distance = std::sqrt(d2);

        if (distance <= 0.0) {
            out.inside_body = true;
            out.inside_of = body.id;
            continue;
        }

        out.potential += body.gm / distance;
        // grad(GM/r) = -GM (x - x_a)/r^3, which is exactly the Newtonian
        // acceleration. The relativistic layer reuses that number instead of
        // recomputing it, and the Newtonian-limit test compares the two.
        out.potential_gradient -= d * (body.gm / (d2 * distance));

        if (body.radius > 0.0 && distance < body.radius) {
            out.inside_body = true;
            out.inside_of = body.id;
        }
    }

    const double u_over_c2 = out.potential / units::c_squared;
    out.a = 1.0 - 2.0 * u_over_c2 + 2.0 * u_over_c2 * u_over_c2;
    out.b = 1.0 + 2.0 * u_over_c2;
    out.grad_a = out.potential_gradient * (-2.0 / units::c_squared +
                                           4.0 * out.potential / (units::c_squared * units::c_squared));
    out.grad_b = out.potential_gradient * (2.0 / units::c_squared);
    out.local_light_speed = units::c * std::sqrt(out.a / out.b);
    return out;
}

}  // namespace sf::gravity
