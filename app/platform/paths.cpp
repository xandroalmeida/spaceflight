#include "app/platform/paths.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>

namespace sf::platform {

std::string executable_directory() {
    const char* base = SDL_GetBasePath();
    return base != nullptr ? std::string{base} : std::string{"."};
}

std::string data_directory(const char* environment_variable, const char* name, const std::string& compiled_default) {
    namespace fs = std::filesystem;
    if (const char* value = std::getenv(environment_variable); value != nullptr && *value != '\0') {
        return value;
    }
    const fs::path exe = executable_directory();
    for (const fs::path& candidate : {exe / name, exe / ".." / "share" / "spaceflight" / name,
                                     exe / ".." / "Resources" / name}) {
        std::error_code error;
        if (fs::is_directory(candidate, error)) {
            return fs::weakly_canonical(candidate, error).string();
        }
    }
    return compiled_default;
}

std::string asset_directory() {
    return data_directory("SPACEFLIGHT_ASSET_DIR", "assets", SPACEFLIGHT_DEFAULT_ASSET_DIR);
}

std::string kernel_directory() {
    return data_directory("SPACEFLIGHT_KERNEL_DIR", "kernels", SPACEFLIGHT_DEFAULT_KERNEL_DIR);
}

std::string catalogue_directory() {
    return data_directory("SPACEFLIGHT_CATALOG_DIR", "catalogs", SPACEFLIGHT_DEFAULT_CATALOG_DIR);
}

}  // namespace sf::platform
