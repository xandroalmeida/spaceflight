#include "core/celestial/oblateness.hpp"

namespace sf::celestial {

std::optional<GeopotentialModel> geopotential_for(BodyId body) {
    // Earth: EGM96.  J2 = -sqrt(5) * Cbar(2,0) with Cbar(2,0) = -4.841653717e-4,
    // referred to the EGM96 reference radius 6378136.3 m (NOT the WGS84 6378137.0).
    if (body == bodies::earth) {
        return GeopotentialModel{1.0826266835e-3, 6378136.3};
    }

    // Moon: classic Lunar Prospector / GRGM value, referred to R = 1738 km.
    if (body == bodies::moon) {
        return GeopotentialModel{2.0323e-4, 1738000.0};
    }

    // Mars: GMM-3, referred to the IAU equatorial radius 3396.19 km.
    if (body == bodies::mars || body == bodies::mars_barycenter) {
        return GeopotentialModel{1.95545e-3, 3396190.0};
    }

    return std::nullopt;
}

}  // namespace sf::celestial
