#pragma once

// The GPU device and the handful of operations the renderer needs from it, over
// SDL_GPU (ADR-0009): Metal on macOS, Vulkan on Linux, Direct3D 12 on Windows.
//
// The device is created WITHOUT a window when there is none. That is what lets
// the screenshot scripts and the GPU validation render off-screen and read the
// pixels back on a machine where nobody is looking -- the Godot scene needed a
// real framebuffer for both.

#include "app/gfx/shader_library.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sf::gfx {

struct ShaderResources {
    std::uint32_t samplers{0};
    std::uint32_t uniform_buffers{0};
};

// RAII for the few SDL_GPU objects the renderer keeps.
template <typename T, void (*Release)(SDL_GPUDevice*, T*)>
class Handle {
public:
    Handle() = default;
    Handle(SDL_GPUDevice* device, T* object) : device_(device), object_(object) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept { *this = std::move(other); }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            device_ = other.device_;
            object_ = other.object_;
            other.object_ = nullptr;
        }
        return *this;
    }
    ~Handle() { reset(); }
    void reset() {
        if (object_ != nullptr && device_ != nullptr) {
            Release(device_, object_);
        }
        object_ = nullptr;
    }
    [[nodiscard]] T* get() const { return object_; }
    explicit operator bool() const { return object_ != nullptr; }

private:
    SDL_GPUDevice* device_{nullptr};
    T* object_{nullptr};
};

using TextureHandle = Handle<SDL_GPUTexture, SDL_ReleaseGPUTexture>;
using BufferHandle = Handle<SDL_GPUBuffer, SDL_ReleaseGPUBuffer>;
using SamplerHandle = Handle<SDL_GPUSampler, SDL_ReleaseGPUSampler>;
using PipelineHandle = Handle<SDL_GPUGraphicsPipeline, SDL_ReleaseGPUGraphicsPipeline>;
using ShaderHandle = Handle<SDL_GPUShader, SDL_ReleaseGPUShader>;
using TransferHandle = Handle<SDL_GPUTransferBuffer, SDL_ReleaseGPUTransferBuffer>;

// Pixels read back from the GPU: 8-bit RGBA, top row first.
struct Pixels {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
};

class Gpu {
public:
    // `window` may be null: an off-screen device. Returns null, with the reason
    // in `error`, when no device could be made.
    static std::unique_ptr<Gpu> create(SDL_Window* window, bool debug, std::string& error);
    ~Gpu();
    Gpu(const Gpu&) = delete;
    Gpu& operator=(const Gpu&) = delete;

    [[nodiscard]] SDL_GPUDevice* device() const { return device_; }
    [[nodiscard]] SDL_Window* window() const { return window_; }
    [[nodiscard]] const char* driver() const { return SDL_GetGPUDeviceDriver(device_); }

    [[nodiscard]] ShaderHandle shader(std::string_view name, ShaderStage stage, ShaderResources resources) const;

    [[nodiscard]] BufferHandle buffer(SDL_GPUBufferUsageFlags usage, std::uint32_t size) const;
    // A one-off upload, submitted and waited for: for data made once at start-up.
    void upload(SDL_GPUBuffer* buffer, const void* data, std::uint32_t size) const;

    [[nodiscard]] TextureHandle texture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage,
                                        std::uint32_t width, std::uint32_t height, std::uint32_t levels = 1,
                                        SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1) const;
    // A one-off upload of level 0, then the mip chain generated on the GPU when
    // the texture has more than one level.
    void upload(SDL_GPUTexture* texture, const void* pixels, std::uint32_t width, std::uint32_t height,
                std::uint32_t bytes_per_pixel, bool generate_mips) const;

    // The whole of a texture, read back. Blocks until the GPU is done.
    [[nodiscard]] Pixels download(SDL_GPUTexture* texture, std::uint32_t width, std::uint32_t height) const;

    [[nodiscard]] bool supports_samples(SDL_GPUTextureFormat format, SDL_GPUSampleCount samples) const {
        return SDL_GPUTextureSupportsSampleCount(device_, format, samples);
    }

private:
    Gpu() = default;

    SDL_GPUDevice* device_{nullptr};
    SDL_Window* window_{nullptr};
    SDL_GPUShaderFormat format_{SDL_GPU_SHADERFORMAT_INVALID};
};

// How many mip levels a texture of this size has.
[[nodiscard]] std::uint32_t mip_levels(std::uint32_t width, std::uint32_t height);

}  // namespace sf::gfx
