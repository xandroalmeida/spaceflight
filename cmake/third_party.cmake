# Third-party code for the application (ADR-0009): the window, the GPU, the user
# interface and the shader toolchain.  NONE of it is linked into
# `spaceflight_core`, and the core's test suite builds without any of it
# (-DSPACEFLIGHT_BUILD_APP=OFF).
#
# Every dependency is pinned by version AND by the SHA-256 of its tarball.  The
# tarballs are cached in external/downloads, so a second build directory does not
# go back to the network -- the same policy scripts/fetch_cspice.sh follows for
# CSPICE.
#
#   SDL3         window, input, audio and the GPU API (Metal / Vulkan / D3D12)
#   Dear ImGui   panels and menus; its draw lists also paint the instruments
#   stb          PNG/JPEG decoding, PNG encoding, Perlin noise
#   glslang      GLSL -> SPIR-V, at BUILD time only (a host tool)
#   SPIRV-Cross  SPIR-V -> MSL / HLSL, at BUILD time only (a host tool)

include(FetchContent)

set(SPACEFLIGHT_DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/external/downloads" CACHE PATH
    "Where third-party tarballs are cached between build directories")

# Warnings in code we do not own are not ours to fix, and a few hundred of them
# per translation unit is how a real warning goes unnoticed.
function(spaceflight_quiet_target target)
    if(TARGET ${target})
        get_target_property(_aliased ${target} ALIASED_TARGET)
        if(_aliased)
            set(target ${_aliased})
        endif()
        get_target_property(_type ${target} TYPE)
        if(NOT _type STREQUAL "INTERFACE_LIBRARY")
            if(MSVC)
                target_compile_options(${target} PRIVATE /w)
            else()
                target_compile_options(${target} PRIVATE -w)
            endif()
        endif()
    endif()
endfunction()

# --- SDL3 --------------------------------------------------------------------
FetchContent_Declare(sdl3
    URL "https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz"
    URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
    DOWNLOAD_DIR "${SPACEFLIGHT_DOWNLOAD_DIR}"
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL)
# Static: the binary has to run without an SDL installed next to it.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
# The camera, the sensors and the haptics are not used; leaving them out removes
# a permission prompt on macOS and a handful of system libraries on Linux.
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sdl3)

# --- Dear ImGui --------------------------------------------------------------
FetchContent_Declare(imgui
    URL "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz"
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    DOWNLOAD_DIR "${SPACEFLIGHT_DOWNLOAD_DIR}"
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(imgui)

# The core of ImGui has no platform and no GPU in it, and that is the half the
# PRESENTATION library links: instruments are drawn into ImDrawLists and a test
# can build those lists with no window and no device.  The SDL3 platform backend
# (input) is a separate target, linked only by the application.
add_library(spaceflight_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_include_directories(spaceflight_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
# 32-bit indices: the solar-system map and the debug read-out easily pass 65 536
# vertices in one list, and a wrapped 16-bit index draws garbage without a word.
target_compile_definitions(spaceflight_imgui PUBLIC
    ImDrawIdx=unsigned\ int
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
spaceflight_quiet_target(spaceflight_imgui)

add_library(spaceflight_imgui_sdl3 STATIC
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp")
target_include_directories(spaceflight_imgui_sdl3 SYSTEM PUBLIC "${imgui_SOURCE_DIR}/backends")
target_link_libraries(spaceflight_imgui_sdl3 PUBLIC spaceflight_imgui SDL3::SDL3-static)
spaceflight_quiet_target(spaceflight_imgui_sdl3)

# --- stb ---------------------------------------------------------------------
FetchContent_Declare(stb
    URL "https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz"
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
    DOWNLOAD_DIR "${SPACEFLIGHT_DOWNLOAD_DIR}"
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(stb)
add_library(spaceflight_stb INTERFACE)
target_include_directories(spaceflight_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")

# --- the shader toolchain (host tools, never linked) -------------------------
FetchContent_Declare(glslang
    URL "https://github.com/KhronosGroup/glslang/archive/refs/tags/16.6.0.tar.gz"
    URL_HASH SHA256=9c09b901149c729df745057dafa815278aaa101b84d2b6e14f16a42de52f97f2
    DOWNLOAD_DIR "${SPACEFLIGHT_DOWNLOAD_DIR}"
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)             # no SPIRV-Tools optimiser
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glslang)

FetchContent_Declare(spirv_cross
    URL "https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/vulkan-sdk-1.4.357.0.tar.gz"
    URL_HASH SHA256=97c910326afdd44d794ce8561326fa675fd1958b27142f03295403044d639639
    DOWNLOAD_DIR "${SPACEFLIGHT_DOWNLOAD_DIR}"
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL)
set(SPIRV_CROSS_CLI ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spirv_cross)

foreach(_tool_target glslang glslang-standalone SPIRV spirv-cross spirv-cross-core
        spirv-cross-glsl spirv-cross-msl spirv-cross-hlsl spirv-cross-reflect
        spirv-cross-cpp GenericCodeGen MachineIndependent OSDependent)
    spaceflight_quiet_target(${_tool_target})
endforeach()
