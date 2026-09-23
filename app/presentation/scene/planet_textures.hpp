#pragma once

// PROCEDURAL planet textures, as placeholders (rules 40-A, 46, 81).
//
// They are not the planets. They are 3-D noise sampled in the direction of the
// sphere -- seamless, with no pinched poles, which is why it is 3-D and not a
// noisy equirectangular image -- sliced into terrain by latitude and height. The
// result reads as a planet from a distance and does not survive a zoom.
//
// The real ones come from outside: the prompts are in docs/assets/planets/ and
// the manifest says where each file has to land. When the file exists it is
// loaded instead and this generator is no longer called -- without any other
// line changing, which is the point of rule 81.

#include <cstdint>
#include <string>
#include <vector>

namespace sf::app {

struct Image {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;   // 8 bits per channel, sRGB-encoded colour
};

namespace planet_textures {

inline constexpr int WIDTH = 512;
inline constexpr int HEIGHT = 256;

// The Earth and the Moon, as placeholders for when the files in
// assets/textures/ are missing: ocean, land, desert and ice sliced by height and
// latitude; three cloud bands; amber lights on the coasts; maria and craters.
[[nodiscard]] Image earth_albedo();
[[nodiscard]] Image earth_clouds();
[[nodiscard]] Image earth_night();
[[nodiscard]] Image moon_albedo();

// Mars, as a placeholder (rules 22, 68, 90).
//
// What this is NOT: a map of Mars. There is no Valles Marineris, no Olympus Mons,
// and the dark patches are not Syrtis Major -- they are noise. What it gets
// right are the three things by which a planet is recognised at a distance: the
// colour (iron oxide, orange-brown, not cartoon red), the CONTRAST between the
// bright plains and the dark albedo regions, and the polar caps, which are Mars's
// signature on a small disc.
[[nodiscard]] Image mars_albedo();

// By the name a BodySurface uses after "procedural:"; empty if unknown.
[[nodiscard]] Image generate(const std::string& name);

}  // namespace planet_textures
}  // namespace sf::app
