#pragma once

// The renderer: what the Godot scene drew, drawn with SDL_GPU (ADR-0009).
//
// It reads the FlightApp and draws; it decides nothing about the ship, the
// planets or the sky. One frame is, in order:
//
//   instruments   the four cockpit displays and the controls' labels, each a 2-D
//                 draw list painted into its own texture
//   shadow        the ship's depth from the Sun
//   world         stars and bodies, in scene units, camera-relative, linear HDR
//   near          hull, cockpit, plume and jets, in metres, multisampled
//   composite     the near layer (filmic, premultiplied) over the world
//                 (linear), encoded to sRGB
//   interface     HUD, map, messages and panels, on top
//
// The frame is drawn into an off-screen texture and then copied to the window,
// so that a screenshot is the frame itself and not a second rendering of it.

#include "app/gfx/gpu.hpp"
#include "app/gfx/imgui_renderer.hpp"
#include "app/presentation/flight_app.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sf::gfx {

// The star field's debug switches (docs/validation/starfield-debug.md).
struct StarfieldOptions {
    bool debug{false};          // every star one size, one white
    bool flat_sprite{false};    // no size tracking, no radial falloff
    double debug_point_size{3.0};
    double base_point_size{2.0};
    double max_point_size{6.0};
};

// A camera for the star field alone: the validation harness's. At the origin,
// looking down -Z of `basis`; the field of view is the vertical one.
struct SkyView {
    app::Basis basis{};
    double fov_deg{75.0};
    double near{0.05};
    double far{2.0e5};
};

class Renderer {
public:
    Renderer(Gpu& gpu, std::string asset_directory);
    ~Renderer();

    // Pipelines, meshes, textures. `output_format` is what the window's swapchain
    // (or nothing, off-screen) wants the finished frame in.
    bool initialise(app::FlightApp& app, SDL_GPUTextureFormat output_format);

    // Only what the star field needs -- pipelines, samplers, the Planck table --
    // with no flight behind it: the starfield validation harness.
    bool initialise_sky(const render::PlanckTable* table, SDL_GPUTextureFormat output_format);

    // The star field alone on black, composited and encoded exactly as a game
    // frame's world is, into `frame_texture()`. The harness measures THIS path,
    // so a star it finds is a star the game draws.
    void render_sky(SDL_GPUCommandBuffer* cmd, const app::StarSky& sky, const SkyView& view, std::uint32_t width,
                    std::uint32_t height);

    // The cockpit displays and the controls' labels, as 2-D draw lists. Between
    // ImGui::NewFrame() and ImGui::Render(): they need the frame's font atlas.
    void build_panels(app::FlightApp& app, ImFont* font);

    // Records the whole frame into `cmd`, finishing in the frame texture
    // (`frame_texture()`), `width` x `height` pixels. `ui` may be null.
    void render(SDL_GPUCommandBuffer* cmd, app::FlightApp& app, std::uint32_t width, std::uint32_t height,
                const ImDrawData* ui);

    [[nodiscard]] SDL_GPUTexture* frame_texture() const { return frame_.get(); }
    [[nodiscard]] SDL_GPUTextureFormat frame_format() const { return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; }
    [[nodiscard]] ImGuiRenderer& ui() { return ui_; }

    StarfieldOptions starfield{};
    // Off when the harness wants the world alone (the star field's evidence).
    bool draw_near_field{true};
    // Clear colour of the world, linear; the scene's was a near-black blue.
    float background[3] = {0.0003F, 0.0004F, 0.0007F};

private:
    struct GpuMesh {
        BufferHandle vertices;
        BufferHandle indices;
        std::uint32_t index_count{0};
    };
    struct DrawItem {
        const app::Part* part;
        app::Transform3 model;     // near world
        double distance{0.0};      // to the camera, for sorting the transparent ones
    };

    bool create_pipelines(SDL_GPUTextureFormat output_format);
    void create_shared(const render::PlanckTable* planck_table);
    void upload_stars(SDL_GPUCopyPass* pass, const app::StarSky& sky);
    void draw_stars(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const float* view_projection,
                    const app::StarSky& sky);
    void ensure_targets(std::uint32_t width, std::uint32_t height);
    const GpuMesh* mesh(const std::string& name);
    SDL_GPUTexture* texture(const std::string& path, bool srgb);
    void upload_frame_data(SDL_GPUCommandBuffer* cmd, app::FlightApp& app, const ImDrawData* ui);
    void render_panels(SDL_GPUCommandBuffer* cmd, app::FlightApp& app);
    void render_shadow(SDL_GPUCommandBuffer* cmd, const std::vector<DrawItem>& items);
    void render_world(SDL_GPUCommandBuffer* cmd, app::FlightApp& app);
    void render_near(SDL_GPUCommandBuffer* cmd, app::FlightApp& app, std::vector<DrawItem>& items);
    void render_composite(SDL_GPUCommandBuffer* cmd);
    void render_ui(SDL_GPUCommandBuffer* cmd, const ImDrawData* ui);
    std::vector<DrawItem> collect_near(app::FlightApp& app, std::vector<app::Part>& storage);
    SDL_GPUTexture* dynamic_texture(const std::string& name);

    Gpu& gpu_;
    ImGuiRenderer ui_;
    std::string assets_;
    SDL_GPUTextureFormat output_format_{SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM};
    SDL_GPUSampleCount near_samples_{SDL_GPU_SAMPLECOUNT_1};

    // Shaders and pipelines.
    std::map<std::string, ShaderHandle> shaders_;
    PipelineHandle body_pipeline_;
    PipelineHandle star_pipeline_;
    PipelineHandle lit_cull_pipeline_;
    PipelineHandle lit_double_pipeline_;
    PipelineHandle lit_alpha_pipeline_;
    PipelineHandle unlit_opaque_pipeline_;
    PipelineHandle unlit_alpha_pipeline_;
    PipelineHandle additive_pipeline_;
    PipelineHandle shadow_pipeline_;
    PipelineHandle composite_pipeline_;

    SamplerHandle repeat_sampler_;
    SamplerHandle clamp_sampler_;
    SamplerHandle nearest_sampler_;
    SamplerHandle shadow_sampler_;

    // Targets.
    std::uint32_t width_{0};
    std::uint32_t height_{0};
    TextureHandle world_colour_;
    TextureHandle world_depth_;
    TextureHandle near_colour_msaa_;
    TextureHandle near_depth_;
    TextureHandle near_colour_;
    TextureHandle shadow_map_;
    TextureHandle frame_;
    static constexpr std::uint32_t SHADOW_SIZE = 4096;

    // Resources.
    std::map<std::string, GpuMesh> meshes_;
    std::map<std::string, TextureHandle> textures_;
    TextureHandle planck_;
    TextureHandle white_;
    TextureHandle black_;
    TextureHandle flat_normal_;
    BufferHandle stars_;
    TransferHandle stars_transfer_;
    std::uint32_t star_capacity_{0};
    std::uint32_t star_count_{0};
    const app::MeshLibrary* library_{nullptr};

    // The 2-D panels drawn into the cockpit: one texture and one list each.
    struct Panel {
        std::string name;
        TextureHandle texture;
        std::unique_ptr<OffscreenList> list;
        std::uint32_t width{0};
        std::uint32_t height{0};
        bool active{false};
    };
    std::vector<Panel> displays_;
    std::vector<Panel> captions_;

    // Per-frame near-field lighting.
    app::Vec3 sun_direction_{1.0, 0.0, 0.0};
    float shadow_matrix_[16]{};
};

}  // namespace sf::gfx
