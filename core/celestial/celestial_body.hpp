#pragma once

#include "core/celestial/body_id.hpp"

#include <string>

namespace sf::celestial {

// A gravitating body as the dynamics sees it: an id, a GM, and (for reporting)
// a radius.  GM and position are always fetched for the SAME BodyId -- that is
// the mechanism that prevents pairing a barycentre position with a planet GM.
// See docs/physics/gravity-model.md section 3.
struct CelestialBody {
    BodyId id{};
    std::string name;
    double gm{0.0};      // [m^3/s^2]
    double radius{0.0};  // [m], 0 for barycentres
};

}  // namespace sf::celestial
