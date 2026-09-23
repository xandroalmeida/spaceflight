#version 450
layout(location = 0) in vec3 v_colour;
layout(location = 1) in vec2 v_coord;
layout(location = 0) out vec4 result;

layout(set = 3, binding = 0) uniform StarFragment {
    vec4 debug;   // starfield_debug, flat_sprite
};

void main() {
    // A sprite is square; round it off, and let the brightness fall towards the
    // edge so a star reads as a point source rather than a tile. The flat mode
    // switches the SHAPING off and leaves the photometry alone, so that a
    // measured pixel is the response and nothing else.
    vec2 offset = v_coord - vec2(0.5);
    float radial = clamp(1.0 - 4.0 * dot(offset, offset), 0.0, 1.0);
    result = vec4(v_colour, debug.y > 0.5 ? 1.0 : radial);
}
