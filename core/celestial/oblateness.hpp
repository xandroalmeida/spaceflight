#pragma once

// Zonal geopotential coefficients.
//
// This is a DOCUMENTED EXCEPTION to the rule in ADR-0003 that constants come from
// kernels: the loaded kernels contain no J2.  Verified -- pck00011.tpc carries
// radii and orientation, gm_de440.tpc carries GM, and no BODY<n>_J2 exists in the
// pool.  Each value below therefore carries its source in a comment, and the
// moment a geopotential kernel is loaded this table goes away rather than living
// alongside it.

#include "core/celestial/body_id.hpp"

#include <optional>

namespace sf::celestial {

// J2 and the reference radius it was determined against.
//
// These two are an INDIVISIBLE PAIR: only the product J2*R^2 is physical.  Taking
// EGM96's J2 (referred to R = 6378136.3 m) together with WGS84's radius
// (6378137.0 m) introduces a silent relative error of 2.2e-7 in the term.  The
// struct exists so that the two cannot be separated by accident.
struct GeopotentialModel {
    double j2{0.0};               // dimensionless, unnormalised
    double reference_radius{0.0}; // [m]

    [[nodiscard]] constexpr bool valid() const { return j2 != 0.0 && reference_radius > 0.0; }
};

// Returns the tabulated model for a body, or nullopt when we have none.
// Never guesses: a body without a documented J2 gets no oblateness term rather
// than a plausible-looking one.
[[nodiscard]] std::optional<GeopotentialModel> geopotential_for(BodyId body);

}  // namespace sf::celestial
