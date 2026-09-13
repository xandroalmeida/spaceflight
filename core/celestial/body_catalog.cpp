#include "core/celestial/body_catalog.hpp"

#include <algorithm>

namespace sf::celestial {

std::vector<BodyId> BodyCatalog::default_solar_system_ids() {
    return {
        bodies::sun,
        bodies::mercury_barycenter,
        bodies::venus_barycenter,
        bodies::earth,   // Earth and Moon are resolved separately: the simulation
        bodies::moon,    // operates inside the Earth-Moon system.
        bodies::mars_barycenter,
        bodies::jupiter_barycenter,
        bodies::saturn_barycenter,
        bodies::uranus_barycenter,
        bodies::neptune_barycenter,
    };
}

BodyCatalog BodyCatalog::resolve(const ephemeris::EphemerisProvider& provider,
                                 std::span<const BodyId> ids) {
    std::vector<CelestialBody> resolved;
    resolved.reserve(ids.size());
    for (const BodyId id : ids) {
        CelestialBody body{};
        body.id = id;
        body.name = id.name();
        body.gm = provider.gravitational_parameter(id);  // throws if absent
        body.radius = provider.mean_radius(id);
        resolved.push_back(std::move(body));
    }
    return BodyCatalog{std::move(resolved)};
}

BodyCatalog BodyCatalog::default_solar_system(const ephemeris::EphemerisProvider& provider) {
    const auto ids = default_solar_system_ids();
    return resolve(provider, ids);
}

const CelestialBody* BodyCatalog::find(BodyId id) const {
    const auto it = std::find_if(bodies_.begin(), bodies_.end(),
                                 [id](const CelestialBody& b) { return b.id == id; });
    return it == bodies_.end() ? nullptr : &*it;
}

}  // namespace sf::celestial
