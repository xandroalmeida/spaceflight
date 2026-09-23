#pragma once

// The whole cockpit palette, in one place (rule 72).
//
// Restricted on purpose: a dark neutral background, near-white primary data,
// desaturated cyan for navigation, amber for attention, red for critical. Five
// colours with a meaning and nothing decorative -- an instrument whose colour
// means nothing spends the one channel a pilot reads without reading.
//
// Colours are sRGB-ENCODED values, the way a designer writes them. The 2-D
// instruments draw them as they are; the 3-D materials convert them to linear
// light before lighting them (Colour::to_linear), exactly once.

#include <algorithm>
#include <cmath>

namespace sf::app {

struct Colour {
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float a{1.0F};

    constexpr Colour operator*(float s) const { return {r * s, g * s, b * s, a * s}; }
    constexpr Colour operator*(const Colour& o) const { return {r * o.r, g * o.g, b * o.b, a * o.a}; }
    constexpr bool operator==(const Colour&) const = default;

    // Towards black by `amount`, alpha untouched.
    [[nodiscard]] constexpr Colour darkened(float amount) const {
        return {r * (1.0F - amount), g * (1.0F - amount), b * (1.0F - amount), a};
    }
    // Towards white by `amount`, alpha untouched.
    [[nodiscard]] constexpr Colour lightened(float amount) const {
        return {r + (1.0F - r) * amount, g + (1.0F - g) * amount, b + (1.0F - b) * amount, a};
    }
    [[nodiscard]] constexpr Colour lerp(const Colour& to, float t) const {
        return {r + (to.r - r) * t, g + (to.g - g) * t, b + (to.b - b) * t, a + (to.a - a) * t};
    }
    [[nodiscard]] constexpr Colour with_alpha(float alpha) const { return {r, g, b, alpha}; }

    // IEC 61966-2-1, per channel; alpha is linear already.
    [[nodiscard]] Colour to_linear() const {
        auto decode = [](float c) {
            return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
        };
        return {decode(r), decode(g), decode(b), a};
    }
};

namespace palette {

inline constexpr Colour BACKGROUND{0.043F, 0.051F, 0.062F};   // graphite, the display ground
inline constexpr Colour PANEL{0.098F, 0.106F, 0.118F};        // the panel surface
inline constexpr Colour PANEL_EDGE{0.16F, 0.17F, 0.19F};

inline constexpr Colour PRIMARY{0.898F, 0.925F, 0.949F};      // primary data
inline constexpr Colour SECONDARY{0.541F, 0.580F, 0.620F};    // label, unit, axis
inline constexpr Colour DIM{0.33F, 0.36F, 0.40F};             // grid, minor tick

inline constexpr Colour NAV{0.361F, 0.788F, 0.769F};          // desaturated cyan: navigation
inline constexpr Colour NAV_DIM{0.20F, 0.44F, 0.44F};
inline constexpr Colour PROGRADE{0.639F, 0.831F, 0.475F};     // green: velocity vector
inline constexpr Colour TARGET{0.792F, 0.616F, 0.902F};       // violet: target
inline constexpr Colour PLAN{0.949F, 0.749F, 0.404F};         // the planned trajectory

inline constexpr Colour WARNING{0.949F, 0.678F, 0.267F};      // amber
inline constexpr Colour CRITICAL{0.902F, 0.361F, 0.333F};     // red
inline constexpr Colour OK{0.451F, 0.784F, 0.541F};           // state green

inline constexpr Colour ENGINE{0.984F, 0.831F, 0.596F};       // main engine plume
inline constexpr Colour RCS{0.761F, 0.878F, 0.976F};          // RCS jet

// The glass of a lit display pushes a little light into the cockpit (rule 51).
inline constexpr Colour DISPLAY_GLOW{0.145F, 0.267F, 0.290F};

// A colour that fades towards the grid as confidence drops. Used where a value
// exists but is stale or out of range -- instead of hiding it, which would make
// the instrument lie by omission.
[[nodiscard]] constexpr Colour faded(const Colour& colour, float amount) {
    return colour.lerp(DIM, std::clamp(amount, 0.0F, 1.0F));
}

}  // namespace palette
}  // namespace sf::app
