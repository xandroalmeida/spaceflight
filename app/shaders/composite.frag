#version 450
#extension GL_GOOGLE_include_directive : require
// The two worlds, joined.
//
// The far world is linear light with no tone curve: the star photometry is
// calibrated in it (a star's pixel IS its response), and anything else would
// break what tests/gpu measures. The near field -- the ship and the cockpit --
// goes through a filmic curve at a FIXED exposure (rule 51), so that the lit
// panel and the sunlit exterior coexist without either one governing the other.
//
// The near layer is premultiplied: where there is no geometry its alpha is zero
// and the world shows through, which is what makes a window a window. Additive
// light (the plume, the RCS jets) carries colour with no alpha, and adds.
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;

layout(set = 2, binding = 0) uniform sampler2D world_colour;
layout(set = 2, binding = 1) uniform sampler2D near_colour;

layout(set = 3, binding = 0) uniform CompositeFragment {
    vec4 params;   // near layer present, white point, exposure
};

// The filmic curve of the scene's near field, exposure bias folded into the
// constants, normalised so that `white` maps to one.
vec3 filmic(vec3 colour, float white) {
    const float A = 0.22 * 4.0;
    const float B = 0.30 * 2.0;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.01;
    const float F = 0.30;
    vec3 mapped = ((colour * (A * colour + C * B) + D * E) / (colour * (A * colour + B) + D * F)) - E / F;
    float w = ((white * (A * white + C * B) + D * E) / (white * (A * white + B) + D * F)) - E / F;
    return mapped / w;
}

void main() {
    vec3 world = clamp(texture(world_colour, v_uv).rgb, 0.0, 1.0);
    vec3 colour = world;
    if (params.x > 0.5) {
        vec4 near = texture(near_colour, v_uv);
        colour = clamp(filmic(max(near.rgb * params.z, vec3(0.0)), params.y), 0.0, 1.0) +
                 (1.0 - clamp(near.a, 0.0, 1.0)) * world;
    }
    result = vec4(linear_to_srgb(clamp(colour, 0.0, 1.0)), 1.0);
}
