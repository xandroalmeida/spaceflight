// spaceflight: the scientific Solar System flight simulator (ADR-0009).
//
//   spaceflight                         the simulator, in a window
//   spaceflight --headless              no window, no GPU: flies itself and prints
//                                       the technical read-out (rule 62)
//   spaceflight --headless --destination Mars
//                                       the whole mission, without a screen
//   spaceflight --shots m7|m8 DIR       the milestone's demonstration, photographed
//   spaceflight --capture DIR           the Milestone 5 capture ladder
//   spaceflight --screenshot FILE       one frame, after --frames frames
//   spaceflight --dump-controls [DIR]   docs/gameplay/controls.md and .json
//
// Scripted runs (--shots, --capture, --screenshot) render OFF-SCREEN at
// --resolution (1920x1080 by default) and advance a fixed 1/60 s per frame, so
// the same command produces the same sequence on any machine; --window shows
// them on screen instead.

#include "app/gfx/gpu.hpp"
#include "app/gfx/renderer.hpp"
#include "app/platform/paths.hpp"
#include "app/platform/sdl_audio.hpp"
#include "app/platform/sdl_input.hpp"
#include "app/presentation/controls_doc.hpp"
#include "app/presentation/flight_app.hpp"
#include "app/presentation/headless_driver.hpp"
#include "app/presentation/shot_director.hpp"
#include "app/ui/interface.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>

#include <stb_image_write.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    bool headless{false};
    int frames{0};                    // 0: until quit (headless default below)
    std::string destination;
    std::string shots_script;
    std::string shots_dir;
    int shots_stop{0};
    std::string capture_dir;
    std::string screenshot;
    std::optional<std::string> dump_controls;
    int width{1920};
    int height{1080};
    bool resolution_given{false};
    bool force_window{false};
    bool gpu_debug{false};
    std::string kernels;
    std::string assets;
    std::string catalogue;
};

void usage() {
    std::fprintf(stderr,
                 "usage: spaceflight [--headless [--frames N] [--destination BODY]]\n"
                 "                   [--shots m7|m8 DIR [--stop N]] [--capture DIR]\n"
                 "                   [--screenshot FILE [--frames N]] [--dump-controls [DIR]]\n"
                 "                   [--resolution WxH] [--window] [--gpu-debug]\n"
                 "                   [--kernels DIR] [--assets DIR] [--catalogue FILE]\n");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(arg + " needs a value");
            }
            return argv[++i];
        };
        if (arg == "--headless") {
            o.headless = true;
        } else if (arg == "--frames") {
            o.frames = std::stoi(next());
        } else if (arg == "--destination") {
            o.destination = next();
        } else if (arg == "--shots") {
            o.shots_script = next();
            o.shots_dir = next();
        } else if (arg == "--stop") {
            o.shots_stop = std::stoi(next());
        } else if (arg == "--capture") {
            o.capture_dir = next();
        } else if (arg == "--screenshot") {
            o.screenshot = next();
        } else if (arg == "--dump-controls") {
            o.dump_controls = (i + 1 < argc && argv[i + 1][0] != '-') ? std::string{argv[++i]} : std::string{};
        } else if (arg == "--resolution") {
            const std::string value = next();
            const auto x = value.find('x');
            if (x == std::string::npos) {
                throw std::runtime_error("--resolution wants WIDTHxHEIGHT");
            }
            o.width = std::stoi(value.substr(0, x));
            o.height = std::stoi(value.substr(x + 1));
            o.resolution_given = true;
        } else if (arg == "--window") {
            o.force_window = true;
        } else if (arg == "--gpu-debug") {
            o.gpu_debug = true;
        } else if (arg == "--kernels") {
            o.kernels = next();
        } else if (arg == "--assets") {
            o.assets = next();
        } else if (arg == "--catalogue") {
            o.catalogue = next();
        } else if (arg == "--help" || arg == "-h") {
            usage();
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option " + arg);
        }
    }
    // The environment variables the Godot scripts used, still honoured.
    const auto env = [](const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr ? std::string{value} : std::string{};
    };
    if (o.shots_script.empty() && !env("SPACEFLIGHT_M7_SHOTS").empty()) {
        o.shots_script = "m7";
        o.shots_dir = env("SPACEFLIGHT_M7_SHOTS");
    }
    if (o.shots_script.empty() && !env("SPACEFLIGHT_M8_SHOTS").empty()) {
        o.shots_script = "m8";
        o.shots_dir = env("SPACEFLIGHT_M8_SHOTS");
    }
    if (o.shots_stop == 0 && !env("SPACEFLIGHT_SHOT_STOP").empty()) {
        o.shots_stop = std::stoi(env("SPACEFLIGHT_SHOT_STOP"));
    }
    if (o.capture_dir.empty()) {
        o.capture_dir = env("SPACEFLIGHT_CAPTURE");
    }
    return true;
}

// A scripted run steps the simulation as fast as the machine allows, and the
// main thread then holds the ephemeris lock (SPICE is not re-entrant) almost
// without pause: a transfer search, whose worker needs that lock for every
// candidate, crawls at a few percent of its speed. While one runs, the frame is
// paced to a real 60 Hz -- what the game does anyway behind vsync.
void pace_while_planning(const sf::app::FlightApp& app) {
    if (app.session().is_planning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

bool write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

bool save_png(const std::string& path, const sf::gfx::Pixels& pixels) {
    std::filesystem::create_directories(std::filesystem::path{path}.parent_path());
    // Opaque: the frame's alpha is the composite's and says nothing.
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(pixels.width * pixels.height * 3));
    for (std::size_t i = 0, j = 0; i < pixels.rgba.size(); i += 4, j += 3) {
        rgb[j] = pixels.rgba[i];
        rgb[j + 1] = pixels.rgba[i + 1];
        rgb[j + 2] = pixels.rgba[i + 2];
    }
    return stbi_write_png(path.c_str(), pixels.width, pixels.height, 3, rgb.data(), pixels.width * 3) != 0;
}

sf::app::Vec2 point(float x, float y) { return sf::app::Vec2{static_cast<double>(x), static_cast<double>(y)}; }

sf::app::FlightConfig flight_config(const Options& o) {
    sf::app::FlightConfig config{};
    config.kernel_directory = o.kernels.empty() ? sf::platform::kernel_directory() : o.kernels;
    config.catalogue_path =
        o.catalogue.empty() ? sf::platform::catalogue_directory() + "/bsc5.dat" : o.catalogue;
    config.headless = o.headless;
    config.headless_destination = o.destination;
    return config;
}

int run_headless(const Options& o) {
    // No window, no GPU, no sound: the FlightApp alone, a fixed 1/60 s a frame.
    sf::app::SilentAudio audio;
    sf::app::FlightApp app(audio);
    if (!app.initialise(flight_config(o))) {
        std::fprintf(stderr, "%s\n", app.error().c_str());
        return 1;
    }
    struct NoKeys final : sf::app::KeyboardState {
        [[nodiscard]] bool is_down(sf::app::Key) const override { return false; }
    } keys;
    const bool mission = !o.destination.empty() || std::getenv("SPACEFLIGHT_HEADLESS_MISSION") != nullptr ||
                         std::getenv("SPACEFLIGHT_HEADLESS_DESTINATION") != nullptr;
    // 450 frames at the fixed 1/60 s: the optics projection, three read-outs and
    // the start of the burn, in about a second. Past ~500 the schedule reaches
    // CRUISE at 1e7x under thrust, where every frame is minutes of powered flight
    // for the propagator -- that is the long demonstration, asked for with
    // --frames.
    const int frames = o.frames > 0 ? o.frames : (mission ? 20000 : 450);
    for (int i = 0; i < frames && !app.quit_requested().has_value(); ++i) {
        app.frame(1.0 / 60.0, keys);
        pace_while_planning(app);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options{};
    try {
        parse(argc, argv, options);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "spaceflight: %s\n", e.what());
        usage();
        return 2;
    }

    if (options.dump_controls.has_value()) {
        const std::filesystem::path dir =
            options.dump_controls->empty() ? std::filesystem::path{SPACEFLIGHT_SOURCE_DIR} / "docs" / "gameplay"
                                           : std::filesystem::path{*options.dump_controls};
        std::filesystem::create_directories(dir);
        if (!write_file(dir / "controls.md", sf::app::controls_doc::markdown()) ||
            !write_file(dir / "controls.json", sf::app::controls_doc::json())) {
            std::fprintf(stderr, "could not write %s\n", dir.string().c_str());
            return 1;
        }
        std::printf("wrote %s/controls.md and controls.json\n", dir.string().c_str());
        return 0;
    }

    if (options.headless) {
        return run_headless(options);
    }

    const bool scripted = !options.shots_script.empty() || !options.capture_dir.empty() || !options.screenshot.empty();
    const bool offscreen = scripted && !options.force_window;
    // SDL_GPU wants the video subsystem even without a window. On a machine with
    // no display the ordinary drivers refuse, and SDL's off-screen driver -- no
    // window system at all -- is what the scripted runs need there.
    if (!SDL_Init(offscreen ? SDL_INIT_VIDEO : (SDL_INIT_VIDEO | SDL_INIT_AUDIO))) {
        if (!offscreen) {
            std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
            return 1;
        }
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
            return 1;
        }
    }

    SDL_Window* window = nullptr;
    if (!offscreen) {
        const int w = options.resolution_given ? options.width : 1440;
        const int h = options.resolution_given ? options.height : 900;
        window = SDL_CreateWindow("Spaceflight", w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (window == nullptr) {
            std::fprintf(stderr, "window: %s\n", SDL_GetError());
            return 1;
        }
    }
    std::string error;
    auto gpu = sf::gfx::Gpu::create(window, options.gpu_debug, error);
    if (gpu == nullptr) {
        std::fprintf(stderr, "%s\n", error.c_str());
        // A scripted run on a machine with no GPU driver has not failed: it could
        // not run, which CTest reports as Skipped (77).
        return offscreen ? 77 : 1;
    }
    std::printf("GPU driver: %s%s\n", gpu->driver(), offscreen ? " (off-screen)" : "");
    std::fflush(stdout);
    if (window != nullptr) {
        // Vsync where the platform has it.
        SDL_SetGPUSwapchainParameters(gpu->device(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      SDL_GPU_PRESENTMODE_VSYNC);
    }

    const std::string assets = options.assets.empty() ? sf::platform::asset_directory() : options.assets;
    std::unique_ptr<sf::app::AudioSink> audio_sink;
    sf::platform::SdlAudio* sdl_audio = nullptr;
    if (!offscreen) {
        auto sink = std::make_unique<sf::platform::SdlAudio>(assets);
        sdl_audio = sink.get();
        audio_sink = std::move(sink);
    } else {
        audio_sink = std::make_unique<sf::app::SilentAudio>();
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    sf::ui::Interface interface;
    if (!interface.initialise(assets)) {
        std::fprintf(stderr, "no font in %s/fonts -- is the asset directory right? (--assets DIR)\n", assets.c_str());
        return 1;
    }

    {
        sf::app::FlightApp app(*audio_sink);
        if (!app.initialise(flight_config(options))) {
            std::fprintf(stderr, "%s\n", app.error().c_str());
            return 1;
        }
        const SDL_GPUTextureFormat swapchain_format =
            window != nullptr ? SDL_GetGPUSwapchainTextureFormat(gpu->device(), window)
                              : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        auto renderer_owner = std::make_unique<sf::gfx::Renderer>(*gpu, assets);
        sf::gfx::Renderer& renderer = *renderer_owner;
        if (!renderer.initialise(app, swapchain_format)) {
            std::fprintf(stderr, "could not build the renderer's pipelines\n");
            return 1;
        }

        std::string pending_screenshot;
        app.on_screenshot = [&](const std::string& path) { pending_screenshot = path; };
        app.on_mouse_capture = [&](bool captured) {
            if (window != nullptr) {
                SDL_SetWindowRelativeMouseMode(window, captured);
            }
        };
        if (!options.shots_script.empty()) {
            const auto script = options.shots_script == "m8" ? sf::app::ShotDirector::Script::M8
                                                             : sf::app::ShotDirector::Script::M7;
            app.set_shot_director(
                std::make_unique<sf::app::ShotDirector>(app, options.shots_dir, script, options.shots_stop));
        }
        if (!options.capture_dir.empty()) {
            app.set_capture_directory(options.capture_dir);
        }

        sf::platform::SdlKeyboard keyboard;
        struct NoKeys final : sf::app::KeyboardState {
            [[nodiscard]] bool is_down(sf::app::Key) const override { return false; }
        } no_keys;
        const sf::app::KeyboardState& keys = offscreen ? static_cast<const sf::app::KeyboardState&>(no_keys) : keyboard;

        auto last = std::chrono::steady_clock::now();
        int frame_number = 0;
        bool running = true;
        float ui_scale = 1.0F;
        while (running) {
            // --- events ---
            ImGuiIO& io = ImGui::GetIO();
            SDL_Event event;
            while (window != nullptr && SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        running = false;
                        break;
                    case SDL_EVENT_KEY_DOWN:
                    case SDL_EVENT_KEY_UP: {
                        sf::app::KeyEvent key{};
                        key.key = sf::platform::key_from_scancode(event.key.scancode);
                        key.shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                        key.ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
                        key.alt = (event.key.mod & SDL_KMOD_ALT) != 0;
                        key.pressed = event.type == SDL_EVENT_KEY_DOWN;
                        key.echo = event.key.repeat;
                        if (!io.WantTextInput) {
                            app.key_event(key);
                        }
                        break;
                    }
                    case SDL_EVENT_MOUSE_MOTION: {
                        io.AddMousePosEvent(event.motion.x / ui_scale, event.motion.y / ui_scale);
                        const bool free_look = keys.is_down(sf::app::Key::Alt);
                        if (!io.WantCaptureMouse || SDL_GetWindowRelativeMouseMode(window)) {
                            app.mouse_motion(point(event.motion.xrel, event.motion.yrel),
                                             point(event.motion.x, event.motion.y), free_look);
                        }
                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP: {
                        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                        const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                                           : event.button.button == SDL_BUTTON_RIGHT ? 1
                                                                                     : 2;
                        io.AddMouseButtonEvent(button, down);
                        if (!io.WantCaptureMouse || !down) {
                            if (button == 0) {
                                app.mouse_button(sf::app::MouseButton::Left, down,
                                                 point(event.button.x, event.button.y));
                            } else if (button == 1) {
                                app.mouse_button(sf::app::MouseButton::Right, down,
                                                 point(event.button.x, event.button.y));
                            }
                        }
                        break;
                    }
                    case SDL_EVENT_MOUSE_WHEEL: {
                        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
                        if (!io.WantCaptureMouse && event.wheel.y != 0.0F) {
                            const auto button =
                                event.wheel.y > 0.0F ? sf::app::MouseButton::WheelUp : sf::app::MouseButton::WheelDown;
                            app.mouse_button(button, true, sf::app::Vec2{});
                        }
                        break;
                    }
                    default: break;
                }
            }
            if (!running) {
                break;
            }

            // --- size ---
            int points_w = options.width;
            int points_h = options.height;
            int pixels_w = options.width;
            int pixels_h = options.height;
            if (window != nullptr) {
                SDL_GetWindowSize(window, &points_w, &points_h);
                SDL_GetWindowSizeInPixels(window, &pixels_w, &pixels_h);
            }
            if (points_w <= 0 || points_h <= 0 || pixels_w <= 0 || pixels_h <= 0) {
                SDL_Delay(16);
                continue;
            }
            app.set_viewport_size(sf::app::Vec2{static_cast<double>(points_w), static_cast<double>(points_h)});
            ui_scale = interface.scale(static_cast<float>(points_w), static_cast<float>(points_h), app.settings().ui_scale);
            io.DisplaySize = ImVec2{static_cast<float>(points_w) / ui_scale, static_cast<float>(points_h) / ui_scale};
            io.DisplayFramebufferScale = ImVec2{static_cast<float>(pixels_w) / io.DisplaySize.x,
                                                static_cast<float>(pixels_h) / io.DisplaySize.y};

            // --- simulation ---
            const auto now = std::chrono::steady_clock::now();
            double delta = std::chrono::duration<double>(now - last).count();
            last = now;
            if (scripted) {
                delta = 1.0 / 60.0;
                pace_while_planning(app);
            }
            delta = std::clamp(delta, 1.0e-4, 0.1);
            io.DeltaTime = static_cast<float>(delta);
            app.frame(delta, keys);
            if (sdl_audio != nullptr) {
                sdl_audio->pump();
            }

            // --- interface ---
            ImGui::NewFrame();
            interface.build(app);
            renderer.build_panels(app, interface.font());
            ImGui::Render();

            // --- frame ---
            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu->device());
            SDL_GPUTexture* swapchain = nullptr;
            Uint32 swapchain_w = 0;
            Uint32 swapchain_h = 0;
            if (window != nullptr) {
                SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &swapchain_w, &swapchain_h);
            }
            renderer.render(cmd, app, static_cast<std::uint32_t>(pixels_w), static_cast<std::uint32_t>(pixels_h),
                            ImGui::GetDrawData());
            if (swapchain != nullptr) {
                SDL_GPUBlitInfo blit{};
                blit.source.texture = renderer.frame_texture();
                blit.source.w = static_cast<Uint32>(pixels_w);
                blit.source.h = static_cast<Uint32>(pixels_h);
                blit.destination.texture = swapchain;
                blit.destination.w = swapchain_w;
                blit.destination.h = swapchain_h;
                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_LINEAR;
                SDL_BlitGPUTexture(cmd, &blit);
            }
            ++frame_number;
            if (!options.screenshot.empty() && frame_number >= std::max(options.frames, 1)) {
                pending_screenshot = options.screenshot;
                app.request_quit(0);
            }
            if (!pending_screenshot.empty()) {
                SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
                SDL_WaitForGPUFences(gpu->device(), true, &fence, 1);
                SDL_ReleaseGPUFence(gpu->device(), fence);
                const auto pixels = gpu->download(renderer.frame_texture(), static_cast<std::uint32_t>(pixels_w),
                                                  static_cast<std::uint32_t>(pixels_h));
                if (!save_png(pending_screenshot, pixels)) {
                    std::fprintf(stderr, "could not write %s\n", pending_screenshot.c_str());
                }
                pending_screenshot.clear();
            } else {
                SDL_SubmitGPUCommandBuffer(cmd);
            }

            if (app.quit_requested().has_value()) {
                running = false;
            }
            if (options.frames > 0 && options.screenshot.empty() && frame_number >= options.frames) {
                running = false;
            }
        }
        SDL_WaitForGPUIdle(gpu->device());
        renderer.ui().shutdown();
        const int code = app.quit_requested().value_or(0);
        ImGui::DestroyContext();
        // The renderer's GPU objects go before the device that made them.
        renderer_owner.reset();
        gpu.reset();
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        return code;
    }
}
