#pragma once

// Where an instrument draws.
//
// The instruments were written against Godot's CanvasItem drawing calls and
// they keep their shape: a line, an arc, a filled circle, a rectangle, a string
// placed by its BASELINE. The application implements this over an ImGui draw
// list (app/gfx/imgui_canvas.cpp), which the GPU then paints into a cockpit
// display, the HUD or the map; the tests implement it with a recorder that
// needs no window.
//
// Coordinates are pixels of the surface being drawn, origin top-left, y down.
// Colours are sRGB-encoded, the way the palette writes them.

#include "app/presentation/geometry.hpp"
#include "app/presentation/palette.hpp"

#include <string_view>
#include <vector>

namespace sf::app {

enum class Align { Left, Centre, Right };

class Canvas {
public:
    virtual ~Canvas() = default;

    virtual void line(Vec2 from, Vec2 to, Colour colour, double width) = 0;
    virtual void polyline(const std::vector<Vec2>& points, Colour colour, double width) = 0;
    // `points` segments from `start` to `end` radians, 0 along +x, increasing
    // towards +y (clockwise on screen).
    virtual void arc(Vec2 centre, double radius, double start, double end, int points, Colour colour,
                     double width) = 0;
    virtual void circle(Vec2 centre, double radius, Colour colour) = 0;   // filled
    virtual void rect(const Rect2& rect, Colour colour, bool filled, double width) = 0;
    // `baseline` is the left end of the text's baseline.
    virtual void text(Vec2 baseline, std::string_view text, double px, Colour colour) = 0;
    [[nodiscard]] virtual double text_width(std::string_view text, double px) const = 0;
    // The distance from the top of the line to the baseline, and the whole
    // line's height, at `px`.
    [[nodiscard]] virtual double ascent(double px) const = 0;
    [[nodiscard]] virtual double line_height(double px) const = 0;
};

// A canvas that remembers what was drawn and draws nothing. The tests read it;
// the font metrics are those of a monospace face at 0.6 em per character, which
// is what DejaVu Sans Mono measures.
class RecordingCanvas final : public Canvas {
public:
    struct Text {
        Vec2 baseline;
        std::string text;
        double px;
        Colour colour;
    };
    struct Stroke {
        Vec2 from;
        Vec2 to;
        Colour colour;
    };

    void line(Vec2 from, Vec2 to, Colour colour, double) override { strokes.push_back({from, to, colour}); }
    void polyline(const std::vector<Vec2>& points, Colour colour, double) override {
        for (std::size_t i = 1; i < points.size(); ++i) {
            strokes.push_back({points[i - 1], points[i], colour});
        }
    }
    void arc(Vec2 centre, double radius, double, double, int, Colour colour, double) override {
        arcs.push_back({centre, Vec2{radius, 0.0}, colour});
    }
    void circle(Vec2 centre, double radius, Colour colour) override {
        circles.push_back({centre, Vec2{radius, 0.0}, colour});
    }
    void rect(const Rect2& r, Colour colour, bool, double) override {
        rects.push_back({r.position, r.position + r.size, colour});
    }
    void text(Vec2 baseline, std::string_view t, double px, Colour colour) override {
        texts.push_back({baseline, std::string{t}, px, colour});
    }
    [[nodiscard]] double text_width(std::string_view t, double px) const override {
        std::size_t characters = 0;
        for (const char c : t) {
            if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
                ++characters;
            }
        }
        return 0.6 * px * static_cast<double>(characters);
    }
    [[nodiscard]] double ascent(double px) const override { return 0.76 * px; }
    [[nodiscard]] double line_height(double px) const override { return 1.17 * px; }

    [[nodiscard]] bool contains_text(std::string_view needle) const {
        for (const auto& t : texts) {
            if (t.text.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    std::vector<Text> texts;
    std::vector<Stroke> strokes;
    std::vector<Stroke> arcs;      // centre, (radius, 0)
    std::vector<Stroke> circles;   // centre, (radius, 0)
    std::vector<Stroke> rects;     // top-left, bottom-right
};

}  // namespace sf::app
