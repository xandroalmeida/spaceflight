#include "core/celestial/solar_system.hpp"

#include "core/ephemeris/errors.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace sf::celestial {
namespace {

using enum BodyType;
using enum MissionSupport;

// ---------------------------------------------------------------------------
// The table (rules 17-20).
//
// Order is the order the user interface shows: outward from the Sun, each planet
// followed by the moons this project names.
//
// `qualification` is a claim about the SOFTWARE, not about the kernels.  It says
// how far anyone has measured a mission to this body, and `resolve` can only
// lower it -- never raise it.  Exactly one body is Qualified today, because
// exactly one has a campaign behind it (docs/validation/lunar-navigation-
// hardening.md); Mars joins it when docs/validation/earth-mars-transfer.md does.
// ---------------------------------------------------------------------------
constexpr std::array<BodyEntry, 21> kDirectory{{
    {bodies::sun, "Sun", Star, bodies::solar_system_barycenter, Observation},

    {bodies::mercury, "Mercury", Planet, bodies::sun, Experimental},
    {bodies::venus, "Venus", Planet, bodies::sun, Experimental},

    {bodies::earth, "Earth", Planet, bodies::sun, Experimental},
    {bodies::moon, "Moon", Moon, bodies::earth, Qualified},

    {bodies::mars, "Mars", Planet, bodies::sun, Experimental},
    {bodies::phobos, "Phobos", Moon, bodies::mars, Experimental},
    {bodies::deimos, "Deimos", Moon, bodies::mars, Experimental},

    {bodies::jupiter, "Jupiter", Planet, bodies::sun, Experimental},
    {bodies::io, "Io", Moon, bodies::jupiter, Experimental},
    {bodies::europa, "Europa", Moon, bodies::jupiter, Experimental},
    {bodies::ganymede, "Ganymede", Moon, bodies::jupiter, Experimental},
    {bodies::callisto, "Callisto", Moon, bodies::jupiter, Experimental},

    {bodies::saturn, "Saturn", Planet, bodies::sun, Experimental},
    {bodies::enceladus, "Enceladus", Moon, bodies::saturn, Experimental},
    {bodies::titan, "Titan", Moon, bodies::saturn, Experimental},

    {bodies::uranus, "Uranus", Planet, bodies::sun, Experimental},

    {bodies::neptune, "Neptune", Planet, bodies::sun, Experimental},
    {bodies::triton, "Triton", Moon, bodies::neptune, Experimental},

    {bodies::pluto, "Pluto", Planet, bodies::sun, Experimental},
    {bodies::charon, "Charon", Moon, bodies::pluto, Experimental},
}};

// The system barycentre a planet belongs to: 499 -> 4, 599 -> 5.  Used only as a
// stand-in POSITION for a planet whose own SPK segment is not loaded, and never
// for a GM -- a system barycentre's GM is the whole system's, which is the
// mistake docs/physics/gravity-model.md section 3 exists to prevent.
[[nodiscard]] std::optional<BodyId> system_barycenter_of(const BodyEntry& entry) {
    if (entry.type != Planet) {
        return std::nullopt;
    }
    const int barycenter = entry.id.naif_id() / 100;
    if (barycenter < 1 || barycenter > 9) {
        return std::nullopt;
    }
    return BodyId{barycenter};
}

[[nodiscard]] bool can_place(const ephemeris::EphemerisProvider& provider, BodyId body,
                             time::CoordinateTime probe) {
    try {
        return provider.has_body(body) && provider.coverage(body).contains(probe);
    } catch (const ephemeris::SpaceflightError&) {
        return false;
    }
}

template <typename Fn>
[[nodiscard]] double value_or_zero(Fn&& fn) {
    try {
        return fn();
    } catch (const ephemeris::SpaceflightError&) {
        return 0.0;
    }
}

[[nodiscard]] std::string lowered(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

}  // namespace

std::string_view to_string(BodyType type) {
    switch (type) {
        case Star:       return "star";
        case Planet:     return "planet";
        case Moon:       return "moon";
        case Barycenter: return "barycenter";
    }
    return "?";
}

std::string_view to_string(MissionSupport support) {
    switch (support) {
        case Observation:  return "observation";
        case Experimental: return "experimental";
        case Qualified:    return "qualified";
    }
    return "?";
}

std::span<const BodyEntry> body_directory() { return kDirectory; }

const BodyEntry* find_entry(BodyId id) {
    const auto it = std::find_if(kDirectory.begin(), kDirectory.end(),
                                 [id](const BodyEntry& e) { return e.id == id; });
    return it == kDirectory.end() ? nullptr : &*it;
}

std::vector<BodyId> children_of(BodyId parent) {
    std::vector<BodyId> out;
    for (const auto& entry : kDirectory) {
        if (entry.parent == parent && entry.id != parent) {
            out.push_back(entry.id);
        }
    }
    return out;
}

std::vector<BodyId> ancestry_of(BodyId body) {
    std::vector<BodyId> chain{body};
    const BodyEntry* entry = find_entry(body);
    // Bounded by the table's depth; the guard is against a table that has been
    // edited into a cycle, which would otherwise hang the user interface.
    for (std::size_t guard = 0; entry != nullptr && guard < kDirectory.size(); ++guard) {
        const BodyId parent = entry->parent;
        chain.push_back(parent);
        if (parent == bodies::solar_system_barycenter) {
            break;
        }
        entry = find_entry(parent);
    }
    return chain;
}

std::optional<BodyId> common_primary(BodyId a, BodyId b) {
    const auto chain_a = ancestry_of(a);
    const auto chain_b = ancestry_of(b);
    for (const BodyId candidate : chain_a) {
        if (std::find(chain_b.begin(), chain_b.end(), candidate) != chain_b.end()) {
            return candidate;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------

SolarSystem SolarSystem::resolve(const ephemeris::EphemerisProvider& provider,
                                 time::CoordinateTime probe) {
    std::vector<SolarSystemBody> resolved;
    resolved.reserve(kDirectory.size());

    for (const auto& entry : kDirectory) {
        SolarSystemBody body{};
        body.entry = entry;
        body.gm = value_or_zero([&] { return provider.gravitational_parameter(entry.id); });
        body.radius = value_or_zero([&] { return provider.mean_radius(entry.id); });

        if (can_place(provider, entry.id, probe)) {
            body.ephemeris_source = entry.id;
            body.has_ephemeris = true;
        } else if (const auto barycenter = system_barycenter_of(entry);
                   barycenter.has_value() && can_place(provider, *barycenter, probe)) {
            body.ephemeris_source = *barycenter;
            body.has_ephemeris = true;
            body.position_from_barycenter = true;
        } else {
            body.ephemeris_source = entry.id;
            body.has_ephemeris = false;
        }

        // The kernels can only LOWER what the table claims.  A body nobody can
        // place is not experimental, it is unavailable; a body standing in for
        // its own barycentre can be looked at and not flown to.
        body.support = entry.qualification;
        if (!body.can_be_destination()) {
            body.support = Observation;
        }

        resolved.push_back(body);
    }

    return SolarSystem{std::move(resolved)};
}

const SolarSystemBody* SolarSystem::find(BodyId id) const {
    const auto it = std::find_if(bodies_.begin(), bodies_.end(),
                                 [id](const SolarSystemBody& b) { return b.entry.id == id; });
    return it == bodies_.end() ? nullptr : &*it;
}

const SolarSystemBody* SolarSystem::find(std::string_view name) const {
    const std::string needle = lowered(name);
    const auto it = std::find_if(bodies_.begin(), bodies_.end(), [&](const SolarSystemBody& b) {
        return lowered(b.entry.name) == needle;
    });
    return it == bodies_.end() ? nullptr : &*it;
}

std::vector<BodyId> SolarSystem::destinations() const {
    std::vector<BodyId> out;
    for (const auto& body : bodies_) {
        if (body.can_be_destination()) {
            out.push_back(body.entry.id);
        }
    }
    return out;
}

std::vector<BodyId> SolarSystem::renderable() const {
    std::vector<BodyId> out;
    for (const auto& body : bodies_) {
        if (body.has_ephemeris && body.radius > 0.0) {
            out.push_back(body.entry.id);
        }
    }
    return out;
}

std::string SolarSystem::describe() const {
    std::ostringstream os;
    os << "solar system directory: " << bodies_.size() << " bodies\n";
    for (const auto& body : bodies_) {
        os << "  " << body.entry.name << " (" << body.entry.id.naif_id() << ")"
           << "  " << to_string(body.entry.type) << "  parent "
           << body.entry.parent.naif_id() << "  " << to_string(body.support);
        if (!body.has_ephemeris) {
            os << "  [no ephemeris]";
        } else if (body.position_from_barycenter) {
            os << "  [position from barycentre " << body.ephemeris_source.naif_id() << "]";
        }
        os << "\n";
    }
    return os.str();
}

}  // namespace sf::celestial
