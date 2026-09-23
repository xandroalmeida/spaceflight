#include "app/presentation/instruments/instrument.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sf::app {

void Instrument::draw(Canvas& canvas, Vec2 size, const InstrumentData& data) {
    canvas_ = &canvas;
    size_ = size;
    data_ = &data;
    paint();
    canvas_ = nullptr;
    data_ = nullptr;
}

double Instrument::unit() const { return std::max(size_.y / 100.0, 0.5); }

int Instrument::font_size(double units) const { return std::max(static_cast<int>(unit() * units), 8); }

void Instrument::draw_frame(Colour colour) {
    canvas_->rect(Rect2{{0.0, 0.0}, size_}, palette::BACKGROUND, true, 1.0);
    canvas_->rect(Rect2{{1.0, 1.0}, size_ - Vec2{2.0, 2.0}}, colour, false, std::max(unit() * 0.4, 1.0));
    if (!title_.empty()) {
        draw_text_at(Vec2{unit() * 2.5, unit() * 6.0}, title_, 5.0, palette::SECONDARY);
    }
}

void Instrument::draw_text_at(Vec2 at, const std::string& text, double units, Colour colour, Align align) {
    const double px = font_size(units);
    const double width = canvas_->text_width(text, px);
    double x = at.x;
    if (align == Align::Right) {
        x -= width;
    } else if (align == Align::Centre) {
        x -= width * 0.5;
    }
    canvas_->text(Vec2{x, at.y}, text, px, colour);
}

void Instrument::draw_field(Vec2 at, const std::string& label, const std::string& value, double value_units,
                            Colour colour) {
    draw_text_at(at, label, 4.2, palette::SECONDARY);
    draw_text_at(at + Vec2{0.0, unit() * (value_units + 1.0)}, value, value_units, colour);
}

void Instrument::draw_bar(const Rect2& rect, double fraction, Colour colour, Colour background) {
    canvas_->rect(rect, background.darkened(0.6F), true, 1.0);
    const Rect2 filled{rect.position, Vec2{rect.size.x * std::clamp(fraction, 0.0, 1.0), rect.size.y}};
    canvas_->rect(filled, colour, true, 1.0);
    canvas_->rect(rect, background, false, std::max(unit() * 0.25, 1.0));
}

void Instrument::draw_tick(Vec2 from, Vec2 to, Colour colour, double width_units) {
    canvas_->line(from, to, colour, std::max(unit() * width_units, 1.0));
}

void Instrument::draw_marker(Vec2 at, double radius, Colour colour, const std::string& kind) {
    constexpr double tau = 2.0 * std::numbers::pi;
    const double w = std::max(unit() * 0.35, 1.0);
    auto& c = *canvas_;
    if (kind == "prograde") {
        c.arc(at, radius, 0.0, tau, 24, colour, w);
        c.circle(at, radius * 0.22, colour);
        c.line(at - Vec2{radius * 1.8, 0.0}, at - Vec2{radius, 0.0}, colour, w);
        c.line(at + Vec2{radius, 0.0}, at + Vec2{radius * 1.8, 0.0}, colour, w);
        c.line(at - Vec2{0.0, radius * 1.8}, at - Vec2{0.0, radius}, colour, w);
    } else if (kind == "retrograde") {
        c.arc(at, radius, 0.0, tau, 24, colour, w);
        const double d = radius * 0.62;
        c.line(at - Vec2{d, d}, at + Vec2{d, d}, colour, w);
        c.line(at - Vec2{d, -d}, at + Vec2{d, -d}, colour, w);
        c.line(at - Vec2{radius * 1.8, 0.0}, at - Vec2{radius, 0.0}, colour, w);
        c.line(at + Vec2{radius, 0.0}, at + Vec2{radius * 1.8, 0.0}, colour, w);
    } else if (kind == "normal") {
        c.polyline({at + Vec2{-radius, radius * 0.7}, at + Vec2{0.0, -radius}, at + Vec2{radius, radius * 0.7}},
                   colour, w);
        c.line(at + Vec2{-radius, radius * 0.7}, at + Vec2{radius, radius * 0.7}, colour, w);
    } else if (kind == "anti_normal") {
        c.polyline({at + Vec2{-radius, -radius * 0.7}, at + Vec2{0.0, radius}, at + Vec2{radius, -radius * 0.7}},
                   colour, w);
        c.line(at + Vec2{-radius, -radius * 0.7}, at + Vec2{radius, -radius * 0.7}, colour, w);
    } else if (kind == "radial_out") {
        c.arc(at, radius * 0.75, 0.0, tau, 20, colour, w);
        c.line(at, at + Vec2{0.0, -radius * 1.7}, colour, w);
    } else if (kind == "radial_in") {
        c.arc(at, radius * 0.75, 0.0, tau, 20, colour, w);
        c.line(at, at + Vec2{0.0, radius * 1.7}, colour, w);
    } else if (kind == "target") {
        c.arc(at, radius, 0.0, tau, 24, colour, w);
        for (const double angle : {0.0, std::numbers::pi * 0.5, std::numbers::pi, std::numbers::pi * 1.5}) {
            const Vec2 dir{std::cos(angle), std::sin(angle)};
            c.line(at + dir * radius, at + dir * (radius * 1.7), colour, w);
        }
    } else if (kind == "anti_target") {
        for (const double angle : {std::numbers::pi * 0.25, std::numbers::pi * 0.75, std::numbers::pi * 1.25,
                                   std::numbers::pi * 1.75}) {
            const Vec2 dir{std::cos(angle), std::sin(angle)};
            c.line(at + dir * (radius * 0.35), at + dir * (radius * 1.5), colour, w);
        }
    } else if (kind == "nose") {
        c.line(at - Vec2{radius * 2.2, 0.0}, at - Vec2{radius * 0.5, 0.0}, colour, w);
        c.line(at + Vec2{radius * 0.5, 0.0}, at + Vec2{radius * 2.2, 0.0}, colour, w);
        c.line(at, at + Vec2{0.0, radius * 1.2}, colour, w);
    } else {
        c.arc(at, radius, 0.0, tau, 16, colour, w);
    }
}

}  // namespace sf::app
