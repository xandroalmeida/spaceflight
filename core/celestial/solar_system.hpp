#pragma once

// The Solar System as a DIRECTORY: what exists, what orbits what, and which of
// it the mission planner can actually be pointed at.
//
// ---------------------------------------------------------------------------
// Why this is separate from BodyCatalog
//
// BodyCatalog answers ONE question -- whose gravity is in the force model -- and
// its membership is chosen for that: it holds Mars *Barycenter* because the GM
// of the Mars system is what a point-mass term 200 million kilometres away needs
// (docs/physics/gravity-model.md section 3).  A barycentre is not a place.  You
// cannot orbit it, land on it, draw it, or aim a capture burn at it.
//
// So asking BodyCatalog "what can the pilot fly to?" gets the wrong answer in
// both directions: it offers points in empty space, and it omits every body
// whose mass is too small to perturb anything -- which is most of the interesting
// ones.  Milestone 7 papered over this with `radius > 0`, which worked while the
// only destination was the Moon.
//
// This file is the other question.  It is the ONE table of body identity and
// hierarchy, and core, the planner, the GDExtension and the user interface all
// read it rather than each keeping a list (Milestone 8 rule 19).
//
// ---------------------------------------------------------------------------
// What is data here and what is not
//
// Identity, type, parent and how far the planner has been qualified: data, in
// the table below.  GM, radius and whether the kernels can even place the body:
// NOT in the table -- they are resolved from the ephemeris at runtime, because
// duplicating a GM here is how the number the gravity model uses and the number
// the display shows start to disagree (rule 79).
//
// See docs/architecture/general-mission-planning.md section 2.

#include "core/celestial/body_id.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/time/coordinate_time.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf::celestial {

enum class BodyType {
    Star,
    Planet,
    Moon,
    Barycenter
};

[[nodiscard]] std::string_view to_string(BodyType type);

// How far the mission planner has been QUALIFIED for this body as a destination.
//
// Three levels and not a boolean, because "the code runs" and "a campaign says
// it works" are different claims and the cockpit has to be able to make the
// weaker one without making the stronger one (rule 21: do not pretend complete
// support).
enum class MissionSupport {
    // Observation only.  Either the kernels cannot place the body, or no
    // transfer strategy applies to the origin/destination pair.
    Observation,

    // A strategy applies and nothing has measured it.  The planner will try.
    Experimental,

    // A validation campaign stands behind it.
    Qualified
};

[[nodiscard]] std::string_view to_string(MissionSupport support);

struct BodyEntry {
    BodyId id{};
    std::string_view name;
    BodyType type{BodyType::Planet};

    // What it orbits.  The Sun's parent is the solar system barycentre, which is
    // true (the Sun does orbit it) and makes the hierarchy a tree with one root.
    BodyId parent{};

    // The ceiling on what `resolve` may report for this body.  A body the
    // kernels cannot place is Observation regardless of what is written here;
    // this is the OTHER half of the question -- whether anyone has flown it.
    MissionSupport qualification{MissionSupport::Experimental};
};

// The table.  Ordered as the user interface wants to show it: the Sun, then the
// planets outward, each followed by its moons.
[[nodiscard]] std::span<const BodyEntry> body_directory();

[[nodiscard]] const BodyEntry* find_entry(BodyId id);

// Direct children only, in directory order.
[[nodiscard]] std::vector<BodyId> children_of(BodyId parent);

// The chain from `body` up to the solar system barycentre, `body` first.
[[nodiscard]] std::vector<BodyId> ancestry_of(BodyId body);

// The nearest body both are in orbit about: the Earth for Earth and the Moon,
// the Sun for the Earth and Mars.  Nullopt when they share no ancestor, which
// cannot happen inside this table and is not assumed away.
[[nodiscard]] std::optional<BodyId> common_primary(BodyId a, BodyId b);

// ---------------------------------------------------------------------------
// One entry, resolved against a loaded kernel set.
// ---------------------------------------------------------------------------
struct SolarSystemBody {
    BodyEntry entry{};

    double gm{0.0};      // [m^3/s^2], 0 when the kernels have none
    double radius{0.0};  // [m], mean; 0 for barycentres and for bodies with no PCK radii

    // Which body id is actually queried for a position.  Equal to `entry.id`
    // whenever the kernels can place it.
    //
    // It differs for the giant planets, whose own SPK segments are in kernels of
    // several hundred megabytes that this project does not ship: there the
    // system BARYCENTRE stands in.  That substitution is legitimate for drawing
    // and refused for planning, and the reason is the same fact in both
    // directions -- a planet's system barycentre sits inside the planet, because
    // its moons are a ten-thousandth of its mass.  Measured for Mars, where both
    // are available: 0.10 m apart.  Good enough to draw Jupiter; not good enough
    // to be the centre of a capture orbit, and nothing here pretends it is.
    BodyId ephemeris_source{};

    // True when `ephemeris_source != entry.id`.
    bool position_from_barycenter{false};

    // False when nothing in the loaded kernels can place the body at all.
    bool has_ephemeris{false};

    // What `entry.qualification` survives once the kernels have had their say.
    MissionSupport support{MissionSupport::Observation};

    // A destination has to be a PLACE with a centre the kernels know: a capture
    // orbit is about a body, and an orbit about a barycentre is about a point.
    [[nodiscard]] bool can_be_destination() const {
        return has_ephemeris && !position_from_barycenter && gm > 0.0 && radius > 0.0;
    }
};

// The directory, resolved.
//
// `probe` is the epoch at which availability is tested.  An epoch and not a
// flag, because "the kernels can place Mars" is a statement about a time span:
// mar099s.bsp covers 1995-2050 and says SPKINSUFFDATA outside it, and a game
// that starts in 2075 should be told that at load rather than mid-flight.
class SolarSystem {
public:
    [[nodiscard]] static SolarSystem resolve(const ephemeris::EphemerisProvider& provider,
                                             time::CoordinateTime probe);

    [[nodiscard]] const std::vector<SolarSystemBody>& bodies() const noexcept { return bodies_; }
    [[nodiscard]] const SolarSystemBody* find(BodyId id) const;
    [[nodiscard]] const SolarSystemBody* find(std::string_view name) const;

    // Everything that can be pointed at, in directory order.
    [[nodiscard]] std::vector<BodyId> destinations() const;

    // Everything with a position and a radius: what the renderer draws.
    [[nodiscard]] std::vector<BodyId> renderable() const;

    [[nodiscard]] std::string describe() const;

private:
    explicit SolarSystem(std::vector<SolarSystemBody> bodies) : bodies_(std::move(bodies)) {}
    std::vector<SolarSystemBody> bodies_;
};

}  // namespace sf::celestial
