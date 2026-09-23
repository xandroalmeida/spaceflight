#pragma once

// The 2-D interface, over Dear ImGui: the HUD, the messages, the orbital map,
// the mission computer, the pause menu, the help panel and the technical
// read-out.
//
// Everything here DRAWS the presentation's state and forwards clicks to the
// same FlightApp calls the keys make. No number is computed here: the panels
// print what app/presentation built.
//
// The interface lives in a virtual 1440 x 900 space, stretched to the window
// with its aspect kept -- the scene's "canvas_items" stretch -- so that a HUD
// laid out in fractions of that space reads the same on a laptop and on a
// 4K screen.

#include "app/presentation/flight_app.hpp"

#include <imgui.h>

#include <string>

namespace sf::ui {

class Interface {
public:
    static constexpr float BASE_WIDTH = 1440.0F;
    static constexpr float BASE_HEIGHT = 900.0F;

    // Loads the font and the style. False when the font file is missing.
    bool initialise(const std::string& asset_directory);

    // The scale from the virtual space to the window's points.
    [[nodiscard]] float scale(float window_width, float window_height, double ui_scale) const;

    // Between ImGui::NewFrame() and ImGui::Render().
    void build(app::FlightApp& app);

    [[nodiscard]] ImFont* font() const { return font_; }

private:
    void draw_hud(app::FlightApp& app);
    void draw_messages(app::FlightApp& app);
    void draw_map(app::FlightApp& app);
    void draw_debug(app::FlightApp& app);
    void mission_panel(app::FlightApp& app);
    void pause_menu(app::FlightApp& app);
    void help_panel(app::FlightApp& app);
    bool begin_panel(const char* id, float width, bool* open);
    void end_panel();
    void rule();
    void label(const std::string& text, float size, app::Colour colour);

    ImFont* font_{nullptr};
};

}  // namespace sf::ui
