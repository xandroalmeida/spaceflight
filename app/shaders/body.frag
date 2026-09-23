#version 450
#extension GL_GOOGLE_include_directive : require
// The surface: texture, clouds, night side, limb, relief.
//
// Everything here is APPEARANCE. None of it reaches the vertex stage, none of it
// changes a Doppler factor, and turning it all off gives back exactly the
// Milestone 5 image: `reflectance` alone, multiplied by the same ratio. At D = 1
// the ratio is exactly vec3(1) and the surface is the texel (rule 47: a generated
// image is never physics; it says what the body LOOKS like and nothing else).
#include "common.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_tangent;
layout(location = 4) in vec3 v_shifted_colour;
layout(location = 5) in vec3 v_colour_ratio;

layout(location = 0) out vec4 result;

// sRGB-encoded maps are sRGB textures (decoded by the sampler); the cloud
// coverage and the normal map are DATA and are not.
layout(set = 2, binding = 0) uniform sampler2D albedo_map;
layout(set = 2, binding = 1) uniform sampler2D cloud_map;
layout(set = 2, binding = 2) uniform sampler2D night_map;
layout(set = 2, binding = 3) uniform sampler2D normal_map;

layout(set = 3, binding = 0) uniform BodyFragment {
    vec4 sun_direction;       // xyz towards the Sun; w = self luminous
    vec4 sun_light;           // linear rgb x energy
    vec4 ambient;             // linear rgb x energy
    vec4 maps;                // use albedo, use clouds, use night, use normal
    vec4 clouds;              // offset, low, high, opacity
    vec4 atmosphere_colour;   // linear rgb, strength
    vec4 surface;             // haze, night energy, normal strength, unused
    vec4 reflectance;         // linear rgb: the colour where there is no map
};

// Equirectangular sampling without the line at the seam.
//
// At u = 0 = 1 the coordinate jumps from 1 to 0 between two neighbouring pixels.
// The GPU picks the mipmap level from the DERIVATIVE of u, and on that column the
// derivative is ~1 instead of ~1e-3: it picks the coarsest mip and draws a
// visible line from pole to pole. The fix is to tell the GPU the true
// derivative: if the jump is larger than half the texture, it is the wrap and not
// a gradient, and a turn is subtracted.
vec4 sample_wrapped(sampler2D map, vec2 uv) {
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    if (abs(dx.x) > 0.5) { dx.x -= sign(dx.x); }
    if (abs(dy.x) > 0.5) { dy.x -= sign(dy.x); }
    return textureGrad(map, uv, dx, dy);
}

float schlick(float cosine) {
    float m = clamp(1.0 - cosine, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

// ROUGHNESS 1.0: the default of every Godot spatial material, which the bodies
// never overrode (the scene's evidence was taken with it).
#define BODY_ROUGHNESS 1.0

void main() {
    // A star emits its own black body; a planet reflects the Sun's.
    if (sun_direction.w > 0.5) {
        result = vec4(v_shifted_colour, 1.0);
        return;
    }

    vec3 surface_colour = reflectance.rgb;
    if (maps.x > 0.5) {
        surface_colour = sample_wrapped(albedo_map, v_uv).rgb;
    }
    // Clouds: a second layer with its own longitude offset, over the surface and
    // under everything else. The offset is a DRAWING parameter -- there is no
    // atmospheric circulation in this project.
    //
    // The mask's curve is MEASURED against the map in use: histogram of the NASA
    // MODIS composite p10 2, p25 14, p50 60, p75 128, p90 178; remapped to
    // 41..153 it leaves 42 % coverage and 42 % genuinely clear sky -- less than
    // the Earth's real 67 %, which is wanted: the clouds have to let the
    // continents underneath show.
    if (maps.y > 0.5) {
        float raw = sample_wrapped(cloud_map, vec2(fract(v_uv.x + clouds.x), v_uv.y)).r;
        float cloud = smoothstep(clouds.y, clouds.z, raw);
        surface_colour = mix(surface_colour, srgb_to_linear(vec3(0.92, 0.94, 0.96)), cloud * clouds.w);
    }
    vec3 albedo = surface_colour * v_colour_ratio;

    vec3 normal = normalize(v_normal);
    vec3 geometric_normal = normal;
    if (maps.w > 0.5) {
        // The relief, derived from the topography LOLA measured. The tangent comes
        // from the mesh at REST, and the vertex stage moves the vertices by the
        // retarded time; the difference is second order in beta and vanishes at
        // orbital speeds (recorded as VISUAL_DEBT).
        vec3 t = normalize(v_tangent.xyz - normal * dot(normal, v_tangent.xyz));
        vec3 b = cross(normal, t) * v_tangent.w;
        vec3 n_ts = sample_wrapped(normal_map, v_uv).rgb * 2.0 - 1.0;
        n_ts.xy *= surface.z;
        normal = normalize(t * n_ts.x + b * n_ts.y + normal * n_ts.z);
    }

    vec3 L = normalize(sun_direction.xyz);
    vec3 V = normalize(-v_world);
    vec3 H = normalize(L + V);
    float NdotL = max(dot(normal, L), 0.0);
    float NdotV = max(dot(normal, V), 1e-4);
    float LdotH = max(dot(L, H), 0.0);
    // Burley's diffuse at roughness 1, which is what the body's surface is.
    float fd90_minus_1 = 2.0 * LdotH * LdotH * BODY_ROUGHNESS - 0.5;
    float burley = (1.0 + fd90_minus_1 * schlick(NdotV)) * (1.0 + fd90_minus_1 * schlick(NdotL));
    // And the specular every Godot material had without asking for it
    // (SPECULAR 0.5 -> F0 0.04, ROUGHNESS 1, a directional light's specular
    // 0.5): at roughness 1 it is not a glint but the broad sheen of the ocean
    // towards the Sun, and without it the lit disc reads flat.
    float NdotH = max(dot(normal, H), 0.0);
    const float roughness = BODY_ROUGHNESS;
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    float d = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    float D = alpha2 / (PI * d * d);
    const float k = alpha * 0.5;
    float visibility = 0.25 / ((NdotL * (1.0 - k) + k) * (NdotV * (1.0 - k) + k));
    float F = 0.04 + 0.96 * schlick(LdotH);
    vec3 specular = sun_light.rgb * (D * visibility * F * PI * 0.5 * NdotL);
    vec3 lit = albedo * (sun_light.rgb * burley * NdotL + ambient.rgb) + specular;

    // How lit this pixel is, from the direction of the Sun, on the GEOMETRIC
    // normal: the terminator is where it is, not where the relief makes it.
    float sun = dot(geometric_normal, L);
    vec3 emitted = vec3(0.0);
    if (maps.z > 0.5) {
        // City lights, fading in across the terminator rather than switching at
        // it: a hard line at sun = 0 reads as a rendering artefact.
        float night = smoothstep(0.08, -0.16, sun);
        emitted += sample_wrapped(night_map, v_uv).rgb * night * surface.y;
    }
    if (atmosphere_colour.w > 0.0) {
        // The limb: air is thickest where the line of sight grazes the surface.
        // Brightened only on the lit side, because an unlit limb does not glow.
        // ⚠️ DRAWING, not physics: there is no atmospheric model in this project
        // and rule 31 says there must not be.
        float grazing = 1.0 - abs(dot(geometric_normal, V));
        float limb = pow(clamp(grazing, 0.0, 1.0), 3.0);
        float lit_side = clamp(sun * 1.4 + 0.25, 0.0, 1.0);
        emitted += atmosphere_colour.rgb * limb * lit_side * atmosphere_colour.w;
        // Without the limb: the air over the whole disc, fading at the terminator,
        // because a sky on the dark side scatters nothing.
        emitted += atmosphere_colour.rgb * lit_side * surface.x;
    }
    result = vec4(lit + emitted, 1.0);
}
