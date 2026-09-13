#pragma once

// What a moving observer SEES: aberration, Doppler, beaming.
//
// These are the transformations between "where the light came from and what it
// carried" in the coordinate frame and in the ship's rest frame. They change the
// image and nothing else -- no function here touches a state
// (docs/physics/relativistic-rendering.md).
//
// Convention throughout: `beta` is the observer's velocity divided by c, in the
// coordinate frame. Two direction conventions appear in the literature and are
// both provided, named, because mixing them flips the sign of every effect:
//
//   propagation direction  n : the way the photon travels
//   source direction       s : the way you look to see it,  s = -n

#include "core/math/vec3.hpp"
#include "core/units/constants.hpp"

#include <cmath>

namespace sf::relativity {

// gamma for a coordinate-velocity beta. Fine here because rendering never runs
// at the extreme where 1/sqrt(1-beta^2) loses its digits; the STATE still uses
// the u-based form (docs/physics/relativistic-propulsion.md section 7).
[[nodiscard]] inline double gamma_from_beta(const math::Vec3& beta) {
    const double b2 = beta.norm_squared();
    return b2 < 1.0 ? 1.0 / std::sqrt(1.0 - b2) : 1.0;
}

// Aberration of a photon's propagation direction:
//
//            n + (gamma - 1)(n.beta_hat) beta_hat - gamma beta
//   n'  =  ----------------------------------------------------
//                        gamma (1 - beta.n)
//
// Reduces to the identity at beta = 0 and to the classical v/c shift at small
// beta.
[[nodiscard]] inline math::Vec3 aberrate_propagation(const math::Vec3& propagation,
                                                     const math::Vec3& beta) {
    const double speed = beta.norm();
    if (speed <= 0.0) {
        return propagation.normalized();
    }
    const math::Vec3 n = propagation.normalized();
    const math::Vec3 beta_hat = beta / speed;
    const double gamma = gamma_from_beta(beta);

    const math::Vec3 numerator =
        n + beta_hat * ((gamma - 1.0) * dot(n, beta_hat)) - beta * gamma;
    const double denominator = gamma * (1.0 - dot(beta, n));
    return numerator / denominator;
}

// The same thing for the direction you point a telescope in. In scalar form,
// with theta measured from the velocity,
//
//   cos theta' = (cos theta + beta) / (1 + beta cos theta)
//
// so a source at 90 degrees appears at arccos(beta): the sky crowds forwards.
[[nodiscard]] inline math::Vec3 aberrate_source_direction(const math::Vec3& to_source,
                                                          const math::Vec3& beta) {
    return -aberrate_propagation(-to_source, beta);
}

// Doppler factor D = f_observed / f_emitted = gamma (1 - beta.n), for a source at
// rest in the coordinate frame. Ahead of the ship, beta.n = -beta and D > 1.
[[nodiscard]] inline double doppler_factor(const math::Vec3& propagation,
                                           const math::Vec3& beta) {
    return gamma_from_beta(beta) * (1.0 - dot(beta, propagation.normalized()));
}

[[nodiscard]] inline double doppler_factor_to_source(const math::Vec3& to_source,
                                                     const math::Vec3& beta) {
    return doppler_factor(-to_source, beta);
}

// Bolometric intensity transforms as D^4, because I_nu/nu^3 is invariant. The
// fourth power is what makes the forward sky 400x brighter at beta = 0.9.
[[nodiscard]] inline double beaming_factor(double doppler) {
    const double d2 = doppler * doppler;
    return d2 * d2;
}

// A Doppler-shifted black body is still a black body, at T' = D T. Not an
// approximation: the Planck shape survives the transformation, and only the
// temperature moves. It is what lets a star field be shifted exactly rather than
// stylised (docs/physics/relativistic-rendering.md section 4).
[[nodiscard]] inline double shifted_temperature(double temperature, double doppler) {
    return temperature * doppler;
}

}  // namespace sf::relativity
