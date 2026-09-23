#pragma once

// SDL's keys to the presentation's (app/presentation/input_actions.hpp), by
// SCANCODE: the physical key, whatever the layout says it is.

#include "app/presentation/input_actions.hpp"

#include <SDL3/SDL.h>

namespace sf::platform {

[[nodiscard]] app::Key key_from_scancode(SDL_Scancode scancode);

class SdlKeyboard final : public app::KeyboardState {
public:
    [[nodiscard]] bool is_down(app::Key key) const override;
};

}  // namespace sf::platform
