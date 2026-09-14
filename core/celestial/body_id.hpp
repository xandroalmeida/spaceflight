#pragma once

// NAIF integer body identifier.
//
// Strong type over `int` because the NAIF numbering is full of traps that a raw
// int hides: 5 is the *Jupiter system barycentre*, 599 is the planet; 3 is the
// Earth-Moon barycentre, 399 is the Earth.  Mixing them up is a silent physics
// error (see docs/physics/gravity-model.md section 3).

#include <compare>
#include <string>
#include <string_view>

namespace sf::celestial {

class BodyId {
public:
    constexpr BodyId() = default;
    explicit constexpr BodyId(int naif_id) : id_(naif_id) {}

    [[nodiscard]] constexpr int naif_id() const { return id_; }

    // True for ids 1..9: those are *system barycentres*, whose GM is the GM of
    // the whole planetary system, not of the planet.
    [[nodiscard]] constexpr bool is_planetary_barycenter() const { return id_ >= 1 && id_ <= 9; }
    [[nodiscard]] constexpr bool is_solar_system_barycenter() const { return id_ == 0; }

    // Name from the built-in table for the bodies this project uses.  Falls back
    // to "NAIF <id>"; the authoritative name lookup is SPICE's bodc2n_c.
    [[nodiscard]] std::string name() const;

    friend constexpr auto operator<=>(const BodyId&, const BodyId&) = default;

private:
    int id_{0};
};

namespace bodies {
inline constexpr BodyId solar_system_barycenter{0};
inline constexpr BodyId mercury_barycenter{1};
inline constexpr BodyId venus_barycenter{2};
inline constexpr BodyId earth_moon_barycenter{3};
inline constexpr BodyId mars_barycenter{4};
inline constexpr BodyId jupiter_barycenter{5};
inline constexpr BodyId saturn_barycenter{6};
inline constexpr BodyId uranus_barycenter{7};
inline constexpr BodyId neptune_barycenter{8};
inline constexpr BodyId pluto_barycenter{9};

inline constexpr BodyId sun{10};
inline constexpr BodyId mercury{199};
inline constexpr BodyId venus{299};
inline constexpr BodyId earth{399};
inline constexpr BodyId moon{301};
inline constexpr BodyId mars{499};
inline constexpr BodyId jupiter{599};
inline constexpr BodyId saturn{699};
inline constexpr BodyId uranus{799};
inline constexpr BodyId neptune{899};
inline constexpr BodyId pluto{999};

// The moons Milestone 8 names.  Whether any of them can actually be PLACED is a
// question about the loaded kernels and is answered by
// core/celestial/solar_system.hpp -- an id is not an ephemeris.
inline constexpr BodyId phobos{401};
inline constexpr BodyId deimos{402};
inline constexpr BodyId io{501};
inline constexpr BodyId europa{502};
inline constexpr BodyId ganymede{503};
inline constexpr BodyId callisto{504};
inline constexpr BodyId enceladus{602};
inline constexpr BodyId titan{606};
inline constexpr BodyId triton{801};
inline constexpr BodyId charon{901};
}  // namespace bodies

// Case-insensitive lookup over the built-in table ("earth", "MOON", "399").
// Returns BodyId{} with `ok == false` when unknown; SPICE can resolve more.
struct BodyLookup {
    BodyId id;
    bool ok{false};
};
BodyLookup body_from_name(std::string_view name);

}  // namespace sf::celestial
