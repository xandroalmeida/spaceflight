#include "app/gfx/renderer.hpp"

#include "app/gfx/imgui_canvas.hpp"
#include "app/presentation/scene/planet_textures.hpp"
#include "core/render/blackbody.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numbers>

namespace sf::gfx {
namespace {

using app::Basis;
using app::Transform3;
using app::Vec3;

constexpr double kPi = std::numbers::pi;

// Column-major 4x4, the layout GLSL's std140 mat4 reads.
struct Mat4 {
    float m[16]{};

    static Mat4 identity() {
        Mat4 out;
        out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0F;
        return out;
    }
    static Mat4 from(const Transform3& t) {
        Mat4 out;
        const Vec3* columns[3] = {&t.basis.x, &t.basis.y, &t.basis.z};
        for (int c = 0; c < 3; ++c) {
            out.m[c * 4 + 0] = static_cast<float>(columns[c]->x);
            out.m[c * 4 + 1] = static_cast<float>(columns[c]->y);
            out.m[c * 4 + 2] = static_cast<float>(columns[c]->z);
        }
        out.m[12] = static_cast<float>(t.origin.x);
        out.m[13] = static_cast<float>(t.origin.y);
        out.m[14] = static_cast<float>(t.origin.z);
        out.m[15] = 1.0F;
        return out;
    }
    Mat4 operator*(const Mat4& o) const {
        Mat4 out;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                float sum = 0.0F;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + r] * o.m[c * 4 + k];
                }
                out.m[c * 4 + r] = sum;
            }
        }
        return out;
    }
};

// The perspective of the scene's cameras, with the depth REVERSED: the near
// plane maps to 1 and the far plane to 0. With a 32-bit float depth buffer that
// spends the float's exponent where the perspective divide throws precision
// away, and a sky sphere 1.9e5 units out keeps its own depth -- the quantisation
// that hid the Milestone 6 star field has no room to happen.
Mat4 perspective(double fov_y_deg, double aspect, double near, double far) {
    const double f = 1.0 / std::tan(fov_y_deg * kPi / 360.0);
    Mat4 out;
    out.m[0] = static_cast<float>(f / aspect);
    out.m[5] = static_cast<float>(f);
    out.m[10] = static_cast<float>(near / (far - near));
    out.m[11] = -1.0F;
    out.m[14] = static_cast<float>(near * far / (far - near));
    return out;
}

// The inverse of a camera pose: rotation by the transpose, then the position.
Mat4 view_of(const Basis& basis, const Vec3& position) {
    const Basis inverse = basis.transposed();
    return Mat4::from(Transform3{inverse, inverse * (position * -1.0)});
}

struct Vec4f {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{0.0F};
};

Vec4f linear(app::Colour c, double energy = 1.0) {
    const auto l = c.to_linear();
    const auto e = static_cast<float>(energy);
    return Vec4f{l.r * e, l.g * e, l.b * e, c.a};
}

Vec4f v4(const Vec3& v, double w = 0.0) {
    return Vec4f{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), static_cast<float>(w)};
}

// --- uniform blocks, std140 --------------------------------------------------

struct StarVertexBlock {
    Mat4 view_projection;
    Vec4f viewport;
    Vec4f params;
    Vec4f debug;
};

struct BodyVertexBlock {
    Mat4 model;
    Mat4 view_projection;
    Vec4f velocity_c;
    Vec4f optics;
    Vec4f reflectance;
    Vec4f flags;
};

struct BodyFragmentBlock {
    Vec4f sun_direction;
    Vec4f sun_light;
    Vec4f ambient;
    Vec4f maps;
    Vec4f clouds;
    Vec4f atmosphere_colour;
    Vec4f surface;
    Vec4f reflectance;
};

struct PlumeFragmentBlock {
    Vec4f camera_position;
    Vec4f near_colour;
    Vec4f far_colour;
    Vec4f flow;
    Vec4f shape;
};

struct LitVertexBlock {
    Mat4 model;
    Mat4 view_projection;
    Mat4 normal_matrix;
    Vec4f uv_scale;
};

struct LitFragmentBlock {
    Vec4f albedo;
    Vec4f emission;
    Vec4f material;
    Vec4f camera_position;
    Vec4f sun_direction;
    Vec4f sun_light;
    Vec4f ambient;
    Mat4 shadow_matrix;
    Vec4f shadow_params;
    Vec4f point_position[4];
    Vec4f point_colour[4];
    Vec4f point_count;
};

struct UnlitFragmentBlock {
    Vec4f colour;
    Vec4f flags;
};

// The lights of the scene, as the Godot scene set them.
constexpr app::Colour kSunColour{1.0F, 0.97F, 0.92F};
constexpr double kWorldSunEnergy = 1.35;
constexpr app::Colour kWorldAmbient{0.05F, 0.06F, 0.08F};
constexpr double kWorldAmbientEnergy = 0.55;
constexpr double kNearSunEnergy = 2.6;
constexpr app::Colour kNearAmbient{0.07F, 0.08F, 0.10F};
constexpr double kNearAmbientEnergy = 0.5;
// The shadow map covers the whole ship: 22 m long, 14 m of radiators.
constexpr double kShadowExtent = 20.0;
constexpr double kShadowDepth = 60.0;

SDL_GPUVertexBufferDescription mesh_buffer() {
    SDL_GPUVertexBufferDescription buffer{};
    buffer.slot = 0;
    buffer.pitch = sizeof(app::MeshVertex);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    return buffer;
}

void mesh_attributes(SDL_GPUVertexAttribute (&attributes)[4]) {
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(app::MeshVertex, position))};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(app::MeshVertex, normal))};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(app::MeshVertex, uv))};
    attributes[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, static_cast<Uint32>(offsetof(app::MeshVertex, tangent))};
}

SDL_GPUColorTargetBlendState opaque_blend() { return SDL_GPUColorTargetBlendState{}; }

SDL_GPUColorTargetBlendState alpha_blend() {
    SDL_GPUColorTargetBlendState b{};
    b.enable_blend = true;
    b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    return b;
}

// Additive: the colour is added and the alpha left alone. A flame writes light,
// not coverage, and in the premultiplied near layer that is exactly what lets it
// show over the world.
SDL_GPUColorTargetBlendState additive_blend() {
    SDL_GPUColorTargetBlendState b{};
    b.enable_blend = true;
    b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    return b;
}

std::string first_existing(const std::string& assets, const std::string& spec, std::string& procedural) {
    std::size_t start = 0;
    while (start <= spec.size()) {
        const auto bar = spec.find('|', start);
        const std::string option = spec.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
        if (option.rfind("procedural:", 0) == 0) {
            procedural = option.substr(11);
            return {};
        }
        if (!option.empty() && std::filesystem::exists(assets + "/" + option)) {
            return assets + "/" + option;
        }
        if (bar == std::string::npos) {
            break;
        }
        start = bar + 1;
    }
    return {};
}

}  // namespace

Renderer::Renderer(Gpu& gpu, std::string asset_directory) : gpu_(gpu), ui_(gpu), assets_(std::move(asset_directory)) {}

Renderer::~Renderer() = default;

bool Renderer::initialise(app::FlightApp& app, SDL_GPUTextureFormat output_format) {
    output_format_ = output_format;
    library_ = &app.meshes();
    if (!ui_.initialise(output_format) || !create_pipelines(output_format)) {
        return false;
    }

    create_shared(app.sky().planck_table());

    // Every mesh the scene knows now, uploaded once; the sphere every body is.
    app.meshes().add("body:sphere", app::mesh::sphere(1.0, 2.0, 64, 32));
    (void)app.cockpit().control_parts();   // makes the control faces' quads exist
    for (const auto& [name, data] : app.meshes().all()) {
        (void)mesh(name);
    }

    // The cockpit's displays and the controls' labels.
    for (const auto& display : app.cockpit().displays()) {
        Panel panel{};
        panel.name = display.name;
        panel.width = static_cast<std::uint32_t>(display.width);
        panel.height = static_cast<std::uint32_t>(display.height);
        panel.texture = gpu_.texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, panel.width,
                                     panel.height);
        panel.list = std::make_unique<OffscreenList>(static_cast<float>(panel.width), static_cast<float>(panel.height));
        displays_.push_back(std::move(panel));
    }
    for (const auto& control : app.cockpit().controls()) {
        Panel panel{};
        panel.name = control.label;
        panel.width = 512;
        panel.height = static_cast<std::uint32_t>(std::lround(512.0 * control.half_size.y / control.half_size.x));
        panel.texture = gpu_.texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, panel.width,
                                     panel.height);
        panel.list = std::make_unique<OffscreenList>(static_cast<float>(panel.width), static_cast<float>(panel.height));
        captions_.push_back(std::move(panel));
    }
    return true;
}

void Renderer::create_shared(const render::PlanckTable* planck_table) {
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler.enable_anisotropy = true;
    sampler.max_anisotropy = 8.0F;
    sampler.max_lod = 1000.0F;
    repeat_sampler_ = SamplerHandle{gpu_.device(), SDL_CreateGPUSampler(gpu_.device(), &sampler)};
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.enable_anisotropy = false;
    clamp_sampler_ = SamplerHandle{gpu_.device(), SDL_CreateGPUSampler(gpu_.device(), &sampler)};
    sampler.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    nearest_sampler_ = SamplerHandle{gpu_.device(), SDL_CreateGPUSampler(gpu_.device(), &sampler)};
    SDL_GPUSamplerCreateInfo shadow{};
    shadow.min_filter = SDL_GPU_FILTER_LINEAR;
    shadow.mag_filter = SDL_GPU_FILTER_LINEAR;
    shadow.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadow.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadow.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadow.enable_compare = true;
    shadow.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    shadow_sampler_ = SamplerHandle{gpu_.device(), SDL_CreateGPUSampler(gpu_.device(), &shadow)};

    // Neutral textures for slots a material leaves empty.
    const auto solid = [&](std::uint8_t r, std::uint8_t g, std::uint8_t b, SDL_GPUTextureFormat format) {
        auto handle = gpu_.texture(format, SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, 1);
        const std::uint8_t px[4] = {r, g, b, 255};
        gpu_.upload(handle.get(), px, 1, 1, 4, false);
        return handle;
    };
    white_ = solid(255, 255, 255, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    black_ = solid(0, 0, 0, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    flat_normal_ = solid(128, 128, 255, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

    // The Planck table, from core/render/blackbody.hpp: 32-bit floats, because
    // the alpha channel is ln(eta) and runs from -17000 to -2.
    render::PlanckTable fallback{};
    const render::PlanckTable* table = planck_table;
    if (table == nullptr) {
        fallback = render::build_planck_table(1024);
        table = &fallback;
    }
    planck_ = gpu_.texture(SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                           static_cast<std::uint32_t>(table->width), 1);
    gpu_.upload(planck_.get(), table->texels.data(), static_cast<std::uint32_t>(table->width), 1, 16, false);
}

bool Renderer::create_pipelines(SDL_GPUTextureFormat output_format) {
    const auto make_shader = [&](const char* name, ShaderStage stage, ShaderResources resources) {
        auto key = std::string{name} + (stage == ShaderStage::Vertex ? ".vert" : ".frag");
        shaders_[key] = gpu_.shader(name, stage, resources);
        return shaders_[key].get();
    };
    SDL_GPUShader* star_vs = make_shader("star", ShaderStage::Vertex, {1, 1});
    SDL_GPUShader* star_fs = make_shader("star", ShaderStage::Fragment, {0, 1});
    SDL_GPUShader* body_vs = make_shader("body", ShaderStage::Vertex, {1, 1});
    SDL_GPUShader* body_fs = make_shader("body", ShaderStage::Fragment, {4, 1});
    SDL_GPUShader* lit_vs = make_shader("lit", ShaderStage::Vertex, {0, 1});
    SDL_GPUShader* lit_fs = make_shader("lit", ShaderStage::Fragment, {2, 1});
    SDL_GPUShader* unlit_vs = make_shader("unlit", ShaderStage::Vertex, {0, 1});
    SDL_GPUShader* unlit_fs = make_shader("unlit", ShaderStage::Fragment, {1, 1});
    SDL_GPUShader* plume_vs = make_shader("plume", ShaderStage::Vertex, {0, 1});
    SDL_GPUShader* plume_fs = make_shader("plume", ShaderStage::Fragment, {0, 1});
    SDL_GPUShader* shadow_vs = make_shader("shadow", ShaderStage::Vertex, {0, 1});
    SDL_GPUShader* shadow_fs = make_shader("shadow", ShaderStage::Fragment, {0, 0});
    SDL_GPUShader* composite_vs = make_shader("composite", ShaderStage::Vertex, {0, 0});
    SDL_GPUShader* composite_fs = make_shader("composite", ShaderStage::Fragment, {2, 1});
    for (const auto& [name, shader] : shaders_) {
        if (!shader) {
            SDL_Log("shader %s failed", name.c_str());
            return false;
        }
    }

    const SDL_GPUTextureFormat hdr = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    const SDL_GPUTextureFormat depth = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    near_samples_ = gpu_.supports_samples(hdr, SDL_GPU_SAMPLECOUNT_4) &&
                            gpu_.supports_samples(depth, SDL_GPU_SAMPLECOUNT_4)
                        ? SDL_GPU_SAMPLECOUNT_4
                        : SDL_GPU_SAMPLECOUNT_1;

    struct Spec {
        SDL_GPUShader* vs;
        SDL_GPUShader* fs;
        bool mesh_input;
        bool instanced_stars;
        SDL_GPUColorTargetBlendState blend;
        SDL_GPUTextureFormat colour;
        bool has_colour;
        bool depth_test;
        bool depth_write;
        SDL_GPUCullMode cull;
        SDL_GPUSampleCount samples;
        bool depth_bias;
    };
    const auto build = [&](const Spec& s) {
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = s.vs;
        info.fragment_shader = s.fs;
        SDL_GPUVertexBufferDescription buffer = mesh_buffer();
        SDL_GPUVertexAttribute attributes[4]{};
        mesh_attributes(attributes);
        if (s.mesh_input) {
            info.vertex_input_state.vertex_buffer_descriptions = &buffer;
            info.vertex_input_state.num_vertex_buffers = 1;
            info.vertex_input_state.vertex_attributes = attributes;
            info.vertex_input_state.num_vertex_attributes = 4;
        }
        SDL_GPUVertexBufferDescription star_buffer{};
        SDL_GPUVertexAttribute star_attributes[2]{};
        if (s.instanced_stars) {
            star_buffer.slot = 0;
            star_buffer.pitch = sizeof(app::StarVertex);
            star_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
            star_attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                  static_cast<Uint32>(offsetof(app::StarVertex, position))};
            star_attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                                  static_cast<Uint32>(offsetof(app::StarVertex, custom))};
            info.vertex_input_state.vertex_buffer_descriptions = &star_buffer;
            info.vertex_input_state.num_vertex_buffers = 1;
            info.vertex_input_state.vertex_attributes = star_attributes;
            info.vertex_input_state.num_vertex_attributes = 2;
        }
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.cull_mode = s.cull;
        // The meshes wind their front faces CLOCKWISE (scene/mesh.hpp).
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
        if (s.depth_bias) {
            info.rasterizer_state.enable_depth_bias = true;
            info.rasterizer_state.depth_bias_constant_factor = 2.0F;
            info.rasterizer_state.depth_bias_slope_factor = 2.5F;
        }
        info.multisample_state.sample_count = s.samples;
        SDL_GPUColorTargetDescription target{};
        target.format = s.colour;
        target.blend_state = s.blend;
        if (s.has_colour) {
            info.target_info.color_target_descriptions = &target;
            info.target_info.num_color_targets = 1;
        }
        info.target_info.has_depth_stencil_target = s.depth_test || s.depth_write;
        info.target_info.depth_stencil_format = depth;
        info.depth_stencil_state.enable_depth_test = s.depth_test;
        info.depth_stencil_state.enable_depth_write = s.depth_write;
        // Reversed depth everywhere except the shadow map, which is orthographic
        // and needs no such help.
        info.depth_stencil_state.compare_op =
            s.has_colour ? SDL_GPU_COMPAREOP_GREATER_OR_EQUAL : SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(gpu_.device(), &info);
        if (pipeline == nullptr) {
            SDL_Log("pipeline: %s", SDL_GetError());
        }
        return PipelineHandle{gpu_.device(), pipeline};
    };

    const auto one = SDL_GPU_SAMPLECOUNT_1;
    body_pipeline_ = build({body_vs, body_fs, true, false, opaque_blend(), hdr, true, true, true,
                            SDL_GPU_CULLMODE_BACK, one, false});
    star_pipeline_ = build({star_vs, star_fs, false, true, alpha_blend(), hdr, true, true, false,
                            SDL_GPU_CULLMODE_NONE, one, false});
    lit_cull_pipeline_ = build({lit_vs, lit_fs, true, false, opaque_blend(), hdr, true, true, true,
                                SDL_GPU_CULLMODE_BACK, near_samples_, false});
    lit_double_pipeline_ = build({lit_vs, lit_fs, true, false, opaque_blend(), hdr, true, true, true,
                                  SDL_GPU_CULLMODE_NONE, near_samples_, false});
    lit_alpha_pipeline_ = build({lit_vs, lit_fs, true, false, alpha_blend(), hdr, true, true, false,
                                 SDL_GPU_CULLMODE_NONE, near_samples_, false});
    unlit_opaque_pipeline_ = build({unlit_vs, unlit_fs, true, false, opaque_blend(), hdr, true, true, true,
                                    SDL_GPU_CULLMODE_BACK, near_samples_, false});
    unlit_alpha_pipeline_ = build({unlit_vs, unlit_fs, true, false, alpha_blend(), hdr, true, true, false,
                                   SDL_GPU_CULLMODE_BACK, near_samples_, false});
    additive_pipeline_ = build({unlit_vs, unlit_fs, true, false, additive_blend(), hdr, true, true, false,
                                SDL_GPU_CULLMODE_NONE, near_samples_, false});
    plume_pipeline_ = build({plume_vs, plume_fs, true, false, additive_blend(), hdr, true, true, false,
                             SDL_GPU_CULLMODE_NONE, near_samples_, false});
    shadow_pipeline_ = build({shadow_vs, shadow_fs, true, false, opaque_blend(), hdr, false, true, true,
                              SDL_GPU_CULLMODE_NONE, one, true});

    SDL_GPUGraphicsPipelineCreateInfo composite{};
    composite.vertex_shader = composite_vs;
    composite.fragment_shader = composite_fs;
    composite.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    SDL_GPUColorTargetDescription target{};
    target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    composite.target_info.color_target_descriptions = &target;
    composite.target_info.num_color_targets = 1;
    composite_pipeline_ = PipelineHandle{gpu_.device(), SDL_CreateGPUGraphicsPipeline(gpu_.device(), &composite)};
    (void)output_format;

    return body_pipeline_ && star_pipeline_ && lit_cull_pipeline_ && lit_double_pipeline_ && lit_alpha_pipeline_ &&
           unlit_opaque_pipeline_ && unlit_alpha_pipeline_ && additive_pipeline_ && plume_pipeline_ && shadow_pipeline_ &&
           composite_pipeline_;
}

void Renderer::ensure_targets(std::uint32_t width, std::uint32_t height) {
    if (width == width_ && height == height_ && frame_) {
        return;
    }
    width_ = width;
    height_ = height;
    const auto sampled_target = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    world_colour_ = gpu_.texture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, sampled_target, width, height);
    world_depth_ = gpu_.texture(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, width, height);
    near_colour_ = gpu_.texture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, sampled_target, width, height);
    if (near_samples_ != SDL_GPU_SAMPLECOUNT_1) {
        near_colour_msaa_ = gpu_.texture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                                         width, height, 1, near_samples_);
    }
    near_depth_ = gpu_.texture(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, width, height,
                               1, near_samples_);
    frame_ = gpu_.texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, sampled_target, width, height);
    if (!shadow_map_) {
        shadow_map_ = gpu_.texture(SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                                   SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                   SHADOW_SIZE, SHADOW_SIZE);
    }
}

const Renderer::GpuMesh* Renderer::mesh(const std::string& name) {
    if (const auto it = meshes_.find(name); it != meshes_.end()) {
        return &it->second;
    }
    const app::MeshData* data = library_ != nullptr ? library_->find(name) : nullptr;
    if (data == nullptr || data->indices.empty()) {
        return nullptr;
    }
    GpuMesh gpu_mesh{};
    const auto vertex_bytes = static_cast<std::uint32_t>(data->vertices.size() * sizeof(app::MeshVertex));
    const auto index_bytes = static_cast<std::uint32_t>(data->indices.size() * sizeof(std::uint32_t));
    gpu_mesh.vertices = gpu_.buffer(SDL_GPU_BUFFERUSAGE_VERTEX, vertex_bytes);
    gpu_mesh.indices = gpu_.buffer(SDL_GPU_BUFFERUSAGE_INDEX, index_bytes);
    gpu_.upload(gpu_mesh.vertices.get(), data->vertices.data(), vertex_bytes);
    gpu_.upload(gpu_mesh.indices.get(), data->indices.data(), index_bytes);
    gpu_mesh.index_count = static_cast<std::uint32_t>(data->indices.size());
    return &meshes_.emplace(name, std::move(gpu_mesh)).first->second;
}

SDL_GPUTexture* Renderer::texture(const std::string& spec, bool srgb) {
    if (spec.empty()) {
        return nullptr;
    }
    const std::string key = spec + (srgb ? "#srgb" : "#linear");
    if (const auto it = textures_.find(key); it != textures_.end()) {
        return it->second.get();
    }
    std::string procedural;
    const std::string path = first_existing(assets_, spec, procedural);
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    if (!path.empty()) {
        int channels = 0;
        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (data != nullptr) {
            pixels.assign(data, data + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
            stbi_image_free(data);
        } else {
            SDL_Log("cannot read %s: %s", path.c_str(), stbi_failure_reason());
        }
    } else if (!procedural.empty()) {
        // Placeholders until the real image exists (rule 81).
        auto image = app::planet_textures::generate(procedural);
        width = image.width;
        height = image.height;
        pixels = std::move(image.rgba);
    }
    if (pixels.empty()) {
        textures_.emplace(key, TextureHandle{});
        return nullptr;
    }
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    auto handle = gpu_.texture(srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                               SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, w, h, mip_levels(w, h));
    gpu_.upload(handle.get(), pixels.data(), w, h, 4, true);
    SDL_GPUTexture* raw = handle.get();
    textures_.emplace(key, std::move(handle));
    return raw;
}

SDL_GPUTexture* Renderer::dynamic_texture(const std::string& name) {
    const auto colon = name.find(':');
    if (colon == std::string::npos) {
        return nullptr;
    }
    const std::string kind = name.substr(0, colon);
    const auto index = static_cast<std::size_t>(std::stoul(name.substr(colon + 1)));
    auto& panels = kind == "display" ? displays_ : captions_;
    return index < panels.size() ? panels[index].texture.get() : nullptr;
}

// =============================================================================
//  per frame
// =============================================================================

void Renderer::build_panels(app::FlightApp& app, ImFont* font) {
    const bool inside = app.cockpit().visible && app.cockpit().displays_visible;
    for (auto& panel : displays_) {
        panel.active = inside;
        if (!inside) {
            continue;
        }
        panel.list->begin();
        ImGuiCanvas canvas(panel.list->list, font);
        app.draw_display(panel.name, canvas, app::Vec2{static_cast<double>(panel.width), static_cast<double>(panel.height)});
        panel.list->end();
    }
    const auto& controls = app.cockpit().controls();
    for (std::size_t i = 0; i < captions_.size() && i < controls.size(); ++i) {
        auto& panel = captions_[i];
        panel.active = inside && !controls[i].caption_text().empty();
        if (!panel.active) {
            continue;
        }
        panel.list->begin();
        ImGuiCanvas canvas(panel.list->list, font);
        // The label's size is the scene's: 96 label pixels at a pixel size of
        // half the face's height over 130, which is 0.369 of the face's height;
        // and the outline was 12 of those 96.
        const double px = 0.369 * static_cast<double>(panel.height);
        const std::string text = controls[i].caption_text();
        const double width = canvas.text_width(text, px);
        const double top = (static_cast<double>(panel.height) - canvas.line_height(px)) * 0.5;
        canvas.outlined_text(app::Vec2{(static_cast<double>(panel.width) - width) * 0.5, top}, text, px,
                             controls[i].caption_colour(), app::Colour{0.0F, 0.0F, 0.0F, 0.9F}, px / 8.0 * 0.5);
        panel.list->end();
    }
}

void Renderer::upload_stars(SDL_GPUCopyPass* pass, const app::StarSky& sky) {
    const auto stars = sky.vertices();
    star_count_ = static_cast<std::uint32_t>(stars.size());
    if (star_count_ == 0) {
        return;
    }
    const auto bytes = static_cast<std::uint32_t>(stars.size() * sizeof(app::StarVertex));
    if (star_capacity_ < bytes || !stars_) {
        star_capacity_ = bytes;
        stars_ = gpu_.buffer(SDL_GPU_BUFFERUSAGE_VERTEX, bytes);
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = bytes;
        stars_transfer_ = TransferHandle{gpu_.device(), SDL_CreateGPUTransferBuffer(gpu_.device(), &info)};
    }
    void* mapped = SDL_MapGPUTransferBuffer(gpu_.device(), stars_transfer_.get(), true);
    std::memcpy(mapped, stars.data(), bytes);
    SDL_UnmapGPUTransferBuffer(gpu_.device(), stars_transfer_.get());
    SDL_GPUTransferBufferLocation from{stars_transfer_.get(), 0};
    SDL_GPUBufferRegion to{stars_.get(), 0, bytes};
    SDL_UploadToGPUBuffer(pass, &from, &to, true);
}

void Renderer::upload_frame_data(SDL_GPUCommandBuffer* cmd, app::FlightApp& app, const ImDrawData* ui) {
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    upload_stars(pass, app.sky());
    for (auto& panel : displays_) {
        if (panel.active) {
            ui_.upload(pass, panel.list->data, "display:" + panel.name);
        }
    }
    for (std::size_t i = 0; i < captions_.size(); ++i) {
        if (captions_[i].active) {
            ui_.upload(pass, captions_[i].list->data, "caption:" + std::to_string(i));
        }
    }
    if (ui != nullptr) {
        ui_.upload(pass, *ui, "interface");
    }
    SDL_EndGPUCopyPass(pass);
}

void Renderer::render(SDL_GPUCommandBuffer* cmd, app::FlightApp& app, std::uint32_t width, std::uint32_t height,
                      const ImDrawData* ui) {
    ensure_targets(width, height);
    ui_.update_textures();
    upload_frame_data(cmd, app, ui);
    render_panels(cmd, app);

    std::vector<app::Part> storage;
    auto items = collect_near(app, storage);
    render_shadow(cmd, items);
    render_world(cmd, app);
    render_near(cmd, app, items);
    render_composite(cmd);
    render_ui(cmd, ui);
}

void Renderer::render_panels(SDL_GPUCommandBuffer* cmd, app::FlightApp& app) {
    (void)app;
    const auto paint = [&](Panel& panel, const std::string& stream, bool clear_black) {
        SDL_GPUColorTargetInfo target{};
        target.texture = panel.texture.get();
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        target.clear_color = clear_black ? SDL_FColor{0.0F, 0.0F, 0.0F, 1.0F} : SDL_FColor{0.0F, 0.0F, 0.0F, 0.0F};
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
        ui_.draw(pass, cmd, panel.list->data, stream, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, panel.width, panel.height);
        SDL_EndGPURenderPass(pass);
    };
    for (auto& panel : displays_) {
        if (panel.active) {
            paint(panel, "display:" + panel.name, true);
        }
    }
    for (std::size_t i = 0; i < captions_.size(); ++i) {
        if (captions_[i].active) {
            paint(captions_[i], "caption:" + std::to_string(i), false);
        }
    }
}

std::vector<Renderer::DrawItem> Renderer::collect_near(app::FlightApp& app, std::vector<app::Part>& storage) {
    // Every part of the near field, placed in the near world: the body frame
    // turned by the ship's attitude. The hull points where the attitude says.
    const Transform3 ship{app.ship_basis(), Vec3{}};
    const Vec3 eye = app.camera().near_camera().position;
    std::vector<DrawItem> items;
    storage.clear();
    storage.reserve(512);

    const auto add = [&](const app::Part& part, const Transform3& parent) {
        if (!part.visible) {
            return;
        }
        storage.push_back(part);
        const Transform3 model = parent * part.transform;
        items.push_back(DrawItem{&storage.back(), model, (model.origin - eye).norm()});
    };
    for (const auto& part : app.spacecraft().parts()) {
        add(part, ship);
    }
    const Transform3 mount = ship * app::SpacecraftVisual::engine_mount();
    for (const auto& part : app.plume().parts()) {
        add(part, mount);
    }
    for (const auto& part : app.rcs_visual().parts()) {
        add(part, ship);
    }
    if (app.cockpit().visible) {
        for (const auto& part : app.cockpit().parts()) {
            add(part, ship);
        }
        for (const auto& part : app.cockpit().control_parts()) {
            add(part, ship);
        }
    }
    // `storage` may have reallocated while it grew: re-point.
    for (std::size_t i = 0; i < items.size(); ++i) {
        items[i].part = &storage[i];
    }
    return items;
}

void Renderer::render_shadow(SDL_GPUCommandBuffer* cmd, const std::vector<DrawItem>& items) {
    // The Sun's view of the ship: orthographic, centred on the centre of mass.
    const Vec3 L = sun_direction_.normalized();
    Vec3 up{0.0, 0.0, 1.0};
    if (std::abs(dot(up, L)) > 0.95) {
        up = Vec3{0.0, 1.0, 0.0};
    }
    const Vec3 x = cross(up, L).normalized();
    const Vec3 y = cross(L, x);
    // Light space: (x, y) across, depth = distance from a plane kShadowDepth
    // sunward, over twice that.
    Mat4 light{};
    const double s = 1.0 / kShadowExtent;
    light.m[0] = static_cast<float>(x.x * s);
    light.m[4] = static_cast<float>(x.y * s);
    light.m[8] = static_cast<float>(x.z * s);
    light.m[1] = static_cast<float>(y.x * s);
    light.m[5] = static_cast<float>(y.y * s);
    light.m[9] = static_cast<float>(y.z * s);
    const double d = -1.0 / (2.0 * kShadowDepth);
    light.m[2] = static_cast<float>(L.x * d);
    light.m[6] = static_cast<float>(L.y * d);
    light.m[10] = static_cast<float>(L.z * d);
    light.m[14] = 0.5F;
    light.m[15] = 1.0F;

    // The same, into texture space for the lit shader: u right, v down.
    Mat4 bias = Mat4::identity();
    bias.m[0] = 0.5F;
    bias.m[5] = -0.5F;
    bias.m[12] = 0.5F;
    bias.m[13] = 0.5F;
    const Mat4 shadow = bias * light;
    std::memcpy(shadow_matrix_, shadow.m, sizeof shadow_matrix_);

    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = shadow_map_.get();
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.clear_depth = 1.0F;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depth);
    SDL_BindGPUGraphicsPipeline(pass, shadow_pipeline_.get());
    for (const auto& item : items) {
        const auto& m = item.part->material;
        if (!m.casts_shadow || m.blend != app::Blend::Opaque || m.unshaded) {
            continue;
        }
        const GpuMesh* gm = mesh(item.part->mesh);
        if (gm == nullptr) {
            continue;
        }
        const Mat4 mvp = light * Mat4::from(item.model);
        SDL_PushGPUVertexUniformData(cmd, 0, &mvp, sizeof mvp);
        SDL_GPUBufferBinding vb{gm->vertices.get(), 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_GPUBufferBinding ib{gm->indices.get(), 0};
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, gm->index_count, 1, 0, 0, 0);
    }
    SDL_EndGPURenderPass(pass);
}

void Renderer::render_world(SDL_GPUCommandBuffer* cmd, app::FlightApp& app) {
    const auto& camera = app.camera().world_camera();
    const double aspect = static_cast<double>(width_) / static_cast<double>(height_);
    const Mat4 projection = perspective(camera.fov_deg, aspect, camera.near, camera.far);
    // Camera-relative: the view is a rotation only, and every position is the
    // body's minus the camera's, subtracted in double.
    const Mat4 rotation = view_of(camera.basis, Vec3{});
    const Mat4 view_projection = projection * rotation;
    sun_direction_ = app.celestial().sun_direction();

    SDL_GPUColorTargetInfo colour{};
    colour.texture = world_colour_.get();
    colour.load_op = SDL_GPU_LOADOP_CLEAR;
    colour.store_op = SDL_GPU_STOREOP_STORE;
    colour.clear_color = SDL_FColor{background[0], background[1], background[2], 1.0F};
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = world_depth_.get();
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.clear_depth = 0.0F;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colour, 1, &depth);

    // --- bodies ---
    const GpuMesh* sphere = mesh("body:sphere");
    if (sphere != nullptr) {
        SDL_BindGPUGraphicsPipeline(pass, body_pipeline_.get());
        SDL_GPUBufferBinding vb{sphere->vertices.get(), 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_GPUBufferBinding ib{sphere->indices.get(), 0};
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_GPUTextureSamplerBinding planck{planck_.get(), nearest_sampler_.get()};
        SDL_BindGPUVertexSamplers(pass, 0, &planck, 1);
        const double reference = app.sky().planck_table_reference_temperature();
        for (const auto& body : app.bodies()) {
            if (!body.visible) {
                continue;
            }
            BodyVertexBlock vertex{};
            vertex.model = Mat4::from(Transform3{body.mesh_basis, body.position - camera.position});
            vertex.view_projection = view_projection;
            vertex.velocity_c = v4(body.relative_velocity_scene, body.light_speed_scene);
            vertex.optics = Vec4f{static_cast<float>(body.doppler), static_cast<float>(body.beaming_doppler),
                                  static_cast<float>(body.half_saturation), static_cast<float>(reference)};
            const auto reflectance = body.reflectance.to_linear();
            vertex.reflectance = Vec4f{reflectance.r, reflectance.g, reflectance.b, body.self_luminous ? 1.0F : 0.0F};
            vertex.flags = Vec4f{body.apply_light_time ? 1.0F : 0.0F,
                                 static_cast<float>(body.self_luminous ? app::CelestialView::SUN_TEMPERATURE : 5772.0),
                                 0.0F, 0.0F};
            SDL_PushGPUVertexUniformData(cmd, 0, &vertex, sizeof vertex);

            SDL_GPUTexture* albedo = texture(body.surface.albedo_map, true);
            SDL_GPUTexture* clouds = texture(body.surface.cloud_map, false);
            SDL_GPUTexture* night = texture(body.surface.night_map, true);
            SDL_GPUTexture* normal = texture(body.surface.normal_map, false);
            BodyFragmentBlock fragment{};
            fragment.sun_direction = v4(body.sun_direction_scene, body.self_luminous ? 1.0 : 0.0);
            fragment.sun_light = linear(kSunColour, kWorldSunEnergy);
            fragment.ambient = linear(kWorldAmbient, kWorldAmbientEnergy);
            fragment.maps = Vec4f{albedo != nullptr ? 1.0F : 0.0F, clouds != nullptr ? 1.0F : 0.0F,
                                  night != nullptr ? 1.0F : 0.0F, normal != nullptr ? 1.0F : 0.0F};
            fragment.clouds = Vec4f{static_cast<float>(body.cloud_offset), 0.16F, 0.60F, 0.88F};
            const auto atmosphere = body.surface.atmosphere_colour.to_linear();
            fragment.atmosphere_colour =
                Vec4f{atmosphere.r, atmosphere.g, atmosphere.b, static_cast<float>(body.surface.atmosphere_strength)};
            fragment.surface = Vec4f{static_cast<float>(body.surface.atmosphere_haze), 0.75F, 1.0F, 0.0F};
            fragment.reflectance = Vec4f{reflectance.r, reflectance.g, reflectance.b, 1.0F};
            SDL_PushGPUFragmentUniformData(cmd, 0, &fragment, sizeof fragment);

            SDL_GPUTextureSamplerBinding maps[4] = {
                {albedo != nullptr ? albedo : white_.get(), repeat_sampler_.get()},
                {clouds != nullptr ? clouds : black_.get(), repeat_sampler_.get()},
                {night != nullptr ? night : black_.get(), repeat_sampler_.get()},
                {normal != nullptr ? normal : flat_normal_.get(), repeat_sampler_.get()},
            };
            SDL_BindGPUFragmentSamplers(pass, 0, maps, 4);
            SDL_DrawGPUIndexedPrimitives(pass, sphere->index_count, 1, 0, 0, 0);
        }
    }

    // --- stars, after the bodies: the Sun hides the sphere behind it ---
    const Mat4 star_view_projection = projection * view_of(camera.basis, camera.position);
    draw_stars(cmd, pass, star_view_projection.m, app.sky());
    SDL_EndGPURenderPass(pass);
}

void Renderer::draw_stars(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const float* view_projection,
                          const app::StarSky& sky) {
    if (star_count_ == 0) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, star_pipeline_.get());
    StarVertexBlock block{};
    std::memcpy(block.view_projection.m, view_projection, sizeof block.view_projection.m);
    block.viewport = Vec4f{static_cast<float>(width_), static_cast<float>(height_), 1.0F / static_cast<float>(width_),
                           1.0F / static_cast<float>(height_)};
    block.params = Vec4f{static_cast<float>(sky.planck_table_reference_temperature()),
                         static_cast<float>(sky.half_saturation()), static_cast<float>(starfield.base_point_size),
                         static_cast<float>(starfield.max_point_size)};
    block.debug = Vec4f{starfield.debug ? 1.0F : 0.0F, starfield.flat_sprite ? 1.0F : 0.0F,
                        static_cast<float>(starfield.debug_point_size), 0.0F};
    SDL_PushGPUVertexUniformData(cmd, 0, &block, sizeof block);
    SDL_PushGPUFragmentUniformData(cmd, 0, &block.debug, sizeof block.debug);
    SDL_GPUTextureSamplerBinding planck{planck_.get(), nearest_sampler_.get()};
    SDL_BindGPUVertexSamplers(pass, 0, &planck, 1);
    SDL_GPUBufferBinding vb{stars_.get(), 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_DrawGPUPrimitives(pass, 6, star_count_, 0, 0);
}

bool Renderer::initialise_sky(const render::PlanckTable* table, SDL_GPUTextureFormat output_format) {
    output_format_ = output_format;
    if (!create_pipelines(output_format)) {
        return false;
    }
    create_shared(table);
    return true;
}

void Renderer::render_sky(SDL_GPUCommandBuffer* cmd, const app::StarSky& sky, const SkyView& view,
                          std::uint32_t width, std::uint32_t height) {
    ensure_targets(width, height);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    upload_stars(copy, sky);
    SDL_EndGPUCopyPass(copy);

    const double aspect = static_cast<double>(width_) / static_cast<double>(height_);
    SDL_GPUColorTargetInfo colour{};
    colour.texture = world_colour_.get();
    colour.load_op = SDL_GPU_LOADOP_CLEAR;
    colour.store_op = SDL_GPU_STOREOP_STORE;
    // Black and nothing else: no background tint between the shader's output and
    // the pixel the harness measures.
    colour.clear_color = SDL_FColor{0.0F, 0.0F, 0.0F, 1.0F};
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = world_depth_.get();
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.clear_depth = 0.0F;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colour, 1, &depth);
    const Mat4 view_projection = perspective(view.fov_deg, aspect, view.near, view.far) * view_of(view.basis, Vec3{});
    draw_stars(cmd, pass, view_projection.m, sky);
    SDL_EndGPURenderPass(pass);

    // The near layer cleared to nothing, so the composite is the world alone.
    SDL_GPUColorTargetInfo near{};
    near.texture = near_colour_.get();
    near.load_op = SDL_GPU_LOADOP_CLEAR;
    near.store_op = SDL_GPU_STOREOP_STORE;
    near.clear_color = SDL_FColor{0.0F, 0.0F, 0.0F, 0.0F};
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &near, 1, nullptr));
    const bool saved = draw_near_field;
    draw_near_field = false;
    render_composite(cmd);
    draw_near_field = saved;
}

void Renderer::render_near(SDL_GPUCommandBuffer* cmd, app::FlightApp& app, std::vector<DrawItem>& items) {
    const auto& camera = app.camera().near_camera();
    const double aspect = static_cast<double>(width_) / static_cast<double>(height_);
    const Mat4 view_projection =
        perspective(camera.fov_deg, aspect, camera.near, camera.far) * view_of(camera.basis, camera.position);

    SDL_GPUColorTargetInfo colour{};
    if (near_samples_ != SDL_GPU_SAMPLECOUNT_1) {
        colour.texture = near_colour_msaa_.get();
        colour.resolve_texture = near_colour_.get();
        colour.store_op = SDL_GPU_STOREOP_RESOLVE;
    } else {
        colour.texture = near_colour_.get();
        colour.store_op = SDL_GPU_STOREOP_STORE;
    }
    colour.load_op = SDL_GPU_LOADOP_CLEAR;
    // Transparent: where there is no geometry the world shows through. That is
    // what makes a window a window, without cutting anything out.
    colour.clear_color = SDL_FColor{0.0F, 0.0F, 0.0F, 0.0F};
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = near_depth_.get();
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.clear_depth = 0.0F;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colour, 1, &depth);
    if (!draw_near_field) {
        SDL_EndGPURenderPass(pass);
        return;
    }

    // The lights, in the near world.
    const Transform3 ship{app.ship_basis(), Vec3{}};
    std::vector<app::PointLight> lights;
    if (app.cockpit().visible) {
        // The cabin's lights are part of the cabin: hidden with it, as the
        // scene's were.
        for (const auto& light : app.cockpit().lights()) {
            lights.push_back(light);
        }
    }
    const auto plume_light = app.plume().light();
    if (plume_light.energy > 0.0) {
        auto placed = plume_light;
        placed.position = app::SpacecraftVisual::engine_mount() * plume_light.position;
        lights.push_back(placed);
    }

    LitFragmentBlock lit{};
    lit.camera_position = v4(camera.position);
    lit.sun_direction = v4(sun_direction_.normalized());
    lit.sun_light = linear(kSunColour, kNearSunEnergy);
    lit.ambient = linear(kNearAmbient, kNearAmbientEnergy);
    std::memcpy(lit.shadow_matrix.m, shadow_matrix_, sizeof shadow_matrix_);
    lit.shadow_params = Vec4f{1.0F, 1.0F / static_cast<float>(SHADOW_SIZE), 0.0015F, 0.0F};
    int count = 0;
    for (const auto& light : lights) {
        if (count >= 4) {
            break;
        }
        lit.point_position[count] = v4(ship * light.position, light.range);
        lit.point_colour[count] = linear(light.colour, light.energy);
        ++count;
    }
    lit.point_count = Vec4f{static_cast<float>(count), 0.0F, 0.0F, 0.0F};

    const auto draw_lit = [&](const DrawItem& item, SDL_GPUGraphicsPipeline* pipeline) {
        const auto& m = item.part->material;
        const GpuMesh* gm = mesh(item.part->mesh);
        if (gm == nullptr) {
            return;
        }
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        LitVertexBlock vertex{};
        vertex.model = Mat4::from(item.model);
        vertex.view_projection = view_projection;
        vertex.normal_matrix = Mat4::from(Transform3{item.model.basis.inverse().transposed(), Vec3{}});
        vertex.uv_scale = Vec4f{static_cast<float>(m.uv_scale.x), static_cast<float>(m.uv_scale.y), 0.0F, 0.0F};
        SDL_PushGPUVertexUniformData(cmd, 0, &vertex, sizeof vertex);
        SDL_GPUTexture* albedo = texture(m.albedo_texture, true);
        LitFragmentBlock fragment = lit;
        fragment.albedo = linear(m.albedo);
        fragment.emission = linear(m.emission, m.emission_energy);
        fragment.material = Vec4f{static_cast<float>(m.roughness), static_cast<float>(m.metallic),
                                  albedo != nullptr ? 1.0F : 0.0F, m.unshaded ? 1.0F : 0.0F};
        SDL_PushGPUFragmentUniformData(cmd, 0, &fragment, sizeof fragment);
        SDL_GPUTextureSamplerBinding samplers[2] = {
            {albedo != nullptr ? albedo : white_.get(), repeat_sampler_.get()},
            {shadow_map_.get(), shadow_sampler_.get()},
        };
        SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);
        SDL_GPUBufferBinding vb{gm->vertices.get(), 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_GPUBufferBinding ib{gm->indices.get(), 0};
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, gm->index_count, 1, 0, 0, 0);
    };
    const auto draw_unlit = [&](const DrawItem& item, SDL_GPUGraphicsPipeline* pipeline) {
        const auto& m = item.part->material;
        const GpuMesh* gm = mesh(item.part->mesh);
        if (gm == nullptr) {
            return;
        }
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        const Mat4 mvp = view_projection * Mat4::from(item.model);
        SDL_PushGPUVertexUniformData(cmd, 0, &mvp, sizeof mvp);
        SDL_GPUTexture* image = m.dynamic_texture.empty() ? texture(m.albedo_texture, true)
                                                          : dynamic_texture(m.dynamic_texture);
        UnlitFragmentBlock fragment{};
        fragment.colour = m.dynamic_texture.empty() ? linear(m.albedo) : Vec4f{1.0F, 1.0F, 1.0F, 1.0F};
        fragment.flags = Vec4f{image != nullptr ? 1.0F : 0.0F, m.dynamic_texture.empty() ? 0.0F : 1.0F, 0.0F, 0.0F};
        SDL_PushGPUFragmentUniformData(cmd, 0, &fragment, sizeof fragment);
        SDL_GPUTextureSamplerBinding sampler{image != nullptr ? image : white_.get(), clamp_sampler_.get()};
        SDL_BindGPUFragmentSamplers(pass, 0, &sampler, 1);
        SDL_GPUBufferBinding vb{gm->vertices.get(), 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_GPUBufferBinding ib{gm->indices.get(), 0};
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, gm->index_count, 1, 0, 0, 0);
    };

    const auto draw_plume = [&](const DrawItem& item) {
        const auto& m = item.part->material;
        const GpuMesh* gm = mesh(item.part->mesh);
        if (gm == nullptr) {
            return;
        }
        SDL_BindGPUGraphicsPipeline(pass, plume_pipeline_.get());
        LitVertexBlock vertex{};
        vertex.model = Mat4::from(item.model);
        vertex.view_projection = view_projection;
        vertex.normal_matrix = Mat4::from(Transform3{item.model.basis.inverse().transposed(), Vec3{}});
        SDL_PushGPUVertexUniformData(cmd, 0, &vertex, sizeof vertex);
        const auto& look = m.plume;
        PlumeFragmentBlock fragment{};
        fragment.camera_position = v4(camera.position);
        fragment.near_colour = linear(look.near, look.energy);
        fragment.near_colour.w = static_cast<float>(look.edge_power);
        fragment.far_colour = linear(look.far, look.energy);
        fragment.far_colour.w = static_cast<float>(look.decay);
        fragment.flow = Vec4f{static_cast<float>(look.time), static_cast<float>(look.turbulence),
                              static_cast<float>(look.streaks), static_cast<float>(look.flow_speed)};
        fragment.shape = Vec4f{static_cast<float>(look.tail_fade), 0.0F, 0.0F, 0.0F};
        SDL_PushGPUFragmentUniformData(cmd, 0, &fragment, sizeof fragment);
        SDL_GPUBufferBinding vb{gm->vertices.get(), 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_GPUBufferBinding ib{gm->indices.get(), 0};
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, gm->index_count, 1, 0, 0, 0);
    };

    // Opaque first, then the transparent ones from back to front, then light.
    std::vector<const DrawItem*> transparent;
    std::vector<const DrawItem*> additive;
    for (const auto& item : items) {
        const auto& m = item.part->material;
        if (m.blend == app::Blend::Additive || m.blend == app::Blend::Plume) {
            additive.push_back(&item);
        } else if (m.blend == app::Blend::Alpha) {
            transparent.push_back(&item);
        } else if (m.unshaded) {
            draw_unlit(item, unlit_opaque_pipeline_.get());
        } else {
            draw_lit(item, m.cull == app::Cull::None ? lit_double_pipeline_.get() : lit_cull_pipeline_.get());
        }
    }
    std::sort(transparent.begin(), transparent.end(),
              [](const DrawItem* a, const DrawItem* b) { return a->distance > b->distance; });
    for (const auto* item : transparent) {
        if (item->part->material.unshaded) {
            draw_unlit(*item, unlit_alpha_pipeline_.get());
        } else {
            draw_lit(*item, lit_alpha_pipeline_.get());
        }
    }
    for (const auto* item : additive) {
        if (item->part->material.blend == app::Blend::Plume) {
            draw_plume(*item);
        } else {
            draw_unlit(*item, additive_pipeline_.get());
        }
    }
    SDL_EndGPURenderPass(pass);
}

void Renderer::render_composite(SDL_GPUCommandBuffer* cmd) {
    SDL_GPUColorTargetInfo target{};
    target.texture = frame_.get();
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, composite_pipeline_.get());
    const Vec4f params{draw_near_field ? 1.0F : 0.0F, 1.0F, 1.0F, 0.0F};
    SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof params);
    SDL_GPUTextureSamplerBinding samplers[2] = {
        {world_colour_.get(), nearest_sampler_.get()},
        {near_colour_.get(), nearest_sampler_.get()},
    };
    SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void Renderer::render_ui(SDL_GPUCommandBuffer* cmd, const ImDrawData* ui) {
    if (ui == nullptr || ui->TotalVtxCount == 0) {
        return;
    }
    SDL_GPUColorTargetInfo target{};
    target.texture = frame_.get();
    target.load_op = SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    ui_.draw(pass, cmd, *ui, "interface", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, width_, height_);
    SDL_EndGPURenderPass(pass);
}

}  // namespace sf::gfx
