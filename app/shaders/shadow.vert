#version 450
// Depth from the Sun, for the near field's self-shadowing: a structure in space
// that shadows itself is half of what makes it look solid.
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(set = 1, binding = 0) uniform ShadowVertex {
    mat4 light_model_view_projection;
};

void main() {
    gl_Position = light_model_view_projection * vec4(in_position, 1.0);
}
