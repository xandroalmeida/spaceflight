# The application: everything that is not the scientific core (ADR-0009).
#
#   spaceflight_session       the core behind a presentation-sized API (no GPU)
#   spaceflight_presentation  cockpit, instruments, cameras, panels (no GPU)
#   spaceflight_gfx           the SDL_GPU renderer
#   spaceflight               the executable
#
# The first two build and test with no window, no device and no display; the
# tests in tests/presentation link them and nothing else from here.
include(third_party)
include(shaders)

add_library(spaceflight_session STATIC
    session/flight_session.cpp
    session/star_sky.cpp
    session/transfer_bridge.cpp)
target_include_directories(spaceflight_session PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(spaceflight_session
    PUBLIC spaceflight::core
    PRIVATE spaceflight::warnings)

add_library(spaceflight_presentation STATIC
    presentation/geometry.cpp
    presentation/format.cpp
    presentation/input_actions.cpp
    presentation/message_log.cpp
    presentation/flight_controls.cpp
    presentation/camera_rig.cpp
    presentation/audio_director.cpp
    presentation/controls_doc.cpp
    presentation/flight_app.cpp
    presentation/headless_driver.cpp
    presentation/shot_director.cpp
    presentation/instruments/instrument.cpp
    presentation/instruments/displays.cpp
    presentation/instruments/nav_display.cpp
    presentation/instruments/orbit_map.cpp
    presentation/scene/mesh.cpp
    presentation/scene/scene.cpp
    presentation/scene/ship_materials.cpp
    presentation/scene/spacecraft_visual.cpp
    presentation/scene/cockpit.cpp
    presentation/scene/celestial_view.cpp
    presentation/scene/planet_textures.cpp
    presentation/ui/mission_panel.cpp
    presentation/ui/debug_hud.cpp)
target_link_libraries(spaceflight_presentation
    PUBLIC spaceflight_session
    PRIVATE spaceflight::warnings spaceflight_stb)

set(SPACEFLIGHT_DEFAULT_ASSET_DIR "${CMAKE_SOURCE_DIR}/assets"
    CACHE PATH "Default asset directory compiled into the application")

spaceflight_compile_shaders(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/embedded_shaders.cpp"
    INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/common.glsl"
    SOURCES
        shaders/star.vert
        shaders/star.frag
        shaders/body.vert
        shaders/body.frag
        shaders/lit.vert
        shaders/lit.frag
        shaders/unlit.vert
        shaders/unlit.frag
        shaders/shadow.vert
        shaders/shadow.frag
        shaders/composite.vert
        shaders/composite.frag
        shaders/ui.vert
        shaders/ui.frag)

# The renderer and the platform: SDL3, ImGui, the embedded shaders.
add_library(spaceflight_gfx STATIC
    gfx/gpu.cpp
    gfx/imgui_renderer.cpp
    gfx/imgui_canvas.cpp
    gfx/renderer.cpp
    gfx/stb_image.cpp
    platform/paths.cpp
    platform/sdl_audio.cpp
    platform/sdl_input.cpp
    ui/interface.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/embedded_shaders.cpp")
target_link_libraries(spaceflight_gfx
    PUBLIC spaceflight_presentation spaceflight_imgui SDL3::SDL3-static spaceflight_stb
    PRIVATE spaceflight::warnings)
target_compile_definitions(spaceflight_gfx PRIVATE
    SPACEFLIGHT_DEFAULT_ASSET_DIR="${SPACEFLIGHT_DEFAULT_ASSET_DIR}"
    SPACEFLIGHT_DEFAULT_KERNEL_DIR="${SPACEFLIGHT_DEFAULT_KERNEL_DIR}"
    SPACEFLIGHT_DEFAULT_CATALOG_DIR="${SPACEFLIGHT_DEFAULT_CATALOG_DIR}")

add_executable(spaceflight main.cpp)
target_link_libraries(spaceflight PRIVATE spaceflight_gfx spaceflight::warnings)
target_compile_definitions(spaceflight PRIVATE SPACEFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# The standalone install (ADR-0009): the executable in bin/, and what it reads
# at run time under share/spaceflight/, where app/platform/paths.cpp looks for it
# relative to the executable -- so the installed tree can be moved or zipped as
# a whole.
#
#   cmake --install build --prefix <dir>
#
# The kernels and the catalogue are downloaded by scripts/fetch_kernels.sh and
# scripts/fetch_star_catalog.sh and are not in the repository; the install
# copies whatever the build was pointed at.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS spaceflight RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${SPACEFLIGHT_DEFAULT_ASSET_DIR}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/spaceflight/assets"
        PATTERN "*.md" EXCLUDE)
install(DIRECTORY "${SPACEFLIGHT_DEFAULT_KERNEL_DIR}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/spaceflight/kernels")
install(FILES "${SPACEFLIGHT_DEFAULT_CATALOG_DIR}/bsc5.dat"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/spaceflight/catalogs"
        OPTIONAL)
