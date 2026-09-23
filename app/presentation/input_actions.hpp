#pragma once

// Every key binding of the simulator, in one table (rule 13).
//
// The table is the source of truth for three things at once:
//
//   * what the cockpit reacts to;
//   * the in-game help panel;
//   * docs/gameplay/controls.md and controls.json (`spaceflight --dump-controls`).
//
// If the three diverge it is because someone wrote a key somewhere else, and
// that is exactly the mistake this exists to make impossible.
//
// Keys are the presentation's own enum and not SDL scancodes: the platform
// layer translates, so the table (and every test of it) needs no window. They
// are PHYSICAL keys -- `W` is the key where W is on a US layout -- because a
// flight control is a position under a finger, not a letter.

#include <string>
#include <string_view>
#include <vector>

namespace sf::app {

enum class Key {
    Unknown,
    A, B, C, D, E, F, G, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Z,
    Num0,
    Shift, Ctrl, Alt,
    Home, Enter, Tab, Space, Escape,
    BracketLeft, BracketRight, Apostrophe, Semicolon, Period, Comma, QuoteLeft,
    PageUp, PageDown,
    F1, F3, F5, F6, F7, F8,
    Up, Down, Left, Right,
};

struct KeyEvent {
    Key key{Key::Unknown};
    bool shift{false};
    bool ctrl{false};
    bool alt{false};
    bool pressed{true};
    bool echo{false};
};

struct Binding {
    const char* action;
    Key key;
    bool shift;
    bool ctrl;
    bool alt;
    const char* group;
    const char* description;
};

struct BindingGroup {
    std::string name;
    std::vector<const Binding*> entries;
};

// What the keyboard is doing right now.  Implemented by the platform layer, and
// by the tests.
class KeyboardState {
public:
    virtual ~KeyboardState() = default;
    [[nodiscard]] virtual bool is_down(Key key) const = 0;
};

namespace input {

[[nodiscard]] const std::vector<Binding>& bindings();
[[nodiscard]] const Binding* find(std::string_view action);

// The key's name as the documentation prints it: "Shift+P", "BracketRight".
[[nodiscard]] std::string key_name(Key key);
[[nodiscard]] std::string label(std::string_view action);

// In table order, grouped, for the help panel and the documentation.
[[nodiscard]] std::vector<BindingGroup> by_group();

// A key press matched against an action with the modifiers matched EXACTLY.
//
// Without this `point_prograde` (P) would fire with Shift+P alongside
// `point_retrograde`, and a pilot who asked for retrograde would get both
// commands -- the last one handled winning, which is the worst kind of input
// bug, because it depends on the order of a switch.
[[nodiscard]] bool pressed_exact(const KeyEvent& event, std::string_view action);

// A held action, modifiers NOT considered (the throttle, the RCS keys, free
// look): 1 while its key is down, 0 otherwise.
[[nodiscard]] double strength(const KeyboardState& keyboard, std::string_view action);
[[nodiscard]] bool held(const KeyboardState& keyboard, std::string_view action);

}  // namespace input
}  // namespace sf::app
