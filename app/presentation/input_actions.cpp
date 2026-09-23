#include "app/presentation/input_actions.hpp"

namespace sf::app::input {

// [action, key, shift, ctrl, alt, group, description]
//
// What CHANGED from the Milestone 5 shortcuts, and why: rule 13 gives
// W/A/S/D/Q/E to attitude and I/J/K/L/U/O to translation, and those ten keys
// were taken by the camera, the exposure and the optics switches. Flight gets
// the main keyboard; the technical tools move down to the function keys and to
// `Alt`. Nothing was removed.
const std::vector<Binding>& bindings() {
    static const std::vector<Binding> table = {
        // --- attitude (RCS rotation) ---
        {"pitch_up", Key::W, false, false, false, "ATITUDE", "arfagem para cima"},
        {"pitch_down", Key::S, false, false, false, "ATITUDE", "arfagem para baixo"},
        {"yaw_left", Key::A, false, false, false, "ATITUDE", "guinada à esquerda"},
        {"yaw_right", Key::D, false, false, false, "ATITUDE", "guinada à direita"},
        {"roll_left", Key::Q, false, false, false, "ATITUDE", "rolagem à esquerda"},
        {"roll_right", Key::E, false, false, false, "ATITUDE", "rolagem à direita"},

        // --- translation (RCS translation) ---
        {"translate_forward", Key::I, false, false, false, "TRANSLAÇÃO", "à frente"},
        {"translate_back", Key::K, false, false, false, "TRANSLAÇÃO", "atrás"},
        {"translate_left", Key::J, false, false, false, "TRANSLAÇÃO", "à esquerda"},
        {"translate_right", Key::L, false, false, false, "TRANSLAÇÃO", "à direita"},
        {"translate_up", Key::U, false, false, false, "TRANSLAÇÃO", "acima"},
        {"translate_down", Key::O, false, false, false, "TRANSLAÇÃO", "abaixo"},

        // --- main engine ---
        {"throttle_up", Key::Shift, false, false, false, "MOTOR", "abre o acelerador"},
        {"throttle_down", Key::Ctrl, false, false, false, "MOTOR", "fecha o acelerador"},
        {"throttle_full", Key::Z, false, false, false, "MOTOR", "acelerador cheio"},
        {"engine_cutoff", Key::X, false, false, false, "MOTOR", "corte do motor"},
        {"engine_mode", Key::G, false, false, false, "MOTOR", "modo IMPULSO / CRUZEIRO"},
        {"rcs_toggle", Key::V, false, false, false, "MOTOR", "RCS ligado / desligado"},
        {"rcs_mode", Key::B, false, false, false, "MOTOR", "RCS rotação / translação"},

        // --- pointing ---
        {"point_prograde", Key::P, false, false, false, "APONTAMENTO", "prógrado"},
        {"point_retrograde", Key::P, true, false, false, "APONTAMENTO", "retrógrado"},
        {"point_normal", Key::N, false, false, false, "APONTAMENTO", "normal"},
        {"point_anti_normal", Key::N, true, false, false, "APONTAMENTO", "anti-normal"},
        {"point_radial_out", Key::R, false, false, false, "APONTAMENTO", "radial para fora"},
        {"point_radial_in", Key::R, true, false, false, "APONTAMENTO", "radial para dentro"},
        {"point_target", Key::T, false, false, false, "APONTAMENTO", "para o alvo"},
        {"point_anti_target", Key::T, true, false, false, "APONTAMENTO", "contra o alvo"},
        {"point_hold", Key::Num0, false, false, false, "APONTAMENTO", "manter atitude"},

        // --- camera ---
        {"camera_cycle", Key::C, false, false, false, "CÂMERA", "alterna o modo de câmera"},
        {"camera_recentre", Key::Home, false, false, false, "CÂMERA", "recentra o olhar"},
        {"camera_free_look", Key::Alt, false, false, false, "CÂMERA", "olhar em volta (mantido)"},
        {"camera_zoom_in", Key::BracketRight, false, false, false, "CÂMERA", "aproxima"},
        {"camera_zoom_out", Key::BracketLeft, false, false, false, "CÂMERA", "afasta"},
        {"camera_focus_next", Key::F, false, false, false, "CÂMERA", "foca o próximo corpo"},

        // --- target and mission ---
        {"target_next", Key::Apostrophe, false, false, false, "MISSÃO", "próximo alvo"},
        {"target_prev", Key::Semicolon, false, false, false, "MISSÃO", "alvo anterior"},
        {"mission_plan", Key::J, true, false, false, "MISSÃO", "planeja a transferência"},
        {"mission_execute", Key::Enter, false, false, false, "MISSÃO", "executa o plano"},
        {"mission_abort", Key::K, true, false, false, "MISSÃO", "ABORTA a missão"},
        {"nav_panel", Key::Tab, false, false, false, "MISSÃO", "computador de navegação"},
        {"orbit_map", Key::M, false, false, false, "MISSÃO", "mapa orbital"},
        {"map_mode", Key::M, true, false, false, "MISSÃO", "mapa local / sistema solar"},

        // --- time ---
        {"warp_up", Key::Period, false, false, false, "TEMPO", "sobe o time warp"},
        {"warp_down", Key::Comma, false, false, false, "TEMPO", "desce o time warp"},
        {"pause", Key::Space, false, false, false, "TEMPO", "pausa"},
        {"menu", Key::Escape, false, false, false, "TEMPO", "menu"},

        // --- interface ---
        {"hud_cycle", Key::QuoteLeft, false, false, false, "INTERFACE", "HUD completo / mínimo / nenhum"},
        {"debug_hud", Key::F3, false, false, false, "INTERFACE", "HUD técnico (todos os números)"},
        {"help", Key::F1, false, false, false, "INTERFACE", "ajuda dos controles"},
        {"exposure_up", Key::PageUp, false, false, false, "INTERFACE", "exposição do céu +"},
        {"exposure_down", Key::PageDown, false, false, false, "INTERFACE", "exposição do céu -"},

        // --- technical tools (kept from M5) ---
        {"restart_orbit", Key::F5, false, false, false, "TÉCNICO", "reinicia a órbita de partida"},
        {"execution_model", Key::F6, false, false, false, "TÉCNICO", "planejador: finito / piloto automático"},
        {"visual_beta", Key::F7, false, false, false, "TÉCNICO", "escada de β visual"},
        {"body_scale", Key::F8, false, false, false, "TÉCNICO", "exagero de escala dos corpos"},
        {"cruise_burn", Key::C, false, false, true, "TÉCNICO", "queima de cruzeiro (β relativístico)"},
        {"optics_aberration", Key::A, false, false, true, "TÉCNICO", "aberração liga/desliga"},
        {"optics_doppler", Key::D, false, false, true, "TÉCNICO", "Doppler liga/desliga"},
        {"optics_beaming", Key::B, false, false, true, "TÉCNICO", "beaming liga/desliga"},
        {"optics_light_time", Key::L, false, false, true, "TÉCNICO", "tempo de luz liga/desliga"},
    };
    return table;
}

const Binding* find(std::string_view action) {
    for (const auto& binding : bindings()) {
        if (action == binding.action) {
            return &binding;
        }
    }
    return nullptr;
}

std::string key_name(Key key) {
    switch (key) {
        case Key::A: return "A";
        case Key::B: return "B";
        case Key::C: return "C";
        case Key::D: return "D";
        case Key::E: return "E";
        case Key::F: return "F";
        case Key::G: return "G";
        case Key::I: return "I";
        case Key::J: return "J";
        case Key::K: return "K";
        case Key::L: return "L";
        case Key::M: return "M";
        case Key::N: return "N";
        case Key::O: return "O";
        case Key::P: return "P";
        case Key::Q: return "Q";
        case Key::R: return "R";
        case Key::S: return "S";
        case Key::T: return "T";
        case Key::U: return "U";
        case Key::V: return "V";
        case Key::W: return "W";
        case Key::X: return "X";
        case Key::Z: return "Z";
        case Key::Num0: return "0";
        case Key::Shift: return "Shift";
        case Key::Ctrl: return "Ctrl";
        // macOS calls the key on its own "Option", but the modifiers say "Alt"
        // everywhere -- and controls.md cannot change with the machine that
        // generated it.
        case Key::Alt: return "Alt";
        case Key::Home: return "Home";
        case Key::Enter: return "Enter";
        case Key::Tab: return "Tab";
        case Key::Space: return "Space";
        case Key::Escape: return "Escape";
        case Key::BracketLeft: return "BracketLeft";
        case Key::BracketRight: return "BracketRight";
        case Key::Apostrophe: return "Apostrophe";
        case Key::Semicolon: return "Semicolon";
        case Key::Period: return "Period";
        case Key::Comma: return "Comma";
        case Key::QuoteLeft: return "QuoteLeft";
        case Key::PageUp: return "PageUp";
        case Key::PageDown: return "PageDown";
        case Key::F1: return "F1";
        case Key::F3: return "F3";
        case Key::F5: return "F5";
        case Key::F6: return "F6";
        case Key::F7: return "F7";
        case Key::F8: return "F8";
        case Key::Up: return "Up";
        case Key::Down: return "Down";
        case Key::Left: return "Left";
        case Key::Right: return "Right";
        case Key::Unknown: break;
    }
    return "?";
}

std::string label(std::string_view action) {
    const auto* binding = find(action);
    if (binding == nullptr) {
        return "?";
    }
    std::string out;
    if (binding->alt) {
        out += "Alt+";
    }
    if (binding->ctrl) {
        out += "Ctrl+";
    }
    if (binding->shift) {
        out += "Shift+";
    }
    return out + key_name(binding->key);
}

std::vector<BindingGroup> by_group() {
    std::vector<BindingGroup> groups;
    for (const auto& binding : bindings()) {
        BindingGroup* into = nullptr;
        for (auto& group : groups) {
            if (group.name == binding.group) {
                into = &group;
                break;
            }
        }
        if (into == nullptr) {
            groups.push_back(BindingGroup{binding.group, {}});
            into = &groups.back();
        }
        into->entries.push_back(&binding);
    }
    return groups;
}

bool pressed_exact(const KeyEvent& event, std::string_view action) {
    if (!event.pressed || event.echo) {
        return false;
    }
    const auto* binding = find(action);
    if (binding == nullptr || binding->key != event.key) {
        return false;
    }
    // A modifier key pressed on its own carries its own flag; it is matched by
    // the key alone.
    const bool own_shift = event.key == Key::Shift;
    const bool own_ctrl = event.key == Key::Ctrl;
    const bool own_alt = event.key == Key::Alt;
    return (own_shift || event.shift == binding->shift) && (own_ctrl || event.ctrl == binding->ctrl) &&
           (own_alt || event.alt == binding->alt);
}

double strength(const KeyboardState& keyboard, std::string_view action) {
    return held(keyboard, action) ? 1.0 : 0.0;
}

bool held(const KeyboardState& keyboard, std::string_view action) {
    const auto* binding = find(action);
    return binding != nullptr && keyboard.is_down(binding->key);
}

}  // namespace sf::app::input
