#include "core/render/star_catalog.hpp"

#include "core/render/tone_response.hpp"
#include "core/units/conversions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <stdexcept>

#ifndef SPACEFLIGHT_DEFAULT_CATALOG_DIR
#define SPACEFLIGHT_DEFAULT_CATALOG_DIR ""
#endif

namespace sf::render {
namespace {

// BSC5 is fixed-width; these are the byte ranges from the VizieR V/50 ReadMe,
// converted from its 1-based inclusive columns to 0-based offset/length.
//
//    1-  4  HR        76- 77  RAh (J2000)    103-107  Vmag
//    5- 14  Name      78- 79  RAm            110-114  B-V
//                     80- 83  RAs
//                        84   DE sign
//                     85- 86  DEd
//                     87- 88  DEm
//                     89- 90  DEs
constexpr std::size_t kMinimumLineLength = 115;

std::string_view column(std::string_view line, std::size_t first_1based, std::size_t last_1based) {
    const std::size_t offset = first_1based - 1;
    const std::size_t length = last_1based - first_1based + 1;
    if (offset + length > line.size()) {
        return {};
    }
    return line.substr(offset, length);
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(first, last - first + 1)};
}

// Fixed-width fields are blank when absent, which is not the same as zero: a
// blank B-V means "no colour was measured", and a star with no colour has no
// temperature and cannot be Doppler-shifted.  std::optional says that; 0.0 would
// silently paint it 10125 K.
std::optional<double> parse_number(std::string_view field) {
    const std::string text = trim(field);
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const double value = std::stod(text, &consumed);
        if (consumed != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

std::string StarCatalog::default_path() {
    namespace fs = std::filesystem;
    const char* env = std::getenv("SPACEFLIGHT_CATALOG_DIR");
    const fs::path dir = (env != nullptr && *env != '\0')
                             ? fs::path{env}
                             : fs::path{SPACEFLIGHT_DEFAULT_CATALOG_DIR};
    return (dir / "bsc5.dat").string();
}

double temperature_from_colour_index(double colour_index) {
    const double b = 0.92 * colour_index;
    return 4600.0 * (1.0 / (b + 1.70) + 1.0 / (b + 0.62));
}

math::Vec3 equatorial_to_unit_vector(double right_ascension_deg, double declination_deg) {
    const double ra = units::deg_to_rad(right_ascension_deg);
    const double dec = units::deg_to_rad(declination_deg);
    const double cos_dec = std::cos(dec);
    return math::Vec3{cos_dec * std::cos(ra), cos_dec * std::sin(ra), std::sin(dec)};
}

StarCatalog StarCatalog::from_bsc5_text(const std::string& text) {
    StarCatalog catalog{};
    std::istringstream stream{text};
    std::string line;

    while (std::getline(stream, line)) {
        if (line.size() < kMinimumLineLength) {
            continue;
        }
        ++catalog.report_.records_read;

        // The 14 records BSC5 keeps as placeholders for objects that turned out
        // not to exist (novae, historical errors) carry blank coordinates.  They
        // are part of the published file, not a parse failure.
        const auto ra_hours = parse_number(column(line, 76, 77));
        if (!ra_hours.has_value()) {
            ++catalog.report_.without_coordinates;
            continue;
        }

        const auto ra_minutes = parse_number(column(line, 78, 79));
        const auto ra_seconds = parse_number(column(line, 80, 83));
        const auto dec_degrees = parse_number(column(line, 85, 86));
        const auto dec_minutes = parse_number(column(line, 87, 88));
        const auto dec_seconds = parse_number(column(line, 89, 90));
        const auto magnitude = parse_number(column(line, 103, 107));
        if (!ra_minutes || !ra_seconds || !dec_degrees || !dec_minutes || !dec_seconds ||
            !magnitude) {
            ++catalog.report_.without_coordinates;
            continue;
        }

        const auto colour = parse_number(column(line, 110, 114));
        if (!colour.has_value()) {
            ++catalog.report_.without_colour;
            continue;
        }

        // 15 degrees per hour of right ascension, by definition.
        const double ra_deg =
            (*ra_hours + *ra_minutes / 60.0 + *ra_seconds / 3600.0) * 15.0;
        const double sign = column(line, 84, 84) == "-" ? -1.0 : 1.0;
        const double dec_deg =
            sign * (*dec_degrees + *dec_minutes / 60.0 + *dec_seconds / 3600.0);

        CatalogStar star{};
        const auto hr = parse_number(column(line, 1, 4));
        star.hr = hr.has_value() ? static_cast<int>(*hr) : 0;
        star.name = trim(column(line, 5, 14));
        star.direction = equatorial_to_unit_vector(ra_deg, dec_deg);
        star.visual_magnitude = *magnitude;
        star.colour_index = *colour;
        star.temperature = temperature_from_colour_index(*colour);
        star.rest_flux = flux_from_magnitude(*magnitude);

        catalog.stars_.push_back(star);
        ++catalog.report_.accepted;
    }

    return catalog;
}

StarCatalog StarCatalog::from_bsc5_file(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error("StarCatalog: cannot open " + path +
                                 " -- run scripts/fetch_star_catalog.sh");
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return from_bsc5_text(buffer.str());
}

void StarCatalog::keep_brighter_than(double magnitude_limit) {
    stars_.erase(std::remove_if(stars_.begin(), stars_.end(),
                                [magnitude_limit](const CatalogStar& s) {
                                    return s.visual_magnitude > magnitude_limit;
                                }),
                 stars_.end());
}

std::string StarCatalog::describe() const {
    std::ostringstream os;
    os << report_.accepted << " stars from " << report_.records_read << " records ("
       << report_.without_coordinates << " with no coordinates, " << report_.without_colour
       << " with no B-V)";
    if (!stars_.empty()) {
        // minmax on MAGNITUDE, where smaller is brighter: the first iterator is
        // the brightest star and the second the faintest, which is the opposite
        // of what the names would suggest if they said min and max.
        const auto [brightest, faintest] = std::minmax_element(
            stars_.begin(), stars_.end(), [](const CatalogStar& a, const CatalogStar& b) {
                return a.visual_magnitude < b.visual_magnitude;
            });
        os << ", V " << brightest->visual_magnitude << " to " << faintest->visual_magnitude;
    }
    return os.str();
}

}  // namespace sf::render
