#version 450
// The 2-D pass: Dear ImGui's vertices -- position, texture coordinate, colour.
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_colour;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_colour;

layout(set = 1, binding = 0) uniform UiVertex {
    vec4 transform;   // scale xy, translate xy (to clip space)
};

void main() {
    v_uv = in_uv;
    v_colour = in_colour;
    gl_Position = vec4(in_position * transform.xy + transform.zw, 0.0, 1.0);
}
