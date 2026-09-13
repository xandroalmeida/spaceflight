#pragma once

// The set of bodies whose gravity is included in a simulation, with their GM
// resolved once from the ephemeris data source.

#include "core/celestial/celestial_body.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"

#include <span>
#include <vector>

namespace sf::celestial {

class BodyCatalog {
public:
    BodyCatalog() = default;

    // Sun, Mercury/Venus/Mars/Jupiter/Saturn/Uranus/Neptune barycentres, Earth
    // and Moon separately.  Rationale for barycentres vs planets:
    // docs/physics/gravity-model.md section 3.
    static std::vector<BodyId> default_solar_system_ids();

    // Resolves GM and radius for each id through the provider.  Throws
    // EphemerisUnavailable if a body has no GM in the kernel pool -- a silently
    // massless planet would be far worse than a loud failure.
    static BodyCatalog resolve(const ephemeris::EphemerisProvider& provider,
                               std::span<const BodyId> ids);

    static BodyCatalog default_solar_system(const ephemeris::EphemerisProvider& provider);

    [[nodiscard]] const std::vector<CelestialBody>& bodies() const noexcept { return bodies_; }
    [[nodiscard]] const CelestialBody* find(BodyId id) const;
    [[nodiscard]] std::size_t size() const noexcept { return bodies_.size(); }
    [[nodiscard]] bool empty() const noexcept { return bodies_.empty(); }

private:
    explicit BodyCatalog(std::vector<CelestialBody> bodies) : bodies_(std::move(bodies)) {}

    std::vector<CelestialBody> bodies_;
};

}  // namespace sf::celestial
