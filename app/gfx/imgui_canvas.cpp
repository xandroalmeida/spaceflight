#include "app/gfx/imgui_canvas.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace sf::gfx {

ImGuiCanvas::ImGuiCanvas(ImDrawList& list, ImFont* font, app::Vec2 origin, double scale)
    : list_(list), font_(font), origin_(origin), scale_(scale) {}

ImVec2 ImGuiCanvas::at(app::Vec2 p) const {
    return ImVec2{static_cast<float>(origin_.x + p.x * scale_), static_cast<float>(origin_.y + p.y * scale_)};
}

ImU32 ImGuiCanvas::pack(app::Colour colour) {
    const auto channel = [](float c) { return static_cast<int>(std::lround(std::clamp(c, 0.0F, 1.0F) * 255.0F)); };
    return IM_COL32(channel(colour.r), channel(colour.g), channel(colour.b), channel(colour.a));
}

void ImGuiCanvas::line(app::Vec2 from, app::Vec2 to, app::Colour colour, double width) {
    list_.AddLine(at(from), at(to), pack(colour), static_cast<float>(width * scale_));
}

void ImGuiCanvas::polyline(const std::vector<app::Vec2>& points, app::Colour colour, double width) {
    std::vector<ImVec2> converted;
    converted.reserve(points.size());
    for (const auto& p : points) {
        converted.push_back(at(p));
    }
    list_.AddPolyline(converted.data(), static_cast<int>(converted.size()), pack(colour),
                      static_cast<float>(width * scale_));
}

void ImGuiCanvas::arc(app::Vec2 centre, double radius, double start, double end, int points, app::Colour colour,
                      double width) {
    list_.PathClear();
    list_.PathArcTo(at(centre), static_cast<float>(radius * scale_), static_cast<float>(start),
                    static_cast<float>(end), std::max(points - 1, 3));
    list_.PathStroke(pack(colour), static_cast<float>(width * scale_));
}

void ImGuiCanvas::circle(app::Vec2 centre, double radius, app::Colour colour) {
    list_.AddCircleFilled(at(centre), static_cast<float>(radius * scale_), pack(colour));
}

void ImGuiCanvas::rect(const app::Rect2& r, app::Colour colour, bool filled, double width) {
    const ImVec2 a = at(r.position);
    const ImVec2 b = at(r.position + r.size);
    if (filled) {
        list_.AddRectFilled(a, b, pack(colour));
    } else {
        list_.AddRect(a, b, pack(colour), 0.0F, static_cast<float>(width * scale_));
    }
}

double ImGuiCanvas::ascent(double px) const {
    return static_cast<double>(font_->GetFontBaked(font_px(px * scale_))->Ascent) / scale_;
}

double ImGuiCanvas::line_height(double px) const {
    const ImFontBaked* baked = font_->GetFontBaked(font_px(px * scale_));
    return static_cast<double>(baked->Ascent - baked->Descent) / scale_;
}

void ImGuiCanvas::text(app::Vec2 baseline, std::string_view text, double px, app::Colour colour) {
    const auto size = font_px(px * scale_);
    const ImVec2 origin = at(baseline);
    const float top = origin.y - font_->GetFontBaked(size)->Ascent;
    list_.AddText(font_, size, ImVec2{origin.x, top}, pack(colour), text.data(), text.data() + text.size());
}

double ImGuiCanvas::text_width(std::string_view text, double px) const {
    const ImVec2 extent = font_->CalcTextSizeA(font_px(px * scale_), FLT_MAX, 0.0F, text.data(),
                                               text.data() + text.size());
    return static_cast<double>(extent.x) / scale_;
}

void ImGuiCanvas::outlined_text(app::Vec2 top_left, std::string_view text, double px, app::Colour colour,
                                app::Colour outline, double outline_px) {
    const auto size = font_px(px * scale_);
    const ImVec2 origin = at(top_left);
    const float o = static_cast<float>(outline_px * scale_);
    const ImU32 dark = pack(outline);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    // Eight offset copies make an outline; the text sits on top.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dy != 0) {
                list_.AddText(font_, size, ImVec2{origin.x + static_cast<float>(dx) * o, origin.y + static_cast<float>(dy) * o},
                              dark, begin, end);
            }
        }
    }
    list_.AddText(font_, size, origin, pack(colour), begin, end);
}

}  // namespace sf::gfx
