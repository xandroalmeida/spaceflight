#pragma once

// Base of the cockpit instruments (rule 17).
//
// An instrument receives one record per frame and draws. It asks the
// simulation nothing and keeps no state between frames beyond the last record
// -- which means the same instrument serves the 3-D panel inside the cockpit
// and the 2-D HUD without knowing which of the two it is on.
//
// The shared primitives -- frame, label, value, tick, marker -- are here so that
// the four displays do not diverge in line weight and text height, which is how
// an interface stops looking like a system.

#include "app/presentation/canvas.hpp"
#include "app/presentation/instruments/instrument_data.hpp"

#include <string>

namespace sf::app {

class Instrument {
public:
    virtual ~Instrument() = default;

    // Draws the whole instrument on `canvas`, which is `size` pixels.
    void draw(Canvas& canvas, Vec2 size, const InstrumentData& data);

protected:
    virtual void paint() = 0;

    // Font size proportional to the instrument's height, so that the same drawing
    // works on a 380 px panel and a 1000 px HUD (rule 59).
    [[nodiscard]] virtual double unit() const;
    [[nodiscard]] int font_size(double units) const;

    void draw_frame(Colour colour = palette::PANEL_EDGE);
    void draw_text_at(Vec2 at, const std::string& text, double units, Colour colour,
                      Align align = Align::Left);
    // A label/value pair, the unit of reading the cockpit is made of. The label
    // is small and desaturated, the value large and bright: the hierarchy of
    // rule 38 put where it is applied, not in a style sheet somewhere else.
    void draw_field(Vec2 at, const std::string& label, const std::string& value, double value_units = 8.0,
                    Colour colour = palette::PRIMARY);
    void draw_bar(const Rect2& rect, double fraction, Colour colour, Colour background = palette::DIM);
    void draw_tick(Vec2 from, Vec2 to, Colour colour, double width_units = 0.3);
    // The reticle of a vector marker, drawn rather than textured: a circle with
    // four ticks is better drawn than in any PNG, and it scales by itself.
    void draw_marker(Vec2 at, double radius, Colour colour, const std::string& kind);

    Canvas* canvas_{nullptr};
    Vec2 size_{};
    const InstrumentData* data_{nullptr};
    std::string title_;
};

}  // namespace sf::app
