#include "app/platform/sdl_input.hpp"

namespace sf::platform {

app::Key key_from_scancode(SDL_Scancode scancode) {
    using app::Key;
    switch (scancode) {
        case SDL_SCANCODE_A: return Key::A;
        case SDL_SCANCODE_B: return Key::B;
        case SDL_SCANCODE_C: return Key::C;
        case SDL_SCANCODE_D: return Key::D;
        case SDL_SCANCODE_E: return Key::E;
        case SDL_SCANCODE_F: return Key::F;
        case SDL_SCANCODE_G: return Key::G;
        case SDL_SCANCODE_I: return Key::I;
        case SDL_SCANCODE_J: return Key::J;
        case SDL_SCANCODE_K: return Key::K;
        case SDL_SCANCODE_L: return Key::L;
        case SDL_SCANCODE_M: return Key::M;
        case SDL_SCANCODE_N: return Key::N;
        case SDL_SCANCODE_O: return Key::O;
        case SDL_SCANCODE_P: return Key::P;
        case SDL_SCANCODE_Q: return Key::Q;
        case SDL_SCANCODE_R: return Key::R;
        case SDL_SCANCODE_S: return Key::S;
        case SDL_SCANCODE_T: return Key::T;
        case SDL_SCANCODE_U: return Key::U;
        case SDL_SCANCODE_V: return Key::V;
        case SDL_SCANCODE_W: return Key::W;
        case SDL_SCANCODE_X: return Key::X;
        case SDL_SCANCODE_Z: return Key::Z;
        case SDL_SCANCODE_0: return Key::Num0;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: return Key::Shift;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL: return Key::Ctrl;
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT: return Key::Alt;
        case SDL_SCANCODE_HOME: return Key::Home;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return Key::Enter;
        case SDL_SCANCODE_TAB: return Key::Tab;
        case SDL_SCANCODE_SPACE: return Key::Space;
        case SDL_SCANCODE_ESCAPE: return Key::Escape;
        case SDL_SCANCODE_LEFTBRACKET: return Key::BracketLeft;
        case SDL_SCANCODE_RIGHTBRACKET: return Key::BracketRight;
        case SDL_SCANCODE_APOSTROPHE: return Key::Apostrophe;
        case SDL_SCANCODE_SEMICOLON: return Key::Semicolon;
        case SDL_SCANCODE_PERIOD: return Key::Period;
        case SDL_SCANCODE_COMMA: return Key::Comma;
        case SDL_SCANCODE_GRAVE: return Key::QuoteLeft;
        case SDL_SCANCODE_PAGEUP: return Key::PageUp;
        case SDL_SCANCODE_PAGEDOWN: return Key::PageDown;
        case SDL_SCANCODE_F1: return Key::F1;
        case SDL_SCANCODE_F3: return Key::F3;
        case SDL_SCANCODE_F5: return Key::F5;
        case SDL_SCANCODE_F6: return Key::F6;
        case SDL_SCANCODE_F7: return Key::F7;
        case SDL_SCANCODE_F8: return Key::F8;
        case SDL_SCANCODE_UP: return Key::Up;
        case SDL_SCANCODE_DOWN: return Key::Down;
        case SDL_SCANCODE_LEFT: return Key::Left;
        case SDL_SCANCODE_RIGHT: return Key::Right;
        default: return Key::Unknown;
    }
}

bool SdlKeyboard::is_down(app::Key key) const {
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    const auto down = [&](SDL_Scancode s) { return static_cast<int>(s) < count && state[s]; };
    using app::Key;
    switch (key) {
        case Key::Shift: return down(SDL_SCANCODE_LSHIFT) || down(SDL_SCANCODE_RSHIFT);
        case Key::Ctrl: return down(SDL_SCANCODE_LCTRL) || down(SDL_SCANCODE_RCTRL);
        case Key::Alt: return down(SDL_SCANCODE_LALT) || down(SDL_SCANCODE_RALT);
        case Key::Enter: return down(SDL_SCANCODE_RETURN) || down(SDL_SCANCODE_KP_ENTER);
        default: break;
    }
    for (int s = 0; s < count; ++s) {
        if (state[s] && key_from_scancode(static_cast<SDL_Scancode>(s)) == key) {
            return true;
        }
    }
    return false;
}

}  // namespace sf::platform
