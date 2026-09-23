// Shared by every shader of the renderer (ADR-0009). GLSL 4.50, SDL_GPU binding
// conventions: vertex samplers set 0, vertex uniforms set 1, fragment samplers
// set 2, fragment uniforms set 3.

// The Planck table: rgb is the linear-sRGB chromaticity of a black body, alpha is
// ln(band efficiency). Built by core/render/blackbody.hpp, never by hand.
//
// Sampled with texelFetch and the interpolation written out, rather than with a
// filtering sampler: linear filtering of a 32-bit float texture is optional on
// Vulkan (and missing on some of the hardware this runs on), and this is the
// SAME arithmetic core/render/blackbody.cpp's fetch() does in double, so a test
// that compares the GPU with the CPU compares the pipeline and not two
// interpolators.
float planck_index(float temperature, float reference) {
    return temperature / (temperature + reference);
}

vec4 planck_sample(sampler2D table, float temperature, float reference) {
    int width = textureSize(table, 0).x;
    float position = planck_index(temperature, reference) * float(width) - 0.5;
    float floor_position = floor(position);
    float fraction = position - floor_position;
    int last = width - 1;
    int i0 = clamp(int(floor_position), 0, last);
    int i1 = clamp(i0 + 1, 0, last);
    vec4 a = texelFetch(table, ivec2(i0, 0), 0);
    vec4 b = texelFetch(table, ivec2(i1, 0), 0);
    return a + (b - a) * fraction;
}

// IEC 61966-2-1.
vec3 srgb_to_linear(vec3 c) {
    vec3 low = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, vec3(lessThanEqual(c, vec3(0.04045))));
}

vec3 linear_to_srgb(vec3 c) {
    vec3 low = c * 12.92;
    vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, vec3(lessThanEqual(c, vec3(0.0031308))));
}

const float PI = 3.14159265358979;
