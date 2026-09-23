#pragma once

// The shaders, compiled at build time and embedded in the executable
// (cmake/shaders.cmake).  One entry per stage; each carries every format the
// build could produce on this host, and the device picks the one it reads.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sf::gfx {

enum class ShaderStage { Vertex, Fragment };

struct EmbeddedShader {
    const char* name;
    ShaderStage stage;
    const std::uint8_t* spirv;
    std::size_t spirv_size;
    const std::uint8_t* msl;
    std::size_t msl_size;
    const std::uint8_t* dxbc;     // nullptr unless the build ran on Windows
    std::size_t dxbc_size;
};

[[nodiscard]] const EmbeddedShader* find_embedded_shader(std::string_view name, ShaderStage stage);

}  // namespace sf::gfx
