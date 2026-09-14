#include "core/celestial/body_id.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <utility>

namespace sf::celestial {
namespace {

struct Entry {
    int id;
    std::string_view name;
};

// Bodies this project names directly.  Not a replacement for SPICE's catalogue:
// it exists so that BodyId::name() and the CLI work without kernels loaded.
constexpr std::array<Entry, 32> kTable{{
    {0, "Solar System Barycenter"},
    {1, "Mercury Barycenter"},
    {2, "Venus Barycenter"},
    {3, "Earth-Moon Barycenter"},
    {4, "Mars Barycenter"},
    {5, "Jupiter Barycenter"},
    {6, "Saturn Barycenter"},
    {7, "Uranus Barycenter"},
    {8, "Neptune Barycenter"},
    {9, "Pluto Barycenter"},
    {10, "Sun"},
    {199, "Mercury"},
    {299, "Venus"},
    {301, "Moon"},
    {399, "Earth"},
    {499, "Mars"},
    {599, "Jupiter"},
    {699, "Saturn"},
    {799, "Uranus"},
    {899, "Neptune"},
    {999, "Pluto"},
    {401, "Phobos"},
    {402, "Deimos"},
    {501, "Io"},
    {502, "Europa"},
    {503, "Ganymede"},
    {504, "Callisto"},
    {602, "Enceladus"},
    {606, "Titan"},
    {801, "Triton"},
    {901, "Charon"},
    {-1, ""},
}};

std::string lowered(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

}  // namespace

std::string BodyId::name() const {
    for (const auto& e : kTable) {
        if (e.id == id_) {
            return std::string{e.name};
        }
    }
    return "NAIF " + std::to_string(id_);
}

BodyLookup body_from_name(std::string_view name) {
    if (name.empty()) {
        return {};
    }

    // Numeric form: "399", "-1"
    {
        int value = 0;
        const auto* begin = name.data();
        const auto* end = name.data() + name.size();
        const auto res = std::from_chars(begin, end, value);
        if (res.ec == std::errc{} && res.ptr == end) {
            return {BodyId{value}, true};
        }
    }

    const std::string needle = lowered(name);
    for (const auto& e : kTable) {
        if (e.name.empty()) {
            continue;
        }
        if (lowered(e.name) == needle) {
            return {BodyId{e.id}, true};
        }
    }

    // Accept "earth barycenter" style aliases.
    if (needle == "ssb" || needle == "barycenter") {
        return {bodies::solar_system_barycenter, true};
    }
    if (needle == "emb") {
        return {bodies::earth_moon_barycenter, true};
    }
    return {};
}

}  // namespace sf::celestial
