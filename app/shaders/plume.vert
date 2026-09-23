#version 450
// The exhaust: the lit stage's transforms, plus the mesh's own coordinates, which
// is where the fragment stage reads how far along the jet it is.

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec3 v_local;

layout(set = 1, binding = 0) uniform PlumeVertex {
    mat4 model;
    mat4 view_projection;
    mat4 normal_matrix;   // inverse-transpose of the model's 3x3, in the upper left
    vec4 unused;
};

void main() {
    vec4 world = model * vec4(in_position, 1.0);
    v_world = world.xyz;
    v_normal = normalize(mat3(normal_matrix) * in_normal);
    v_local = in_position;
    gl_Position = view_projection * world;
}
