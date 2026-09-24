#version 450
#extension GL_GOOGLE_include_directive : require
// The exhaust, as an optically thin glowing gas and not as a painted cone.
//
// A column of gas that emits and does not absorb is as bright, at each pixel, as
// the length of the line of sight inside it. For a circular cross-section that
// chord is 2 r cos(theta), theta being the angle between the surface normal and
// the view: |N.V| is the column depth, EXACTLY, with no fitting. So the edge of
// the jet is not a line but where the chord goes to zero, and the mesh's
// silhouette vanishes. Drawn with both faces and additive blending, each face
// contributes half of it. `edge_power` above 1 makes a denser axis than a
// uniform column, which is what a jet that is hottest in the middle has.
//
// Along the jet the brightness falls as the gas expands and cools (`decay`) and
// the far end dissolves (`tail_fade`) instead of being cut off. The streaks are
// the one moving part: density fluctuations carried downstream at the flow
// speed. ⚠️ DRAWING, not physics: no radiative transfer, no nozzle model, and no
// shock diamonds -- those need an ambient pressure to reflect off, and in vacuum
// there is none.
#include "common.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_local;

layout(location = 0) out vec4 result;

layout(set = 3, binding = 0) uniform PlumeFragment {
    vec4 camera_position;   // xyz
    vec4 near_colour;       // linear rgb x energy at the nozzle; w = edge power
    vec4 far_colour;        // linear rgb x energy at the tail; w = decay
    vec4 flow;              // x time, y turbulence, z streaks, w flow speed
    vec4 shape;             // x tail fade start
};

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Value noise whose y is periodic, so that going once round the jet closes up
// without a seam.
float value_noise(vec2 p, float period) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float y0 = mod(i.y, period);
    float y1 = mod(i.y + 1.0, period);
    float a = hash(vec2(i.x, y0));
    float b = hash(vec2(i.x + 1.0, y0));
    float c = hash(vec2(i.x, y1));
    float d = hash(vec2(i.x + 1.0, y1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    // The mesh is one unit tall along y, its top at the tail: t runs 0 at the
    // nozzle to 1 at the far end.
    float t = clamp(v_local.y + 0.5, 0.0, 1.0);

    vec3 N = normalize(v_normal);
    vec3 V = normalize(camera_position.xyz - v_world);
    float column = pow(abs(dot(N, V)), near_colour.w);

    float along = exp(-far_colour.w * t);
    // A fade start at or past the tip means "no fade" (the exit glow and the
    // reactor disc pass 2). It has to be said explicitly: smoothstep with
    // edge0 >= edge1 is undefined in GLSL, and on Metal it returned 1 -- tail 0,
    // and those parts were never drawn.
    float tail = shape.x < 1.0 ? 1.0 - smoothstep(shape.x, 1.0, t) : 1.0;

    // Streaks travel downstream; the angle round the axis decorrelates them so
    // they are filaments and not rings. Two octaves, and the amplitude grows
    // downstream, where a real jet is less orderly.
    float turn = atan(v_local.z, v_local.x) / (2.0 * PI) + 0.5;   // 0..1 round the axis
    float s = t * flow.z - flow.x * flow.w * flow.z;
    float n = value_noise(vec2(s, turn * 7.0), 7.0) * 0.65 + value_noise(vec2(s * 2.3, turn * 14.0), 14.0) * 0.35;
    float streak = 1.0 + flow.y * (0.4 + 0.6 * t) * (n * 2.0 - 1.0);

    vec3 colour = mix(near_colour.rgb, far_colour.rgb, smoothstep(0.0, 0.7, t));
    result = vec4(colour * (column * along * tail * max(streak, 0.0) * 0.5), 0.0);
}
