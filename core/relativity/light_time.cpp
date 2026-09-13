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
        const math::Vec3 body = provider.position(target, retarded, frame);
        const double distance = (body - observer_position).norm();
        const double updated = distance / units::c;

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
