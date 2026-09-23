#version 450
#extension GL_GOOGLE_include_directive : require
// A physically based surface in the metallic-roughness form: Burley diffuse, GGX
// specular, one directional light with a shadow map, a few point lights and an
// ambient term. It stands where Godot's StandardMaterial3D stood, and keeps its
// conventions -- light energy multiplies the light's colour, and a point light
// falls off as (1 - (d/range)^4)^2 / d -- so the materials of
// app/presentation/scene/ship_materials.cpp keep the numbers they were tuned
// with.
#include "common.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 result;

layout(set = 2, binding = 0) uniform sampler2D albedo_map;
layout(set = 2, binding = 1) uniform sampler2DShadow shadow_map;

const int MAX_POINT_LIGHTS = 4;

layout(set = 3, binding = 0) uniform LitFragment {
    vec4 albedo;              // linear rgb, alpha
    vec4 emission;            // linear rgb x energy
    vec4 material;            // roughness, metallic, use texture, unshaded
    vec4 camera_position;     // xyz
    vec4 sun_direction;       // xyz towards the light
    vec4 sun_light;           // linear rgb x energy
    vec4 ambient;             // linear rgb x energy
    mat4 shadow_matrix;       // world -> shadow map uv / depth
    vec4 shadow_params;       // enabled, texel size, bias, unused
    vec4 point_position[MAX_POINT_LIGHTS];   // xyz, range
    vec4 point_colour[MAX_POINT_LIGHTS];     // linear rgb x energy
    vec4 point_count;
};

float schlick(float cosine) {
    float m = clamp(1.0 - cosine, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

// One light's contribution: Burley diffuse and GGX specular.
vec3 shade(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 base, float roughness, float metallic) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) {
        return vec3(0.0);
    }
    vec3 H = normalize(L + V);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float LdotH = max(dot(L, H), 0.0);

    float fd90_minus_1 = 2.0 * LdotH * LdotH * roughness - 0.5;
    float burley = (1.0 + fd90_minus_1 * schlick(NdotV)) * (1.0 + fd90_minus_1 * schlick(NdotL));
    vec3 diffuse = base * (1.0 - metallic) * burley;

    float alpha = max(roughness * roughness, 0.002);
    float alpha2 = alpha * alpha;
    float d = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    float D = alpha2 / (PI * d * d);
    float k = alpha * 0.5;
    float visibility = 0.25 / ((NdotL * (1.0 - k) + k) * (NdotV * (1.0 - k) + k));
    vec3 f0 = mix(vec3(0.04), base, metallic);
    vec3 F = f0 + (vec3(1.0) - f0) * schlick(LdotH);
    vec3 specular = D * visibility * F * PI;

    return (diffuse + specular) * radiance * NdotL;
}

float sun_visibility(vec3 world, vec3 normal) {
    if (shadow_params.x < 0.5) {
        return 1.0;
    }
    // A normal offset of a couple of texels keeps a surface from shadowing
    // itself where the light grazes it.
    vec3 offset = world + normal * shadow_params.y * 2.0;
    vec4 projected = shadow_matrix * vec4(offset, 1.0);
    vec3 coord = projected.xyz / projected.w;
    if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0 || coord.z < 0.0 || coord.z > 1.0) {
        return 1.0;
    }
    // 3x3 percentage-closer filtering.
    float lit = 0.0;
    float texel = shadow_params.y;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += texture(shadow_map, vec3(coord.xy + vec2(x, y) * texel, coord.z - shadow_params.z));
        }
    }
    return lit / 9.0;
}

void main() {
    vec3 base = albedo.rgb;
    float alpha = albedo.a;
    if (material.z > 0.5) {
        vec4 texel = texture(albedo_map, v_uv);
        base *= texel.rgb;
        alpha *= texel.a;
    }
    if (material.w > 0.5) {
        result = vec4(base + emission.rgb, alpha);
        return;
    }

    vec3 N = normalize(v_normal);
    vec3 V = normalize(camera_position.xyz - v_world);
    // A single-sided polygon drawn from behind is lit from its front: flip the
    // normal towards the viewer, as double-sided materials do.
    if (!gl_FrontFacing) {
        N = -N;
    }
    float roughness = clamp(material.x, 0.02, 1.0);
    float metallic = clamp(material.y, 0.0, 1.0);

    vec3 colour = vec3(0.0);
    vec3 L = normalize(sun_direction.xyz);
    colour += shade(N, V, L, sun_light.rgb, base, roughness, metallic) * sun_visibility(v_world, N);

    int count = int(point_count.x);
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (i >= count) {
            break;
        }
        vec3 to_light = point_position[i].xyz - v_world;
        float distance = length(to_light);
        float range = point_position[i].w;
        float nd = distance / range;
        nd *= nd;
        nd *= nd;
        nd = max(1.0 - nd, 0.0);
        float attenuation = nd * nd / max(distance, 1e-4);
        colour += shade(N, V, to_light / max(distance, 1e-6), point_colour[i].rgb * attenuation, base, roughness,
                        metallic);
    }

    // Ambient: the light reflected by the planet, which exists and is weak.
    // Metals get it through their specular colour -- there is no reflection probe,
    // and without this a metallic part would be black everywhere except where the
    // Sun's lobe hits.
    vec3 f0 = mix(vec3(0.04), base, metallic);
    colour += ambient.rgb * (base * (1.0 - metallic) + f0 * (1.0 - 0.5 * roughness));
    colour += emission.rgb;
    result = vec4(colour, alpha);
}
