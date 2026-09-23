#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;

layout(set = 2, binding = 0) uniform sampler2D image;

layout(set = 3, binding = 0) uniform UnlitFragment {
    vec4 colour;   // linear rgb, alpha
    vec4 flags;    // use image, image holds sRGB-encoded values
};

void main() {
    vec4 c = colour;
    if (flags.x > 0.5) {
        vec4 texel = texture(image, v_uv);
        if (flags.y > 0.5) {
            // The displays and the labels are painted by the 2-D pass, in the
            // sRGB values the palette is written in; light arithmetic needs them
            // linear.
            texel.rgb = srgb_to_linear(texel.rgb);
        }
        c *= texel;
    }
    result = c;
}
