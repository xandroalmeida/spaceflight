#pragma once

// The instruments' Canvas, over an ImGui draw list: the application's half of
// app/presentation/canvas.hpp.

#include "app/presentation/canvas.hpp"

#include <imgui.h>

namespace sf::gfx {

// ImGui sizes a font by its ascent-to-descent height; the scene (and Godot before
// it) sized it by the em. DejaVu Sans Mono's hhea box is (1901 + 483) / 2048 em,
// so an "18 px" label is 18 * 1.164 ImGui pixels -- or every caption comes out a
// sixth smaller than the manual's captures.
inline constexpr float FONT_EM = 1.1641F;
[[nodiscard]] inline float font_px(double px) { return static_cast<float>(px) * FONT_EM; }

class ImGuiCanvas final : public app::Canvas {
public:
    // `origin` and `scale` place the canvas's pixels inside the list's space.
    ImGuiCanvas(ImDrawList& list, ImFont* font, app::Vec2 origin = {}, double scale = 1.0);

    void line(app::Vec2 from, app::Vec2 to, app::Colour colour, double width) override;
    void polyline(const std::vector<app::Vec2>& points, app::Colour colour, double width) override;
    void arc(app::Vec2 centre, double radius, double start, double end, int points, app::Colour colour,
             double width) override;
    void circle(app::Vec2 centre, double radius, app::Colour colour) override;
    void rect(const app::Rect2& rect, app::Colour colour, bool filled, double width) override;
    void text(app::Vec2 baseline, std::string_view text, double px, app::Colour colour) override;
    [[nodiscard]] double text_width(std::string_view text, double px) const override;
    [[nodiscard]] double ascent(double px) const override;
    [[nodiscard]] double line_height(double px) const override;

    // Text with a dark outline, the way the scene's labels were drawn.
    void outlined_text(app::Vec2 top_left, std::string_view text, double px, app::Colour colour,
                       app::Colour outline, double outline_px);

private:
    [[nodiscard]] ImVec2 at(app::Vec2 p) const;
    [[nodiscard]] static ImU32 pack(app::Colour colour);

    ImDrawList& list_;
    ImFont* font_;
    app::Vec2 origin_;
    double scale_;
};

}  // namespace sf::gfx
