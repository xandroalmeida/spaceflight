#version 450
#extension GL_GOOGLE_include_directive : require
// Colour and brightness of a star from its Doppler factor.
//
// This shader contains NO relativity. D arrives per star, already computed by
// core/relativity/optics.hpp on the CPU, and the two lines that use it are what
// D MEANS rather than how it was obtained:
//
//   T' = D T      docs/physics/relativistic-rendering.md section 4
//   I' = D^4 I    section 5, corrected to the visible band by section 10.1
//
// It does not know what beta is, and it must not learn.
// See docs/architecture/relativistic-shaders.md sections 2 and 3.
//
// One instance per star, drawn as a screen-aligned quad of POINT_SIZE pixels:
// point primitives have no size on Direct3D and a maximum of one pixel on some
// Vulkan drivers, and a star field whose appearance depended on the API would be
// a star field no test could pin.
#include "common.glsl"

layout(location = 0) in vec3 in_position;   // aberrated direction x sky radius
layout(location = 1) in vec4 in_custom;     // T_rest, F_rest, D_colour, D_beam

layout(location = 0) out vec3 v_colour;
layout(location = 1) out vec2 v_coord;

layout(set = 0, binding = 0) uniform sampler2D planck_table;

layout(set = 1, binding = 0) uniform StarUniforms {
    mat4 view_projection;
    vec4 viewport;      // width, height, 1/width, 1/height (pixels of the target)
    vec4 params;        // table reference temperature, half saturation, base point size, max point size
    vec4 debug;         // starfield_debug, flat_sprite, debug point size, unused
};

const vec2 CORNERS[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                               vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

void main() {
    float rest_temperature = in_custom.x;
    float rest_flux = in_custom.y;
    float doppler = in_custom.z;
    float beaming = in_custom.w;
    float point_size;

    if (debug.x > 0.5) {
        // STARFIELD_DEBUG (docs/validation/starfield-debug.md section 3): every
        // star at one size in one white. It answers exactly one question -- is
        // the GEOMETRY reaching the framebuffer -- without any of the photometry
        // being able to hide the answer by returning zero.
        v_colour = vec3(1.0);
        point_size = debug.z;
    } else {
        float shifted_temperature = rest_temperature * doppler;
        vec4 rest = planck_sample(planck_table, rest_temperature, params.x);
        vec4 shifted = planck_sample(planck_table, shifted_temperature, params.x);

        // eta(DT)/eta(T) as a difference of logarithms. The catalogue's V
        // magnitude is a flux ALREADY IN THE BAND, so the bolometric D^4 has to
        // be corrected by how much of the shifted spectrum is still in it -- the
        // whole reason the forward sky is 51x and not 400x at beta = 0.9048.
        float band = exp(shifted.a - rest.a);
        float luminance = rest_flux * pow(beaming, 4.0) * band;

        // The structural limit: a bijection [0, inf) -> [0, 1). Nothing is cut
        // off, the scale is compressed, and the ordering survives. When
        // `luminance` underflows to zero -- the aft sky at beta = 0.99 -- the
        // answer is zero, and that is the physics: there are no photons in the
        // band.
        float response = luminance / (luminance + params.y);
        v_colour = shifted.rgb * response;
        // Brighter stars spread further on any real detector, so the size tracks
        // the same response rather than being a second, independent fudge.
        point_size = clamp(params.z * (0.5 + response), 1.0, params.w);
    }
    if (debug.y > 0.5) {
        point_size = debug.z;
    }

    vec2 corner = CORNERS[gl_VertexIndex % 6];
    vec4 clip = view_projection * vec4(in_position, 1.0);
    clip.xy += corner * point_size * viewport.zw * clip.w;
    gl_Position = clip;
    v_coord = corner * 0.5 + 0.5;
}
