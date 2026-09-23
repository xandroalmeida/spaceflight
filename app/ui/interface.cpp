#include "app/ui/interface.hpp"

#include "app/gfx/imgui_canvas.hpp"
#include "app/presentation/format.hpp"
#include "app/presentation/input_actions.hpp"
#include "app/presentation/ui/debug_hud.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace sf::ui {
namespace {

using app::Colour;
namespace palette = app::palette;

ImVec4 im(Colour c) { return ImVec4{c.r, c.g, c.b, c.a}; }

ImU32 packed(Colour c) { return ImGui::ColorConvertFloat4ToU32(im(c)); }

}  // namespace

bool Interface::initialise(const std::string& asset_directory) {
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.BackendRendererName = "spaceflight_sdl_gpu";
    io.BackendPlatformName = "spaceflight_sdl3";

    // Monospace, because the read-out is a table of columns and because 0/O and
    // 1/I have to be told apart at a glance (rule 73). DejaVu Sans Mono carries
    // the Greek, the arrows and the triangles the panels use.
    const std::string path = asset_directory + "/fonts/DejaVuSansMono.ttf";
    if (!std::filesystem::exists(path)) {
        return false;
    }
    ImFontConfig config{};
    config.OversampleH = 2;
    config.OversampleV = 2;
    font_ = io.Fonts->AddFontFromFileTTF(path.c_str(), gfx::font_px(14.0), &config);
    if (font_ == nullptr) {
        return false;
    }

    // The palette of the scene's panels (rule 72): a graphite ground, a thin
    // edge, near-white text, cyan for what answers.
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0F;
    style.WindowBorderSize = 1.0F;
    style.WindowPadding = ImVec2{18.0F, 18.0F};
    style.FrameRounding = 3.0F;
    style.FramePadding = ImVec2{10.0F, 6.0F};
    style.ItemSpacing = ImVec2{10.0F, 10.0F};
    style.GrabRounding = 3.0F;
    style.ScrollbarSize = 12.0F;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4{0.043F, 0.051F, 0.062F, 0.96F};
    colors[ImGuiCol_ChildBg] = ImVec4{0.0F, 0.0F, 0.0F, 0.0F};
    colors[ImGuiCol_Border] = im(palette::PANEL_EDGE);
    colors[ImGuiCol_Text] = im(palette::PRIMARY);
    colors[ImGuiCol_TextDisabled] = im(palette::DIM);
    colors[ImGuiCol_Button] = im(palette::PANEL_EDGE);
    colors[ImGuiCol_ButtonHovered] = im(palette::NAV_DIM);
    colors[ImGuiCol_ButtonActive] = im(palette::NAV);
    colors[ImGuiCol_FrameBg] = ImVec4{0.08F, 0.09F, 0.10F, 1.0F};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.12F, 0.13F, 0.15F, 1.0F};
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.14F, 0.15F, 0.17F, 1.0F};
    colors[ImGuiCol_SliderGrab] = im(palette::NAV_DIM);
    colors[ImGuiCol_SliderGrabActive] = im(palette::NAV);
    colors[ImGuiCol_Separator] = im(palette::PANEL_EDGE);
    colors[ImGuiCol_ScrollbarBg] = ImVec4{0.0F, 0.0F, 0.0F, 0.0F};
    colors[ImGuiCol_ScrollbarGrab] = im(palette::PANEL_EDGE);
    return true;
}

float Interface::scale(float window_width, float window_height, double ui_scale) const {
    return std::min(window_width / BASE_WIDTH, window_height / BASE_HEIGHT) * static_cast<float>(ui_scale);
}

void Interface::build(app::FlightApp& app) {
    draw_hud(app);
    draw_messages(app);
    draw_map(app);
    if (app.panel_visible(app::FlightApp::Panel::Mission)) {
        mission_panel(app);
    }
    if (app.panel_visible(app::FlightApp::Panel::Pause)) {
        pause_menu(app);
    }
    if (app.panel_visible(app::FlightApp::Panel::Help)) {
        help_panel(app);
    }
    if (app.debug_visible()) {
        draw_debug(app);
    }
}

void Interface::draw_hud(app::FlightApp& app) {
    if (!app.hud_visible()) {
        return;
    }
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    gfx::ImGuiCanvas canvas(*ImGui::GetBackgroundDrawList(), font_);
    app.draw_hud(canvas, app::Vec2{static_cast<double>(size.x), static_cast<double>(size.y)});
}

void Interface::draw_messages(app::FlightApp& app) {
    // Bottom-left, above the HUD's strip. In the middle of the screen they sit
    // over exactly the part of the view the ship tends to cross.
    auto& log = app.messages();
    if (!log.shown() || log.visible().empty()) {
        return;
    }
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const double px = std::clamp(std::floor(static_cast<double>(size.y) / 52.0), 12.0, 24.0);
    gfx::ImGuiCanvas canvas(*ImGui::GetBackgroundDrawList(), font_);
    const double line = canvas.line_height(px) + 2.0;
    double y = static_cast<double>(size.y) - 300.0 - line * static_cast<double>(log.visible().size());
    for (const auto& message : log.visible()) {
        Colour colour = app::MessageLog::colour(message.level);
        colour.a *= message.opacity;
        // A dark outline: the messages appear over space and over the Earth, and
        // without it half of them are illegible half the time.
        canvas.outlined_text(app::Vec2{28.0, y}, message.text, px, colour, Colour{0.0F, 0.0F, 0.0F, 0.85F * message.opacity},
                             1.5);
        y += line;
    }
}

void Interface::draw_map(app::FlightApp& app) {
    if (!app.panel_visible(app::FlightApp::Panel::Map)) {
        return;
    }
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    gfx::ImGuiCanvas canvas(*ImGui::GetBackgroundDrawList(), font_);
    app.draw_map(canvas, app::Vec2{static_cast<double>(size.x), static_cast<double>(size.y)});
}

void Interface::draw_debug(app::FlightApp& app) {
    // The whole read-out, in two columns, the font sized by the CONTENT: the
    // largest size at which every line fits the window.
    const auto lines = app::debug_hud::two_columns(app::debug_hud::hud_lines(app, false));
    if (lines.empty()) {
        return;
    }
    const ImVec2 view = ImGui::GetIO().DisplaySize;
    const float gap = std::max(std::round(view.y * 0.012F), 6.0F);
    std::size_t longest = 0;
    for (const auto& line : lines) {
        longest = std::max(longest, app::fmt::display_width(line));
    }
    gfx::ImGuiCanvas canvas(*ImGui::GetForegroundDrawList(), font_);
    double px = 22.0;
    for (; px > 10.0; px -= 1.0) {
        const double text_h = static_cast<double>(lines.size()) * canvas.line_height(px);
        const double text_w = canvas.text_width(std::string(longest, 'M'), px);
        if (text_h <= static_cast<double>(view.y - 2.0F * gap - 16.0F) &&
            text_w <= static_cast<double>(view.x - 2.0F * gap - 24.0F)) {
            break;
        }
    }
    const double line_h = canvas.line_height(px);
    const double width = canvas.text_width(std::string(longest, 'M'), px);
    ImDrawList* list = ImGui::GetForegroundDrawList();
    list->AddRectFilled(ImVec2{gap, gap},
                        ImVec2{gap + static_cast<float>(width) + 24.0F,
                               gap + static_cast<float>(line_h * static_cast<double>(lines.size())) + 16.0F},
                        IM_COL32(0, 0, 0, 158), 6.0F);
    double y = static_cast<double>(gap) + 8.0 + canvas.ascent(px);
    for (const auto& line : lines) {
        canvas.text(app::Vec2{static_cast<double>(gap) + 12.0, y}, line, px, Colour{0.88F, 0.92F, 1.0F});
        y += line_h;
    }
}

bool Interface::begin_panel(const char* id, float width, bool* open) {
    const ImVec2 view = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2{view.x * 0.5F, view.y * 0.5F}, ImGuiCond_Always, ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSizeConstraints(ImVec2{width, 0.0F}, ImVec2{width, view.y - 40.0F});
    return ImGui::Begin(id, open,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoCollapse);
}

void Interface::end_panel() { ImGui::End(); }

void Interface::rule() {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2{p.x + width, p.y}, packed(palette::PANEL_EDGE));
    ImGui::Dummy(ImVec2{width, 1.0F});
}

void Interface::label(const std::string& text, float size, Colour colour) {
    ImGui::PushFont(font_, gfx::font_px(size));
    ImGui::PushStyleColor(ImGuiCol_Text, im(colour));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void Interface::mission_panel(app::FlightApp& app) {
    // NAV -> choose target -> SEARCH -> summary -> EXECUTE | CANCEL. Every
    // number in the summary is the core's; this renames and formats (rule 77).
    auto& panel = app.mission_panel();
    if (!begin_panel("##mission", 760.0F, nullptr)) {
        end_panel();
        return;
    }
    label("MISSION COMPUTER", 20.0F, palette::PRIMARY);
    rule();

    ImGui::PushFont(font_, gfx::font_px(14.0F));
    const auto row = [&](const char* caption, const std::string& value, Colour colour, int step_id,
                         const std::function<void(int)>& step) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, im(palette::SECONDARY));
        ImGui::TextUnformatted(caption);
        ImGui::PopStyleColor();
        ImGui::SameLine(150.0F);
        ImGui::PushID(step_id);
        if (ImGui::Button("◀", ImVec2{44.0F, 0.0F})) {
            step(-1);
        }
        ImGui::SameLine();
        ImGui::PushFont(font_, gfx::font_px(18.0F));
        const float text_w = ImGui::CalcTextSize(value.c_str()).x;
        const float cell = 190.0F;
        const ImVec2 at = ImGui::GetCursorPos();
        ImGui::SetCursorPosX(at.x + std::max(0.0F, (cell - text_w) * 0.5F));
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, im(colour));
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine(at.x + cell + 10.0F);
        if (ImGui::Button("▶", ImVec2{44.0F, 0.0F})) {
            step(1);
        }
        ImGui::PopID();
    };
    const std::string target = panel.targets().empty() ? std::string{"—"} : app::fmt::upper(panel.current_target());
    row("TARGET", target, palette::TARGET, 1, [&](int d) { panel.step_target(d); });
    row("TARGET ORBIT", panel.orbit_label(), palette::PRIMARY, 2, [&](int d) { panel.step_altitude(d); });
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(panel.plan_button_text().c_str()).x - 20.0F));
    if (ImGui::Button(panel.plan_button_text().c_str())) {
        panel.request_plan();
    }

    // The summary.
    ImGui::BeginChild("##summary", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_AutoResizeY);
    ImGui::Dummy(ImVec2{0.0F, 0.0F});
    for (const auto& line : panel.summary()) {
        bool first = true;
        for (const auto& span : line) {
            if (!first) {
                ImGui::SameLine(0.0F, 0.0F);
            }
            first = false;
            ImGui::PushStyleColor(ImGuiCol_Text, im(span.colour));
            ImGui::TextUnformatted(span.text.empty() ? " " : span.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (panel.summary().empty()) {
        ImGui::Dummy(ImVec2{0.0F, 120.0F});
    }
    ImGui::EndChild();

    // One button per feasible alternative (rule 42): the table as a choice.
    if (!panel.choices().empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, im(palette::SECONDARY));
        ImGui::TextUnformatted("USE");
        ImGui::PopStyleColor();
        for (const auto& choice : panel.choices()) {
            ImGui::SameLine();
            if (ImGui::Button(choice.label.c_str(), ImVec2{140.0F, 0.0F}) && panel.on_alternative_chosen) {
                panel.on_alternative_chosen(choice.source_index);
            }
        }
    }
    if (!panel.status().empty()) {
        ImGui::PushFont(font_, gfx::font_px(13.0F));
        ImGui::PushStyleColor(ImGuiCol_Text, im(panel.status_colour()));
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextUnformatted(panel.status().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    rule();
    ImGui::BeginDisabled(!panel.execute_enabled());
    if (ImGui::Button("EXECUTE", ImVec2{150.0F, 40.0F}) && panel.on_execute) {
        panel.on_execute();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!panel.cancel_enabled());
    if (ImGui::Button("CANCEL", ImVec2{150.0F, 40.0F}) && panel.on_cancel) {
        panel.on_cancel();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - 80.0F));
    if (ImGui::Button("CLOSE", ImVec2{80.0F, 40.0F}) && panel.on_closed) {
        panel.on_closed();
    }
    ImGui::PopFont();
    end_panel();
}

void Interface::pause_menu(app::FlightApp& app) {
    // Four entries and four settings (rules 69, 70). Pausing stops SIMULATION
    // TIME and nothing else: the camera keeps answering and the interface lives.
    if (!begin_panel("##pause", 420.0F, nullptr)) {
        end_panel();
        return;
    }
    label("PAUSED", 24.0F, palette::PRIMARY);
    label("simulation time is stopped; the ship is untouched", 12.0F, palette::SECONDARY);
    rule();
    ImGui::PushFont(font_, gfx::font_px(15.0F));
    const float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("RESUME", ImVec2{width, 34.0F})) {
        app.set_paused(false);
    }
    if (ImGui::Button("CONTROLS", ImVec2{width, 34.0F})) {
        app.show_panel(app::FlightApp::Panel::Pause, false);
        app.show_panel(app::FlightApp::Panel::Help, true);
    }
    ImGui::PopFont();
    rule();
    label("SETTINGS", 14.0F, palette::SECONDARY);
    auto& settings = app.settings();
    const auto slider = [&](const char* key, const char* caption, double& value, float low, float high) {
        label(app::fmt::format("%s   %.2f", caption, value), 12.0F, palette::SECONDARY);
        float v = static_cast<float>(value);
        ImGui::PushID(key);
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderFloat("##slider", &v, low, high, "", ImGuiSliderFlags_None)) {
            app.apply_setting(key, static_cast<double>(v));
        }
        ImGui::PopID();
    };
    slider("mouse_sensitivity", "MOUSE SENSITIVITY", settings.mouse_sensitivity, 0.2F, 3.0F);
    slider("master_volume", "MASTER VOLUME", settings.master_volume, 0.0F, 1.0F);
    slider("effects_volume", "EFFECTS VOLUME", settings.effects_volume, 0.0F, 1.0F);
    slider("ui_scale", "UI SCALE", settings.ui_scale, 0.7F, 1.6F);
    rule();
    ImGui::PushFont(font_, gfx::font_px(15.0F));
    if (ImGui::Button("QUIT", ImVec2{width, 34.0F})) {
        app.request_quit(0);
    }
    ImGui::PopFont();
    end_panel();
}

void Interface::help_panel(app::FlightApp& app) {
    // Generated from the key table, not written by hand (rule 75).
    if (!begin_panel("##help", 760.0F, nullptr)) {
        end_panel();
        return;
    }
    label("CONTROLS", 22.0F, palette::PRIMARY);
    label("mouse: drag to look / orbit   wheel: zoom   click a cockpit button to use it", 12.0F, palette::SECONDARY);
    const auto groups = app::input::by_group();
    const float view_h = ImGui::GetIO().DisplaySize.y;
    ImGui::BeginChild("##keys", ImVec2{0.0F, std::min(view_h - 220.0F, 560.0F)}, ImGuiChildFlags_None);
    if (ImGui::BeginTable("##columns", 2)) {
        ImGui::TableNextRow();
        const std::size_t split = groups.size() / 2 + 1;
        for (int column = 0; column < 2; ++column) {
            ImGui::TableSetColumnIndex(column);
            for (std::size_t g = column == 0 ? 0 : split; g < (column == 0 ? split : groups.size()); ++g) {
                label(groups[g].name, 14.0F, palette::NAV);
                for (const auto* binding : groups[g].entries) {
                    label("  " + app::fmt::rpad(app::input::label(binding->action), 13) + "  " + binding->description,
                          12.0F, palette::SECONDARY);
                }
                ImGui::Dummy(ImVec2{0.0F, 4.0F});
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PushFont(font_, gfx::font_px(14.0F));
    if (ImGui::Button("CLOSE")) {
        app.show_panel(app::FlightApp::Panel::Help, false);
    }
    ImGui::PopFont();
    end_panel();
}

}  // namespace sf::ui
