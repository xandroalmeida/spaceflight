#version 450
// The near field -- the hull, the cockpit, the panel -- in METRES, in the frame
// whose axes are the integration frame's and whose origin is the ship's centre
// of mass (camera_rig.hpp explains why this is a second world).

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;

layout(set = 1, binding = 0) uniform LitVertex {
    mat4 model;
    mat4 view_projection;
    mat4 normal_matrix;   // inverse-transpose of the model's 3x3, in the upper left
    vec4 uv_scale;        // xy
};

void main() {
    vec4 world = model * vec4(in_position, 1.0);
    v_world = world.xyz;
    v_normal = normalize(mat3(normal_matrix) * in_normal);
    v_uv = in_uv * uv_scale.xy;
    gl_Position = view_projection * world;
}
