#version 450
// One triangle over the whole target.
layout(location = 0) out vec2 v_uv;

void main() {
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_uv = corner;
    gl_Position = vec4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
}
