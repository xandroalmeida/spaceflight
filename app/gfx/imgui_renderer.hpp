#pragma once

// Dear ImGui's draw data, painted with SDL_GPU -- into the window and into the
// cockpit's displays alike.
//
// The official SDL_GPU backend keeps ONE vertex buffer per frame, which is right
// for one window and wrong here: every cockpit display and every control label
// is its own ImDrawData, drawn into its own texture, in the same frame. So each
// of them gets its own stream of buffers, uploaded in the frame's copy pass and
// drawn in its own render pass.

#include "app/gfx/gpu.hpp"

#include <imgui.h>

#include <map>
#include <string>

namespace sf::gfx {

class ImGuiRenderer {
public:
    explicit ImGuiRenderer(Gpu& gpu);
    ~ImGuiRenderer();

    // Pipelines for the formats the 2-D pass draws into.
    bool initialise(SDL_GPUTextureFormat window_format);

    // Creates, updates and destroys the textures ImGui asks for (the font atlas
    // grows as new sizes are used). Call after ImGui::Render(), before recording.
    void update_textures();

    // Copies one draw data's vertices and indices into its stream. Call inside
    // the frame's copy pass, once per stream per frame.
    void upload(SDL_GPUCopyPass* pass, const ImDrawData& data, const std::string& stream);

    // Draws it into the current render pass, whose target is `format` and
    // `width` x `height` pixels.
    void draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const ImDrawData& data, const std::string& stream,
              SDL_GPUTextureFormat format, std::uint32_t width, std::uint32_t height);

    // Releases every texture ImGui owns; call before destroying the context.
    void shutdown();

private:
    struct Stream {
        BufferHandle vertices;
        BufferHandle indices;
        TransferHandle vertex_transfer;
        TransferHandle index_transfer;
        std::uint32_t vertex_capacity{0};
        std::uint32_t index_capacity{0};
    };
    void update_texture(ImTextureData* texture);
    SDL_GPUGraphicsPipeline* pipeline_for(SDL_GPUTextureFormat format);

    Gpu& gpu_;
    ShaderHandle vertex_;
    ShaderHandle fragment_;
    SamplerHandle sampler_;
    std::map<SDL_GPUTextureFormat, PipelineHandle> pipelines_;
    std::map<std::string, Stream> streams_;
    TextureHandle white_;
};

// A draw list of its own, the size of one display, for Canvas drawing outside
// ImGui's windows.
struct OffscreenList {
    explicit OffscreenList(float width, float height);
    ImDrawList list;
    ImDrawData data;
    void begin();
    void end();
    float width;
    float height;
};

}  // namespace sf::gfx
