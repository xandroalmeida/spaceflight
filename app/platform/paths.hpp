#pragma once

// Where the data lives: the assets (textures, sounds, font), the SPICE kernels
// and the star catalogue.
//
// A binary copied somewhere else has to find them next to itself, and a binary
// run from the build tree has to find them in the source tree. So, in order:
//
//   1. the environment variable (SPACEFLIGHT_ASSET_DIR, SPACEFLIGHT_KERNEL_DIR,
//      SPACEFLIGHT_CATALOG_DIR);
//   2. next to the executable: <exe>/<name>, then <exe>/../share/spaceflight/<name>
//      (the layout `cmake --install` and the packaged binary use);
//   3. the source tree the build was configured from.

#include <string>

namespace sf::platform {

[[nodiscard]] std::string executable_directory();

// The first existing directory among the candidates above, or the compiled-in
// default when none exists (so the error names a path someone can act on).
[[nodiscard]] std::string data_directory(const char* environment_variable, const char* name,
                                         const std::string& compiled_default);

[[nodiscard]] std::string asset_directory();
[[nodiscard]] std::string kernel_directory();
[[nodiscard]] std::string catalogue_directory();

}  // namespace sf::platform
