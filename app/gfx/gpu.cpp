#include "app/gfx/gpu.hpp"

#include <algorithm>
#include <cstring>

namespace sf::gfx {

std::uint32_t mip_levels(std::uint32_t width, std::uint32_t height) {
    std::uint32_t levels = 1;
    std::uint32_t size = std::max(width, height);
    while (size > 1) {
        size >>= 1U;
        ++levels;
    }
    return levels;
}

std::unique_ptr<Gpu> Gpu::create(SDL_Window* window, bool debug, std::string& error) {
    // Every format the build could have produced on this host: SPIR-V for Vulkan,
    // MSL for Metal, DXBC for Direct3D 12 (Windows builds only).
    SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
#if defined(_WIN32)
    formats |= SDL_GPU_SHADERFORMAT_DXBC;
#endif
    SDL_GPUDevice* device = SDL_CreateGPUDevice(formats, debug, nullptr);
    if (device == nullptr) {
        error = std::string{"no GPU device: "} + SDL_GetError();
        return nullptr;
    }
    if (window != nullptr && !SDL_ClaimWindowForGPUDevice(device, window)) {
        error = std::string{"cannot draw to the window: "} + SDL_GetError();
        SDL_DestroyGPUDevice(device);
        return nullptr;
    }
    auto gpu = std::unique_ptr<Gpu>(new Gpu());
    gpu->device_ = device;
    gpu->window_ = window;
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
    if ((supported & SDL_GPU_SHADERFORMAT_MSL) != 0U) {
        gpu->format_ = SDL_GPU_SHADERFORMAT_MSL;
    } else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0U) {
        gpu->format_ = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if ((supported & SDL_GPU_SHADERFORMAT_DXBC) != 0U) {
        gpu->format_ = SDL_GPU_SHADERFORMAT_DXBC;
    } else {
        error = "the GPU device reads none of the shader formats this build produced";
        return nullptr;
    }
    return gpu;
}

Gpu::~Gpu() {
    if (device_ != nullptr) {
        SDL_WaitForGPUIdle(device_);
        if (window_ != nullptr) {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
        }
        SDL_DestroyGPUDevice(device_);
    }
}

ShaderHandle Gpu::shader(std::string_view name, ShaderStage stage, ShaderResources resources) const {
    const auto* embedded = find_embedded_shader(name, stage);
    if (embedded == nullptr) {
        SDL_Log("shader %.*s not embedded in this build", static_cast<int>(name.size()), name.data());
        return {};
    }
    SDL_GPUShaderCreateInfo info{};
    info.stage = stage == ShaderStage::Vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = resources.samplers;
    info.num_uniform_buffers = resources.uniform_buffers;
    info.format = format_;
    switch (format_) {
        case SDL_GPU_SHADERFORMAT_MSL:
            info.code = embedded->msl;
            info.code_size = embedded->msl_size;
            info.entrypoint = "main0";   // SPIRV-Cross renames main for Metal
            break;
        case SDL_GPU_SHADERFORMAT_DXBC:
            info.code = embedded->dxbc;
            info.code_size = embedded->dxbc_size;
            info.entrypoint = "main";
            break;
        default:
            info.code = embedded->spirv;
            info.code_size = embedded->spirv_size;
            info.entrypoint = "main";
            break;
    }
    if (info.code == nullptr || info.code_size == 0) {
        SDL_Log("shader %.*s has no code for this device's format", static_cast<int>(name.size()), name.data());
        return {};
    }
    SDL_GPUShader* shader = SDL_CreateGPUShader(device_, &info);
    if (shader == nullptr) {
        SDL_Log("shader %.*s: %s", static_cast<int>(name.size()), name.data(), SDL_GetError());
    }
    return ShaderHandle{device_, shader};
}

BufferHandle Gpu::buffer(SDL_GPUBufferUsageFlags usage, std::uint32_t size) const {
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = std::max<std::uint32_t>(size, 16);
    return BufferHandle{device_, SDL_CreateGPUBuffer(device_, &info)};
}

void Gpu::upload(SDL_GPUBuffer* buffer, const void* data, std::uint32_t size) const {
    if (size == 0) {
        return;
    }
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = size;
    TransferHandle transfer{device_, SDL_CreateGPUTransferBuffer(device_, &info)};
    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer.get(), false);
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device_, transfer.get());

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation from{transfer.get(), 0};
    SDL_GPUBufferRegion to{buffer, 0, size};
    SDL_UploadToGPUBuffer(pass, &from, &to, false);
    SDL_EndGPUCopyPass(pass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);
}

TextureHandle Gpu::texture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, std::uint32_t width,
                           std::uint32_t height, std::uint32_t levels, SDL_GPUSampleCount samples) const {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = std::max<std::uint32_t>(width, 1);
    info.height = std::max<std::uint32_t>(height, 1);
    info.layer_count_or_depth = 1;
    info.num_levels = std::max<std::uint32_t>(levels, 1);
    info.sample_count = samples;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &info);
    if (texture == nullptr) {
        SDL_Log("texture %ux%u: %s", width, height, SDL_GetError());
    }
    return TextureHandle{device_, texture};
}

void Gpu::upload(SDL_GPUTexture* texture, const void* pixels, std::uint32_t width, std::uint32_t height,
                 std::uint32_t bytes_per_pixel, bool generate_mips) const {
    const std::uint32_t size = width * height * bytes_per_pixel;
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = size;
    TransferHandle transfer{device_, SDL_CreateGPUTransferBuffer(device_, &info)};
    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer.get(), false);
    std::memcpy(mapped, pixels, size);
    SDL_UnmapGPUTransferBuffer(device_, transfer.get());

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo from{};
    from.transfer_buffer = transfer.get();
    from.pixels_per_row = width;
    from.rows_per_layer = height;
    SDL_GPUTextureRegion to{};
    to.texture = texture;
    to.w = width;
    to.h = height;
    to.d = 1;
    SDL_UploadToGPUTexture(pass, &from, &to, false);
    SDL_EndGPUCopyPass(pass);
    if (generate_mips) {
        SDL_GenerateMipmapsForGPUTexture(cmd, texture);
    }
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);
}

Pixels Gpu::download(SDL_GPUTexture* texture, std::uint32_t width, std::uint32_t height) const {
    Pixels out{static_cast<int>(width), static_cast<int>(height), {}};
    const std::uint32_t size = width * height * 4;
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = size;
    TransferHandle transfer{device_, SDL_CreateGPUTransferBuffer(device_, &info)};

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion from{};
    from.texture = texture;
    from.w = width;
    from.h = height;
    from.d = 1;
    SDL_GPUTextureTransferInfo to{};
    to.transfer_buffer = transfer.get();
    to.pixels_per_row = width;
    to.rows_per_layer = height;
    SDL_DownloadFromGPUTexture(pass, &from, &to);
    SDL_EndGPUCopyPass(pass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);

    const auto* mapped = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(device_, transfer.get(), false));
    out.rgba.assign(mapped, mapped + size);
    SDL_UnmapGPUTransferBuffer(device_, transfer.get());
    return out;
}

}  // namespace sf::gfx
