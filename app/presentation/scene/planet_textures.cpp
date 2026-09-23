#include "app/presentation/scene/planet_textures.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#define STB_PERLIN_IMPLEMENTATION
#include <stb_perlin.h>

namespace sf::app::planet_textures {
namespace {

struct Rgb {
    double r;
    double g;
    double b;
};

Rgb lerp(const Rgb& a, const Rgb& b, double t) {
    return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Rgb darkened(const Rgb& c, double amount) { return Rgb{c.r * (1.0 - amount), c.g * (1.0 - amount), c.b * (1.0 - amount)}; }

// Fractal noise in [-1, 1], seeded, sampled at a direction.
double noise(double x, double y, double z, double frequency, int octaves, int seed) {
    const auto f = static_cast<float>(frequency);
    const float value = stb_perlin_fbm_noise3(static_cast<float>(x) * f + static_cast<float>(seed) * 0.173F,
                                              static_cast<float>(y) * f, static_cast<float>(z) * f, 2.0F, 0.5F,
                                              octaves);
    return std::clamp(static_cast<double>(value), -1.0, 1.0);
}

// Pixel -> unit direction, in the sphere mesh's UV convention: u goes round, v
// runs down from the north pole. A sampling that does not match the mesh puts
// Europe in the Pacific and nothing reports it.
void direction(int x, int y, double& dx, double& dy, double& dz) {
    const double u = (x + 0.5) / WIDTH;
    const double v = (y + 0.5) / HEIGHT;
    const double longitude = u * 2.0 * std::numbers::pi;
    const double latitude = (0.5 - v) * std::numbers::pi;
    const double cos_lat = std::cos(latitude);
    dx = cos_lat * std::cos(longitude);
    dy = std::sin(latitude);
    dz = cos_lat * std::sin(longitude);
}

void put(Image& image, int x, int y, const Rgb& colour) {
    auto* px = &image.rgba[static_cast<std::size_t>((y * WIDTH + x) * 4)];
    px[0] = static_cast<std::uint8_t>(std::clamp(colour.r, 0.0, 1.0) * 255.0 + 0.5);
    px[1] = static_cast<std::uint8_t>(std::clamp(colour.g, 0.0, 1.0) * 255.0 + 0.5);
    px[2] = static_cast<std::uint8_t>(std::clamp(colour.b, 0.0, 1.0) * 255.0 + 0.5);
    px[3] = 255;
}

Image blank() { return Image{WIDTH, HEIGHT, std::vector<std::uint8_t>(static_cast<std::size_t>(WIDTH * HEIGHT * 4))}; }

double latitude_of(int y) { return (0.5 - (y + 0.5) / HEIGHT) * std::numbers::pi; }

// Cellular noise, "second-nearest minus nearest" (FastNoiseLite's
// DISTANCE2_SUB, which the scene's craters used): about -1 inside a cell, rising
// towards 0 on the borders between cells -- dark floors, bright rims.
double cellular(double x, double y, double z, double frequency, int seed) {
    const double px = x * frequency;
    const double py = y * frequency;
    const double pz = z * frequency;
    const auto cx = static_cast<int>(std::floor(px));
    const auto cy = static_cast<int>(std::floor(py));
    const auto cz = static_cast<int>(std::floor(pz));
    const auto hash = [seed](int i, int j, int k, int axis) {
        auto h = (static_cast<std::uint32_t>(i) * 73856093U) ^ (static_cast<std::uint32_t>(j) * 19349663U) ^
                 (static_cast<std::uint32_t>(k) * 83492791U) ^ (static_cast<std::uint32_t>(seed) * 2654435761U) ^
                 (static_cast<std::uint32_t>(axis) * 40503U);
        h ^= h >> 13;
        h *= 0x5bd1e995U;
        h ^= h >> 15;
        return static_cast<double>(h & 0xFFFFFFU) / static_cast<double>(0xFFFFFF);
    };
    double d0 = 1.0e9;
    double d1 = 1.0e9;
    for (int i = cx - 1; i <= cx + 1; ++i) {
        for (int j = cy - 1; j <= cy + 1; ++j) {
            for (int k = cz - 1; k <= cz + 1; ++k) {
                const double fx = i + hash(i, j, k, 0) - px;
                const double fy = j + hash(i, j, k, 1) - py;
                const double fz = k + hash(i, j, k, 2) - pz;
                const double d = std::sqrt(fx * fx + fy * fy + fz * fz);
                if (d < d0) {
                    d1 = d0;
                    d0 = d;
                } else if (d < d1) {
                    d1 = d;
                }
            }
        }
    }
    return std::clamp(d1 - d0 - 1.0, -1.0, 1.0);
}

}  // namespace

Image earth_albedo() {
    Image image = blank();
    const Rgb deep{0.024, 0.055, 0.125};
    const Rgb shallow{0.055, 0.145, 0.255};
    const Rgb beach{0.52, 0.47, 0.33};
    const Rgb grass{0.145, 0.30, 0.145};
    const Rgb forest{0.075, 0.195, 0.105};
    const Rgb desert{0.60, 0.50, 0.31};
    const Rgb ice{0.90, 0.92, 0.95};
    for (int y = 0; y < HEIGHT; ++y) {
        const double abs_lat = std::abs(latitude_of(y)) / (std::numbers::pi * 0.5);
        for (int x = 0; x < WIDTH; ++x) {
            double dx = 0.0;
            double dy = 0.0;
            double dz = 0.0;
            direction(x, y, dx, dy, dz);
            double height = noise(dx * 2.0, dy * 2.0, dz * 2.0, 1.15, 5, 1337);
            height += 0.35 * noise(dx * 2.0, dy * 2.0, dz * 2.0, 3.6, 4, 4242);
            Rgb colour{};
            if (height < 0.0) {
                colour = lerp(deep, shallow, std::clamp((height + 0.35) / 0.35, 0.0, 1.0));
            } else if (height < 0.035) {
                colour = beach;
            } else {
                const double dryness = std::clamp(0.5 + 0.5 * noise(dx * 5.0, dy * 5.0, dz * 5.0, 3.6, 4, 4242), 0.0, 1.0);
                // Deserts at the latitudes of the Hadley cell, ~15 to 35 degrees.
                const double hadley = 1.0 - std::abs(abs_lat - 0.28) * 3.4;
                colour = lerp(forest, grass, dryness);
                colour = lerp(colour, desert, std::clamp(hadley, 0.0, 1.0) * dryness);
            }
            // Polar caps, their edge broken by the same noise so they do not
            // become a rectangular band.
            const double ice_edge = 0.80 + 0.10 * noise(dx * 4.0, dy * 4.0, dz * 4.0, 3.6, 4, 4242);
            if (abs_lat > ice_edge) {
                colour = lerp(colour, ice, std::clamp((abs_lat - ice_edge) / 0.12, 0.0, 1.0));
            }
            put(image, x, y, colour);
        }
    }
    return image;
}

Image earth_clouds() {
    Image image = blank();
    for (int y = 0; y < HEIGHT; ++y) {
        const double abs_lat = std::abs(latitude_of(y)) / (std::numbers::pi * 0.5);
        // Convergence zone at the equator, dry belt in the subtropics, fronts at
        // mid-latitudes: the three bands one sees from orbit.
        double climate = 0.55 * std::exp(-std::pow(abs_lat / 0.12, 2.0));
        climate += 0.50 * std::exp(-std::pow((abs_lat - 0.62) / 0.22, 2.0));
        climate += 0.20;
        climate -= 0.35 * std::exp(-std::pow((abs_lat - 0.30) / 0.13, 2.0));
        for (int x = 0; x < WIDTH; ++x) {
            double dx = 0.0;
            double dy = 0.0;
            double dz = 0.0;
            direction(x, y, dx, dy, dz);
            double value = 0.5 + 0.5 * noise(dx * 2.0, dy * 2.0, dz * 2.0, 1.9, 5, 909);
            value = value * 0.75 + 0.25 * (0.5 + 0.5 * noise(dx * 2.0, dy * 2.0, dz * 2.0, 5.5, 3, 5150));
            const double cover = std::clamp((value + climate - 0.92) * 3.4, 0.0, 1.0);
            put(image, x, y, Rgb{cover, cover, cover});
        }
    }
    return image;
}

Image earth_night() {
    Image image = blank();
    // Sodium amber and not white: it is the colour the Earth has at night, and the
    // difference is half of what makes the image recognisable.
    const Rgb lamp{1.0, 0.72, 0.36};
    for (int y = 0; y < HEIGHT; ++y) {
        const double abs_lat = std::abs(latitude_of(y)) / (std::numbers::pi * 0.5);
        for (int x = 0; x < WIDTH; ++x) {
            double dx = 0.0;
            double dy = 0.0;
            double dz = 0.0;
            direction(x, y, dx, dy, dz);
            const double height = noise(dx * 2.0, dy * 2.0, dz * 2.0, 1.15, 5, 1337) +
                                  0.35 * noise(dx * 2.0, dy * 2.0, dz * 2.0, 3.6, 4, 4242);
            if (height < 0.035 || abs_lat > 0.78) {
                put(image, x, y, Rgb{0.0, 0.0, 0.0});
                continue;
            }
            // Lights where people are: clustered, and stronger near the coast.
            const double density = 0.5 + 0.5 * noise(dx * 2.0, dy * 2.0, dz * 2.0, 14.0, 2, 77);
            const double coastal = std::clamp(1.0 - std::abs(height - 0.10) * 6.0, 0.0, 1.0);
            const double brightness = std::clamp((density - 0.62) * 5.0, 0.0, 1.0) * (0.35 + 0.65 * coastal);
            put(image, x, y, Rgb{lamp.r * brightness, lamp.g * brightness, lamp.b * brightness});
        }
    }
    return image;
}

Image moon_albedo() {
    Image image = blank();
    const Rgb highland{0.62, 0.61, 0.59};
    const Rgb mare{0.27, 0.27, 0.28};
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            double dx = 0.0;
            double dy = 0.0;
            double dz = 0.0;
            direction(x, y, dx, dy, dz);
            // The maria are large and dark and on one side only; low-frequency
            // noise gives exactly that uneven distribution.
            const double basalt =
                std::clamp((noise(dx * 2.0, dy * 2.0, dz * 2.0, 0.9, 3, 31415) - 0.08) * 4.0, 0.0, 1.0);
            Rgb colour = lerp(highland, mare, basalt);
            // Craters: cellular noise gives bright rims and dark floors, which is
            // how a crater reads at this resolution.
            const double rim = cellular(dx * 2.0, dy * 2.0, dz * 2.0, 3.0, 2024);
            colour = lerp(colour, Rgb{0.80, 0.79, 0.77}, std::clamp(rim * 1.4, 0.0, 1.0) * 0.5);
            colour = darkened(colour, std::clamp(-rim * 0.9, 0.0, 1.0) * 0.35);
            colour = lerp(colour, darkened(colour, 0.12), 0.5 + 0.5 * noise(dx * 2.0, dy * 2.0, dz * 2.0, 9.0, 3, 2718));
            put(image, x, y, colour);
        }
    }
    return image;
}

Image mars_albedo() {
    Image image{WIDTH, HEIGHT, std::vector<std::uint8_t>(static_cast<std::size_t>(WIDTH * HEIGHT * 4))};

    // Three tones, judged by eye against Viking/MOC images, not chosen by taste:
    // the bright dust, the dark basaltic rock of the albedo regions, and the ice
    // of the caps, which is CO2 and not pure white.
    const Rgb bright_dust{0.72, 0.46, 0.29};
    const Rgb ochre{0.58, 0.34, 0.20};
    const Rgb dark_terrain{0.33, 0.21, 0.145};
    const Rgb polar_ice{0.92, 0.90, 0.88};

    for (int y = 0; y < HEIGHT; ++y) {
        const double latitude = (0.5 - (y + 0.5) / HEIGHT) * std::numbers::pi;
        const double abs_lat = std::abs(latitude) / (std::numbers::pi * 0.5);
        for (int x = 0; x < WIDTH; ++x) {
            double dx = 0.0;
            double dy = 0.0;
            double dz = 0.0;
            direction(x, y, dx, dy, dz);
            const double region = noise(dx * 2.0, dy * 2.0, dz * 2.0, 1.05, 4, 4995);
            // The dark regions of Mars cover about a quarter of the disc and
            // gather in the southern hemisphere. The step and the latitude bias
            // produce that fraction and that asymmetry.
            const double darkness =
                std::clamp((region + 0.10 - 0.22 * latitude / (std::numbers::pi * 0.5)) * 2.2, 0.0, 1.0);
            Rgb colour = lerp(bright_dust, ochre, std::clamp(0.5 + 0.5 * region, 0.0, 1.0));
            colour = lerp(colour, dark_terrain, darkness);
            // Dust: storms leave the planet mottled on scales of hundreds of
            // kilometres, and that is what breaks the "painted sphere" reading.
            const double dust = noise(dx * 2.0, dy * 2.0, dz * 2.0, 3.2, 4, 1877);
            colour = lerp(colour, bright_dust, std::clamp(dust * 0.55, 0.0, 1.0));
            const double grit = noise(dx * 2.0, dy * 2.0, dz * 2.0, 11.0, 2, 6060);
            colour = lerp(colour, darkened(colour, 0.10), 0.5 + 0.5 * grit);

            // The caps. The southern one is smaller and the edge is ragged: a
            // rectangular band reads as a texture fault.
            const double edge = (latitude > 0.0 ? 0.86 : 0.90) + 0.05 * noise(dx * 4.0, dy * 4.0, dz * 4.0, 3.2, 4, 1877);
            if (abs_lat > edge) {
                colour = lerp(colour, polar_ice, std::clamp((abs_lat - edge) / 0.09, 0.0, 1.0));
            }
            auto* px = &image.rgba[static_cast<std::size_t>((y * WIDTH + x) * 4)];
            px[0] = static_cast<std::uint8_t>(std::clamp(colour.r, 0.0, 1.0) * 255.0 + 0.5);
            px[1] = static_cast<std::uint8_t>(std::clamp(colour.g, 0.0, 1.0) * 255.0 + 0.5);
            px[2] = static_cast<std::uint8_t>(std::clamp(colour.b, 0.0, 1.0) * 255.0 + 0.5);
            px[3] = 255;
        }
    }
    return image;
}

Image generate(const std::string& name) {
    if (name == "earth_albedo") {
        return earth_albedo();
    }
    if (name == "earth_clouds") {
        return earth_clouds();
    }
    if (name == "earth_night") {
        return earth_night();
    }
    if (name == "moon_albedo") {
        return moon_albedo();
    }
    if (name == "mars_albedo") {
        return mars_albedo();
    }
    return {};
}

}  // namespace sf::app::planet_textures
