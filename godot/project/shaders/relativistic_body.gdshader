shader_type spatial;
// world_vertex_coords puts VERTEX in SCENE units on the way in and on the way
// out.  That is not a convenience: the light-cone equation compares a length
// against c * dtau, so the vertex has to be in the same units as the c that is
// passed in, and model space is scaled by the body's radius.
render_mode world_vertex_coords;

// A body as it is SEEN: per-vertex light time, and colour shifted by the Doppler
// factor of its centre.
//
// Two things happen here, and only one of them is physics this shader owns.
//
//   colour   D arrives as a uniform from core/relativity/optics.hpp, and the
//            shader applies what D means -- exactly as star_field.gdshader does.
//
//   shape    the per-vertex retarded time, which is per-vertex BY DEFINITION and
//            therefore cannot live anywhere else.  This is the one formula in
//            the project that exists twice; it mirrors
//            sf::render::retarded_light_time() and
//            sf::render::lorentz_contract() in core/render/terrell.hpp, and what
//            is done about the duplication is
//            docs/architecture/relativistic-shaders.md section 5: the test pins
//            the RESULT (arcsin beta to 1e-5 deg, a round silhouette to 3e-4)
//            rather than the source text.
//
// Nobody rotates anything.  Terrell rotation is what comes out.

uniform sampler2D planck_table : filter_linear, repeat_disable;
uniform float table_reference_temperature = 6000.0;

// Doppler factor of the body centre, from optics.hpp.
uniform float doppler = 1.0;
uniform float beaming_doppler = 1.0;

// The body's velocity relative to the observer, and the speed of light, BOTH in
// scene units per second.  The scale cancels out of the retarded time, which is
// why they must share it: a c in metres per second against a position in scene
// units would put the effect seven orders of magnitude out and it would simply
// vanish (docs/architecture/relativistic-shaders.md section 4).
uniform vec3 relative_velocity_scene = vec3(0.0);
uniform float light_speed_scene = 299.792458;

// The observer, in scene coordinates.  The body centre comes from MODEL_MATRIX,
// and it was already placed by core/relativity/light_time.hpp against the real
// ephemeris -- minutes of light time along a curved path -- so this shader
// computes only the DIFFERENTIAL within the body and must not retard the centre
// a second time.
uniform vec3 observer_position_scene = vec3(0.0);

uniform vec3 reflectance : source_color = vec3(0.5);

// Planets reflect; they are not black bodies.  The illuminant is, so the shift
// acts on sunlight and the albedo multiplies it
// (docs/physics/relativistic-rendering.md section 9.4).
uniform float illuminant_temperature = 5772.0;
// A star (the Sun) emits its own black body instead, at its own temperature.
uniform bool is_self_luminous = false;
uniform float half_saturation = 0.1584893;

// Off, the body is drawn where it geometrically is.  Provided so the difference
// can be seen rather than argued about; the STATE is identical either way.
uniform bool apply_light_time = true;

varying vec3 shifted_colour;

float table_index(float temperature) {
    return temperature / (temperature + table_reference_temperature);
}

// |p - v dtau| = c dtau, solved.  Mirrors sf::render::retarded_light_time().
//
// No clamp and no branch: the discriminant is structurally positive because
// c^2 - v^2 > 0 is guaranteed by the state (rule 13) and survives the scaling
// above exactly.
float retarded_light_time(vec3 offset, vec3 velocity, float c) {
    float pv = dot(offset, velocity);
    float c2_minus_v2 = c * c - dot(velocity, velocity);
    float discriminant = pv * pv + c2_minus_v2 * dot(offset, offset);
    return (sqrt(discriminant) - pv) / c2_minus_v2;
}

// Mirrors sf::render::lorentz_contract().  A mesh is the body's shape in ITS OWN
// frame; this is what makes it a set of positions in the observer's frame at one
// instant, which is the question the light cone can then be asked about.
//
// This is NOT what rule 38 forbids.  Forbidden is contraction as the ANSWER --
// drawing the squashed mesh.  Omitting it does not give "no contraction": it
// gives a silhouette 160% out of round, against 9e-5 with it
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

void vertex() {
    vec3 centre = MODEL_MATRIX[3].xyz;
    vec3 centre_from_observer = centre - observer_position_scene;

    if (apply_light_time) {
        // The mesh, in world orientation, is the body's shape in its own rest
        // frame.  Two steps, in this order (section 11.5).
        vec3 rest_offset = VERTEX - centre;
        vec3 contracted = lorentz_contract(rest_offset, relative_velocity_scene, light_speed_scene);

        vec3 p = centre_from_observer + contracted;
        float dtau_vertex = retarded_light_time(p, relative_velocity_scene, light_speed_scene);
        float dtau_centre =
            retarded_light_time(centre_from_observer, relative_velocity_scene, light_speed_scene);

        // The differential, added back to the centre the CPU already placed.
        vec3 apparent_offset = (p - relative_velocity_scene * dtau_vertex) -
                               (centre_from_observer - relative_velocity_scene * dtau_centre);
        VERTEX = centre + apparent_offset;
    }

    vec4 rest = textureLod(planck_table, vec2(table_index(illuminant_temperature), 0.5), 0.0);
    vec4 shifted =
        textureLod(planck_table, vec2(table_index(illuminant_temperature * doppler), 0.5), 0.0);

    float band = exp(shifted.a - rest.a);
    float luminance = pow(beaming_doppler, 4.0) * band;
    float response = luminance / (luminance + half_saturation);

    if (is_self_luminous) {
        // A star: it emits its own black body, and the beaming acts on it.
        shifted_colour = shifted.rgb * response;
    } else {
        // A planet: grey reflectance times a shifted illuminant.  At D = 1 the
        // ratio is exactly 1 and the colour is the tabulated albedo -- anything
        // else would mean the scene lies while standing still.  The division is
        // safe for any illuminant above ~1500 K, where every tabulated channel is
        // strictly positive; the epsilon is float hygiene, not a limit.
        shifted_colour = reflectance * (shifted.rgb / max(rest.rgb, vec3(1e-6)));
    }
}

void fragment() {
    ALBEDO = is_self_luminous ? vec3(0.0) : shifted_colour;
    if (is_self_luminous) {
        EMISSION = shifted_colour;
    }
}
