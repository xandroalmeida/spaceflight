#version 450
// Things that are not lit: flames (additive), the displays and the labels.
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec2 v_uv;

layout(set = 1, binding = 0) uniform UnlitVertex {
    mat4 model_view_projection;
};

void main() {
    v_uv = in_uv;
    gl_Position = model_view_projection * vec4(in_position, 1.0);
}
