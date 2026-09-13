#pragma once

// The Yale Bright Star Catalogue, read as it is published.
//
// The Milestone 2 star field was seeded noise, and godot/README.md said so.  This
// reads BSC5 (Hoffleit & Warren 1991, VizieR V/50): 9110 records, fixed-width
// ASCII, the whole naked-eye sky.
//
// Three quantities per star, and each has a job:
//
//   direction  right ascension / declination, J2000 -- the SAME axes as the
//              integration frame (docs/architecture/coordinate-system.md), so the
//              unit vector enters the scene with no rotation at all
//   flux       from V, because a magnitude IS a band-limited flux, which is
//              exactly what section 10.1 needs
//   temperature from B-V, because a Doppler shift is a temperature shift
//              (section 4) and there is nothing to shift without one
//
// See docs/physics/relativistic-rendering.md section 12.

#include "core/math/vec3.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace sf::render {

struct CatalogStar {
    int hr{0};                     // Harvard Revised number, the catalogue's key
    std::string name;              // Bayer/Flamsteed, often blank
    math::Vec3 direction{};        // unit, equatorial J2000
    double visual_magnitude{0.0};  // V
    double colour_index{0.0};      // B-V
    double temperature{0.0};       // [K], from colour_index
    double rest_flux{0.0};         // relative to V = 0
};

// What the catalogue could not give, counted rather than silently dropped: a
// loader that reports 8786 stars from a 9110-record file has to say where the
// other 324 went, or the next person will assume a parser bug.
struct StarCatalogReport {
    std::size_t records_read{0};
    std::size_t without_coordinates{0};  // BSC5's own non-existent entries
    std::size_t without_colour{0};       // no B-V, so no temperature
    std::size_t accepted{0};
};

class StarCatalog {
public:
    StarCatalog() = default;

    // Reads BSC5 fixed-width ASCII.  Throws std::runtime_error if the file cannot
    // be opened; a malformed LINE is skipped and counted, because a catalogue with
    // three bad rows is still a catalogue.
    static StarCatalog from_bsc5_file(const std::string& path);
    static StarCatalog from_bsc5_text(const std::string& text);

    // A catalogue built by hand, for tests and for the starfield validation
    // harness.  The real sky is a poor first instrument: 8786 stars all over the
    // sphere cannot tell "the projection is wrong" from "the photometry returned
    // zero", because every wrong answer looks like an empty screen.  Six stars on
    // the axes can, and that is what docs/validation/starfield-debug.md section 5
    // starts with.
    //
    // `direction` is normalised here; `temperature` and `visual_magnitude` are
    // taken as given, and `rest_flux` and `colour_index` are derived so that a
    // synthetic star goes through exactly the same arithmetic downstream as a
    // BSC5 one.  Throws std::invalid_argument for a zero direction or a
    // non-positive temperature -- neither is a dim star, both are a caller error.
    static StarCatalog from_stars(const std::vector<CatalogStar>& stars);

    // Builder form, so a harness can name what it is making.
    void add_star(const math::Vec3& direction, double temperature, double visual_magnitude,
                  std::string name = {});

    [[nodiscard]] const std::vector<CatalogStar>& stars() const noexcept { return stars_; }
    [[nodiscard]] std::size_t size() const noexcept { return stars_.size(); }
    [[nodiscard]] bool empty() const noexcept { return stars_.empty(); }
    [[nodiscard]] const StarCatalogReport& report() const noexcept { return report_; }

    // Keep only stars at or brighter than a magnitude limit.  A presentation
    // choice -- BSC5 reaches V = 7.96, two magnitudes past the naked eye -- and
    // therefore explicit rather than a constant inside the loader.
    void keep_brighter_than(double magnitude_limit);

    [[nodiscard]] std::string describe() const;

    // Where bsc5.dat lives by default: SPACEFLIGHT_CATALOG_DIR if set, else the
    // path compiled in at configure time.  Same mechanism as
    // SpiceKernelSet::default_directory(), for the same reason -- the data is
    // fetched, not committed, and a build tree must still find it.
    static std::string default_path();

private:
    std::vector<CatalogStar> stars_;
    StarCatalogReport report_{};
};

// Ballesteros (2012), EPL 97, 34008: the star as two black bodies seen through
// the B and V bands, inverted for T.
//
//   T = 4600 K [ 1/(0.92(B-V) + 1.70) + 1/(0.92(B-V) + 0.62) ]
//
// Calibrated so that the Sun's B-V = 0.656 returns 5772 K exactly.  Good to ~5%
// between 4000 and 10000 K and poor at the hot end -- -15% for Achernar -- which
// is in the validity table of docs/physics/relativistic-rendering.md section 7.
[[nodiscard]] double temperature_from_colour_index(double colour_index);

// The same relation read the other way, for a star that was DEFINED by its
// temperature -- a synthetic one.  Ballesteros is a quadratic in b = 0.92 (B-V):
//
//     (T/4600) b^2 + (2.32 T/4600 - 2) b + (1.054 T/4600 - 2.32) = 0
//
// and the root taken is the one on the physical branch, where B-V falls as the
// star gets hotter.  Exact inverse, not a fit: the round trip is a test
// (tests/scientific/test_relativistic_sky.cpp).
[[nodiscard]] double colour_index_from_temperature(double temperature);

// Right ascension and declination (degrees, J2000) to a unit vector on the same
// axes as the integration frame.
[[nodiscard]] math::Vec3 equatorial_to_unit_vector(double right_ascension_deg,
                                                   double declination_deg);

}  // namespace sf::render
