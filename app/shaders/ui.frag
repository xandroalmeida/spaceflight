#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_colour;
layout(location = 0) out vec4 result;

layout(set = 2, binding = 0) uniform sampler2D image;

void main() {
    // Colours stay in the sRGB values the palette is written in: the 2-D pass
    // draws on top of the encoded image, as a 2-D canvas does.
    result = v_colour * texture(image, v_uv);
}
