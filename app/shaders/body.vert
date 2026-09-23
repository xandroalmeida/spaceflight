#version 450
#extension GL_GOOGLE_include_directive : require
// A body as it is SEEN: per-vertex light time, and colour shifted by the Doppler
// factor of its centre.
//
// Two things happen here, and only one of them is physics this shader owns.
//
//   colour   D arrives as a uniform from core/relativity/optics.hpp, and the
//            shader applies what D means -- exactly as star.vert does.
//
//   shape    the per-vertex retarded time, which is per-vertex BY DEFINITION and
//            therefore cannot live anywhere else. This is the one formula in the
//            project that exists twice; it mirrors
//            sf::render::retarded_light_time() and sf::render::lorentz_contract()
//            in core/render/terrell.hpp, and what is done about the duplication
//            is docs/architecture/relativistic-shaders.md section 5: the test
//            pins the RESULT (arcsin beta, a round silhouette) rather than the
//            source text.
//
// Nobody rotates anything. Terrell rotation is what comes out.
//
// Positions are CAMERA-RELATIVE and in scene units: the model matrix carries the
// body's position minus the camera's (subtracted in double on the CPU), so the
// observer is the origin and the light-cone equation compares a length against
// c dtau in the units c is given in.
#include "common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_tangent;
layout(location = 4) out vec3 v_shifted_colour;
layout(location = 5) out vec3 v_colour_ratio;

layout(set = 0, binding = 0) uniform sampler2D planck_table;

layout(set = 1, binding = 0) uniform BodyVertex {
    mat4 model;               // body axes x radius, translated to (body - camera)
    mat4 view_projection;     // rotation-only view, then projection
    vec4 velocity_c;          // relative velocity (scene units / s), light speed (scene units / s)
    vec4 optics;              // doppler, beaming doppler, half saturation, table reference temperature
    vec4 reflectance;         // linear rgb, is_self_luminous
    vec4 flags;               // apply_light_time, illuminant temperature
};

// |p - v dtau| = c dtau, solved. Mirrors sf::render::retarded_light_time().
//
// No clamp and no branch: the discriminant is structurally positive because
// c^2 - v^2 > 0 is guaranteed by the state (rule 13) and survives the scaling
// exactly.
float retarded_light_time(vec3 offset, vec3 velocity, float c) {
    float pv = dot(offset, velocity);
    float c2_minus_v2 = c * c - dot(velocity, velocity);
    float discriminant = pv * pv + c2_minus_v2 * dot(offset, offset);
    return (sqrt(discriminant) - pv) / c2_minus_v2;
}

// Mirrors sf::render::lorentz_contract(). A mesh is the body's shape in ITS OWN
// frame; this is what makes it a set of positions in the observer's frame at one
// instant, which is the question the light cone can then be asked about.
//
// This is NOT what rule 38 forbids. Forbidden is contraction as the ANSWER --
// drawing the squashed mesh. Omitting it does not give "no contraction": it
// gives a silhouette 160 % out of round, against 9e-5 with it
// (docs/physics/relativistic-rendering.md section 11.5).
vec3 lorentz_contract(vec3 rest_offset, vec3 velocity, float c) {
    float speed = length(velocity);
    if (speed <= 0.0) {
        return rest_offset;
    }
    vec3 direction = velocity / speed;
    float beta = speed / c;
    float inverse_gamma = sqrt(1.0 - beta * beta);
    float along = dot(rest_offset, direction);
    return rest_offset - direction * along + direction * (along * inverse_gamma);
}

void main() {
    vec3 centre = model[3].xyz;
    vec3 world = (model * vec4(in_position, 1.0)).xyz;
    vec3 velocity = velocity_c.xyz;
    float c = velocity_c.w;

    if (flags.x > 0.5) {
        // The mesh, in world orientation, is the body's shape in its own rest
        // frame. Two steps, in this order (section 11.5). The body's CENTRE was
        // already placed by core/relativity/light_time.hpp against the real
        // ephemeris, so this computes only the DIFFERENTIAL within the body and
        // must not retard the centre a second time.
        vec3 rest_offset = world - centre;
        vec3 contracted = lorentz_contract(rest_offset, velocity, c);
        vec3 p = centre + contracted;
        float dtau_vertex = retarded_light_time(p, velocity, c);
        float dtau_centre = retarded_light_time(centre, velocity, c);
        vec3 apparent_offset = (p - velocity * dtau_vertex) - (centre - velocity * dtau_centre);
        world = centre + apparent_offset;
    }

    float illuminant = flags.y;
    vec4 rest = planck_sample(planck_table, illuminant, optics.w);
    vec4 shifted = planck_sample(planck_table, illuminant * optics.x, optics.w);
    float band = exp(shifted.a - rest.a);
    float luminance = pow(optics.y, 4.0) * band;
    float response = luminance / (luminance + optics.z);

    if (reflectance.w > 0.5) {
        // A star: it emits its own black body, and the beaming acts on it.
        v_shifted_colour = shifted.rgb * response;
        v_colour_ratio = vec3(1.0);
    } else {
        // A planet: reflectance times a shifted illuminant. At D = 1 the ratio is
        // exactly 1 and the colour is the tabulated albedo -- anything else would
        // mean the scene lies while standing still. The epsilon is float
        // hygiene, not a limit.
        v_colour_ratio = shifted.rgb / max(rest.rgb, vec3(1e-6));
        v_shifted_colour = reflectance.rgb * v_colour_ratio;
    }

    mat3 normal_matrix = mat3(model);
    v_normal = normalize(normal_matrix * in_normal);
    v_tangent = vec4(normalize(normal_matrix * in_tangent.xyz), in_tangent.w);
    v_world = world;
    v_uv = in_uv;
    gl_Position = view_projection * vec4(world, 1.0);
}
