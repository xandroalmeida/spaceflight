#include "app/gfx/imgui_renderer.hpp"

#include <algorithm>
#include <cstring>

namespace sf::gfx {

ImGuiRenderer::ImGuiRenderer(Gpu& gpu) : gpu_(gpu) {}

ImGuiRenderer::~ImGuiRenderer() = default;

bool ImGuiRenderer::initialise(SDL_GPUTextureFormat window_format) {
    vertex_ = gpu_.shader("ui", ShaderStage::Vertex, ShaderResources{0, 1});
    fragment_ = gpu_.shader("ui", ShaderStage::Fragment, ShaderResources{1, 0});
    if (!vertex_ || !fragment_) {
        return false;
    }
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SamplerHandle{gpu_.device(), SDL_CreateGPUSampler(gpu_.device(), &sampler)};

    // A white texel for draw commands that carry no texture.
    white_ = gpu_.texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, 1);
    const std::uint8_t white[4] = {255, 255, 255, 255};
    gpu_.upload(white_.get(), white, 1, 1, 4, false);

    return pipeline_for(window_format) != nullptr && pipeline_for(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) != nullptr;
}

SDL_GPUGraphicsPipeline* ImGuiRenderer::pipeline_for(SDL_GPUTextureFormat format) {
    if (const auto it = pipelines_.find(format); it != pipelines_.end()) {
        return it->second.get();
    }
    SDL_GPUVertexBufferDescription buffer{};
    buffer.slot = 0;
    buffer.pitch = sizeof(ImDrawVert);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(ImDrawVert, pos))};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(ImDrawVert, uv))};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, static_cast<Uint32>(offsetof(ImDrawVert, col))};

    SDL_GPUColorTargetDescription target{};
    target.format = format;
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex_.get();
    info.fragment_shader = fragment_.get();
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(gpu_.device(), &info);
    if (pipeline == nullptr) {
        SDL_Log("ui pipeline: %s", SDL_GetError());
        return nullptr;
    }
    pipelines_.emplace(format, PipelineHandle{gpu_.device(), pipeline});
    return pipeline;
}

void ImGuiRenderer::update_textures() {
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->Status != ImTextureStatus_OK) {
            update_texture(texture);
        }
    }
}

void ImGuiRenderer::update_texture(ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate) {
        auto handle = gpu_.texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                   static_cast<std::uint32_t>(tex->Width), static_cast<std::uint32_t>(tex->Height));
        SDL_GPUTexture* raw = handle.get();
        // Ownership passes to ImGui's texture record; released in shutdown() or
        // on WantDestroy.
        tex->BackendUserData = new TextureHandle(std::move(handle));
        tex->SetTexID(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(raw)));
    }
    if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
        auto* raw = reinterpret_cast<SDL_GPUTexture*>(static_cast<std::uintptr_t>(tex->GetTexID()));
        const bool whole = tex->Status == ImTextureStatus_WantCreate;
        const int x0 = whole ? 0 : tex->UpdateRect.x;
        const int y0 = whole ? 0 : tex->UpdateRect.y;
        const int w = whole ? tex->Width : tex->UpdateRect.w;
        const int h = whole ? tex->Height : tex->UpdateRect.h;
        const auto pitch = static_cast<std::size_t>(w * tex->BytesPerPixel);
        std::vector<std::uint8_t> block(pitch * static_cast<std::size_t>(h));
        for (int y = 0; y < h; ++y) {
            std::memcpy(block.data() + pitch * static_cast<std::size_t>(y), tex->GetPixelsAt(x0, y0 + y), pitch);
        }
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = static_cast<Uint32>(block.size());
        TransferHandle transfer{gpu_.device(), SDL_CreateGPUTransferBuffer(gpu_.device(), &info)};
        void* mapped = SDL_MapGPUTransferBuffer(gpu_.device(), transfer.get(), false);
        std::memcpy(mapped, block.data(), block.size());
        SDL_UnmapGPUTransferBuffer(gpu_.device(), transfer.get());
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_.device());
        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo from{};
        from.transfer_buffer = transfer.get();
        from.pixels_per_row = static_cast<Uint32>(w);
        from.rows_per_layer = static_cast<Uint32>(h);
        SDL_GPUTextureRegion to{};
        to.texture = raw;
        to.x = static_cast<Uint32>(x0);
        to.y = static_cast<Uint32>(y0);
        to.w = static_cast<Uint32>(w);
        to.h = static_cast<Uint32>(h);
        to.d = 1;
        SDL_UploadToGPUTexture(pass, &from, &to, false);
        SDL_EndGPUCopyPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        tex->SetStatus(ImTextureStatus_OK);
    }
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        delete static_cast<TextureHandle*>(tex->BackendUserData);
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

void ImGuiRenderer::shutdown() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    SDL_WaitForGPUIdle(gpu_.device());
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->BackendUserData != nullptr) {
            delete static_cast<TextureHandle*>(texture->BackendUserData);
            texture->BackendUserData = nullptr;
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

void ImGuiRenderer::upload(SDL_GPUCopyPass* pass, const ImDrawData& data, const std::string& name) {
    auto& stream = streams_[name];
    const auto vertex_bytes = static_cast<std::uint32_t>(static_cast<std::size_t>(data.TotalVtxCount) * sizeof(ImDrawVert));
    const auto index_bytes = static_cast<std::uint32_t>(static_cast<std::size_t>(data.TotalIdxCount) * sizeof(ImDrawIdx));
    if (vertex_bytes == 0 || index_bytes == 0) {
        return;
    }
    const auto grow = [&](BufferHandle& buffer, TransferHandle& transfer, std::uint32_t& capacity,
                          std::uint32_t needed, SDL_GPUBufferUsageFlags usage) {
        if (capacity >= needed && buffer) {
            return;
        }
        capacity = std::max(needed * 2, 65536U);
        buffer = gpu_.buffer(usage, capacity);
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = capacity;
        transfer = TransferHandle{gpu_.device(), SDL_CreateGPUTransferBuffer(gpu_.device(), &info)};
    };
    grow(stream.vertices, stream.vertex_transfer, stream.vertex_capacity, vertex_bytes, SDL_GPU_BUFFERUSAGE_VERTEX);
    grow(stream.indices, stream.index_transfer, stream.index_capacity, index_bytes, SDL_GPU_BUFFERUSAGE_INDEX);

    auto* vertices = static_cast<ImDrawVert*>(SDL_MapGPUTransferBuffer(gpu_.device(), stream.vertex_transfer.get(), true));
    auto* indices = static_cast<ImDrawIdx*>(SDL_MapGPUTransferBuffer(gpu_.device(), stream.index_transfer.get(), true));
    for (const ImDrawList* list : data.CmdLists) {
        std::memcpy(vertices, list->VtxBuffer.Data, static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
        std::memcpy(indices, list->IdxBuffer.Data, static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
        vertices += list->VtxBuffer.Size;
        indices += list->IdxBuffer.Size;
    }
    SDL_UnmapGPUTransferBuffer(gpu_.device(), stream.vertex_transfer.get());
    SDL_UnmapGPUTransferBuffer(gpu_.device(), stream.index_transfer.get());

    SDL_GPUTransferBufferLocation vertex_from{stream.vertex_transfer.get(), 0};
    SDL_GPUBufferRegion vertex_to{stream.vertices.get(), 0, vertex_bytes};
    SDL_UploadToGPUBuffer(pass, &vertex_from, &vertex_to, true);
    SDL_GPUTransferBufferLocation index_from{stream.index_transfer.get(), 0};
    SDL_GPUBufferRegion index_to{stream.indices.get(), 0, index_bytes};
    SDL_UploadToGPUBuffer(pass, &index_from, &index_to, true);
}

void ImGuiRenderer::draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const ImDrawData& data,
                         const std::string& name, SDL_GPUTextureFormat format, std::uint32_t width,
                         std::uint32_t height) {
    const auto it = streams_.find(name);
    if (it == streams_.end() || !it->second.vertices || data.TotalVtxCount == 0) {
        return;
    }
    auto& stream = it->second;
    SDL_GPUGraphicsPipeline* pipeline = pipeline_for(format);
    if (pipeline == nullptr) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_GPUBufferBinding vertex_binding{stream.vertices.get(), 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_GPUBufferBinding index_binding{stream.indices.get(), 0};
    SDL_BindGPUIndexBuffer(pass, &index_binding,
                           sizeof(ImDrawIdx) == 2 ? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_GPUViewport viewport{0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F};
    SDL_SetGPUViewport(pass, &viewport);

    // Display space -> clip space.
    const float scale[4] = {2.0F / data.DisplaySize.x, -2.0F / data.DisplaySize.y,
                            -1.0F - data.DisplayPos.x * (2.0F / data.DisplaySize.x),
                            1.0F + data.DisplayPos.y * (2.0F / data.DisplaySize.y)};
    SDL_PushGPUVertexUniformData(cmd, 0, scale, sizeof scale);

    const ImVec2 clip_offset = data.DisplayPos;
    const ImVec2 clip_scale = data.FramebufferScale;
    int global_vertex = 0;
    int global_index = 0;
    for (const ImDrawList* list : data.CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback != nullptr || command.ElemCount == 0) {
                continue;
            }
            ImVec2 clip_min{(command.ClipRect.x - clip_offset.x) * clip_scale.x,
                            (command.ClipRect.y - clip_offset.y) * clip_scale.y};
            ImVec2 clip_max{(command.ClipRect.z - clip_offset.x) * clip_scale.x,
                            (command.ClipRect.w - clip_offset.y) * clip_scale.y};
            clip_min.x = std::max(clip_min.x, 0.0F);
            clip_min.y = std::max(clip_min.y, 0.0F);
            clip_max.x = std::min(clip_max.x, static_cast<float>(width));
            clip_max.y = std::min(clip_max.y, static_cast<float>(height));
            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) {
                continue;
            }
            SDL_Rect scissor{static_cast<int>(clip_min.x), static_cast<int>(clip_min.y),
                             static_cast<int>(clip_max.x - clip_min.x), static_cast<int>(clip_max.y - clip_min.y)};
            SDL_SetGPUScissor(pass, &scissor);
            auto* texture = reinterpret_cast<SDL_GPUTexture*>(static_cast<std::uintptr_t>(command.GetTexID()));
            SDL_GPUTextureSamplerBinding binding{texture != nullptr ? texture : white_.get(), sampler_.get()};
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            SDL_DrawGPUIndexedPrimitives(pass, command.ElemCount, 1,
                                         command.IdxOffset + static_cast<Uint32>(global_index),
                                         static_cast<Sint32>(command.VtxOffset) + global_vertex, 0);
        }
        global_index += list->IdxBuffer.Size;
        global_vertex += list->VtxBuffer.Size;
    }
    SDL_Rect full{0, 0, static_cast<int>(width), static_cast<int>(height)};
    SDL_SetGPUScissor(pass, &full);
}

OffscreenList::OffscreenList(float w, float h) : list(ImGui::GetDrawListSharedData()), width(w), height(h) {}

void OffscreenList::begin() {
    list._ResetForNewFrame();
    list.PushClipRect(ImVec2{0.0F, 0.0F}, ImVec2{width, height});
    list.PushTexture(ImGui::GetIO().Fonts->TexRef);
}

void OffscreenList::end() {
    list._PopUnusedDrawCmd();
    data.Clear();
    data.Valid = true;
    data.CmdLists.push_back(&list);
    data.TotalVtxCount = list.VtxBuffer.Size;
    data.TotalIdxCount = list.IdxBuffer.Size;
    data.DisplayPos = ImVec2{0.0F, 0.0F};
    data.DisplaySize = ImVec2{width, height};
    data.FramebufferScale = ImVec2{1.0F, 1.0F};
}

}  // namespace sf::gfx
