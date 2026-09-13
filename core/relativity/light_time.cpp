#include "core/relativity/light_time.hpp"

#include "core/relativity/optics.hpp"
#include "core/units/constants.hpp"

#include <cmath>

namespace sf::relativity {

ApparentPosition apparent_position(const ephemeris::EphemerisProvider& provider,
                                   celestial::BodyId target,
                                   const math::Vec3& observer_position, time::CoordinateTime t,
                                   coordinates::ReferenceFrame frame, double tolerance_seconds,
                                   int max_iterations) {
    ApparentPosition result{};
    result.retarded_epoch = t;

    double light_time = 0.0;
    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        const auto retarded = t - time::Duration::seconds(light_time);
        const auto body = provider.state(target, retarded, frame).state;
        const math::Vec3 relative = body.position - observer_position;
        const double distance = relative.norm();
        const double residual = light_time - distance / units::c;

        // Newton derivative of F(L)=L-|x(t-L)-x_obs(t)|/c:
        // F'(L)=1+r_hat.v_target/c. Unlike fixed-point iteration, whose
        // contraction factor approaches one for a relativistic source moving
        // toward the observer, this remains fast up to the light cone. If an
        // invalid/superluminal ephemeris makes the derivative non-positive,
        // retain the bounded fixed-point step and report non-convergence.
        const double slope = distance > 0.0
                                 ? 1.0 + dot(relative / distance, body.velocity) / units::c
                                 : 1.0;
        double updated = distance / units::c;
        if (slope > 0.0 && std::isfinite(slope)) {
            updated = light_time - residual / slope;
        }
        if (!(updated >= 0.0) || !std::isfinite(updated)) {
            updated = distance / units::c;
        }

        result.iterations = iteration + 1;
        if (std::abs(updated - light_time) <= tolerance_seconds) {
            light_time = updated;
            result.converged = true;
            break;
        }
        light_time = updated;
    }

    result.light_time = light_time;
    result.retarded_epoch = t - time::Duration::seconds(light_time);

    const auto state = provider.state(target, result.retarded_epoch, frame);
    result.body_position = state.state.position;
    result.relative_position = state.state.position - observer_position;
    result.relative_velocity = state.state.velocity;
    return result;
}

math::Vec3 apparent_direction(const ephemeris::EphemerisProvider& provider,
                              celestial::BodyId target, const math::Vec3& observer_position,
                              const math::Vec3& observer_velocity, time::CoordinateTime t,
                              coordinates::ReferenceFrame frame) {
    const auto apparent = apparent_position(provider, target, observer_position, t, frame);
    const math::Vec3 beta = observer_velocity / units::c;
    return aberrate_source_direction(apparent.relative_position, beta);
}

}  // namespace sf::relativity
