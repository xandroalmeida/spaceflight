#pragma once

// Ephemeris providers with known-by-construction answers.
//
// These exist so that the propagator can be tested against a closed-form
// solution: with a single point mass fixed at the origin, the trajectory is an
// exact Kepler orbit, and any deviation is integration error -- not ephemeris
// error, not third bodies.  That separation is what makes a tolerance defensible.

#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/ephemeris/errors.hpp"

#include <stdexcept>

namespace sft {

class FixedPointMassProvider final : public sf::ephemeris::EphemerisProvider {
public:
    FixedPointMassProvider(sf::celestial::BodyId body, double gm, double radius = 0.0,
                           sf::math::Vec3 position = {})
        : body_(body), gm_(gm), radius_(radius), position_(position) {}

    [[nodiscard]] sf::ephemeris::BodyState state(sf::celestial::BodyId body,
                                                 sf::time::CoordinateTime t,
                                                 sf::coordinates::ReferenceFrame frame) const override {
        require_known(body);
        sf::ephemeris::BodyState out{};
        out.body = body;
        out.epoch = t;
        out.frame = frame;
        out.state.position = position_;  // at rest: the two-body problem, exactly
        return out;
    }

    [[nodiscard]] double gravitational_parameter(sf::celestial::BodyId body) const override {
        require_known(body);
        return gm_;
    }

    [[nodiscard]] double mean_radius(sf::celestial::BodyId body) const override {
        require_known(body);
        return radius_;
    }

    [[nodiscard]] sf::ephemeris::CoverageWindow coverage(sf::celestial::BodyId body) const override {
        sf::ephemeris::CoverageWindow w{};
        if (body == body_) {
            w.begin = sf::time::CoordinateTime::from_seconds_since_j2000(-1.0e18);
            w.end = sf::time::CoordinateTime::from_seconds_since_j2000(1.0e18);
            w.valid = true;
        }
        return w;
    }

    [[nodiscard]] bool has_body(sf::celestial::BodyId body) const override { return body == body_; }

private:
    void require_known(sf::celestial::BodyId body) const {
        if (body != body_) {
            throw sf::ephemeris::EphemerisUnavailable("FixedPointMassProvider: unknown body " +
                                                      body.name());
        }
    }

    sf::celestial::BodyId body_;
    double gm_;
    double radius_;
    sf::math::Vec3 position_;
};

}  // namespace sft
