shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;

// Colour and brightness of a star from its Doppler factor.
//
// This shader contains NO relativity.  D arrives per vertex, already computed by
// core/relativity/optics.hpp on the CPU, and the two lines that use it are what D
// MEANS rather than how it was obtained:
//
//   T' = D T      docs/physics/relativistic-rendering.md section 4
//   I' = D^4 I    section 5, corrected to the visible band by section 10.1
//
// It does not know what beta is, and it must not learn.
// See docs/architecture/relativistic-shaders.md sections 2 and 3.
//
// ---------------------------------------------------------------------------
// Why the colour goes to ALBEDO and not to EMISSION
//
// Under `render_mode unshaded` Godot 4's Forward+ renderer resolves the fragment
// to `vec4(albedo, alpha)` and never reads EMISSION: the emission term only
// exists in the lit branch, alongside the ambient, diffuse and specular it is
// summed with.  Writing EMISSION here paints every star with ALBEDO -- which the
// first version set to vec3(0.0).  8786 stars were uploaded, transformed and
// rasterised, in black, onto black.  That is the whole of the Milestone 6
// "starfield invisible" blocker.
//
// Measured on Godot 4.5.stable, Metal 3.2, Forward+, ten point primitives:
//
//   unshaded + EMISSION     0 lit pixels
//   unshaded + ALBEDO     360 lit pixels
//   lit      + EMISSION   360 lit pixels
//
// `unshaded` is the right mode -- a star must not be shaded by the scene's
// lights -- so the colour belongs in ALBEDO.  docs/validation/starfield-debug.md
// section 4.
// ---------------------------------------------------------------------------

// rgb: linear-sRGB chromaticity of a black body.  a: ln of the band efficiency
// eta(T).  Built by core/render/blackbody.hpp -- never by hand, never exported
// from a tool, because it is what a test compares against the published
// Planckian locus.
uniform sampler2D planck_table : filter_linear, repeat_disable;

// The bijection T -> [0,1) that indexes it.  A range with a clamp would be the
// forbidden shape (rule 13); this has no ends.
uniform float table_reference_temperature = 6000.0;

// Exposure: the luminance that reads half scale.  Naka-Rushton, section 10.4 --
// a saturating sensor, not a cut.
uniform float half_saturation = 0.1584893;

// Point size in pixels for a star at half scale.  Presentation only: a real
// star is a point source and its apparent size on any detector is the
// instrument's, not the star's.
uniform float base_point_size = 2.0;
uniform float max_point_size = 6.0;

// STARFIELD_DEBUG (docs/validation/starfield-debug.md section 3).
//
// Ignores magnitude, temperature, Doppler, beaming and the tone curve, and draws
// every star at one size in one white.  It answers exactly one question -- is the
// GEOMETRY reaching the framebuffer -- and it answers it without any of the
// photometry being able to hide the answer by returning zero.  It is a debug
// path and it is switched off by default, so nothing a pilot sees goes through
// it.
uniform bool starfield_debug = false;
uniform float debug_point_size = 3.0;

// The second debug axis, and it is a different question from the first.
//
// A star sprite is shaped twice for PRESENTATION: its size tracks its brightness,
// and its alpha falls off radially so that a square sprite reads as a point.
// Neither is photometry -- the physics is in `response` -- but both of them land
// on the pixel a harness measures, and together they cost a measurement about
// 10% and make it depend on where inside its texel the sprite happened to fall.
//
// This switches the SHAPING off and leaves the photometry alone, so that a
// measured pixel is the response and nothing else.  It changes what a star looks
// like; it does not change what a star is.  Off by default.
uniform bool starfield_flat_sprite = false;

varying flat vec3 star_colour;

float table_index(float temperature) {
    return temperature / (temperature + table_reference_temperature);
}

void vertex() {
    // A point primitive has one vertex per star, so everything per-star belongs
    // here and the fragment stage only paints.
    float rest_temperature = CUSTOM0.x;
    float rest_flux = CUSTOM0.y;
    float doppler = CUSTOM0.z;
    float beaming = CUSTOM0.w;

    if (starfield_debug) {
        star_colour = vec3(1.0);
        POINT_SIZE = debug_point_size;
    } else {
        float shifted_temperature = rest_temperature * doppler;

        vec4 rest = textureLod(planck_table, vec2(table_index(rest_temperature), 0.5), 0.0);
        vec4 shifted = textureLod(planck_table, vec2(table_index(shifted_temperature), 0.5), 0.0);

        // eta(DT)/eta(T) as a difference of logarithms.  The catalogue's V magnitude
        // is a flux ALREADY IN THE BAND, so the bolometric D^4 has to be corrected by
        // how much of the shifted spectrum is still in it -- which is the whole
        // reason the forward sky is 51x and not 400x at beta = 0.9048.
        float band = exp(shifted.a - rest.a);

        float luminance = rest_flux * pow(beaming, 4.0) * band;

        // The structural limit.  A bijection [0, inf) -> [0, 1): nothing is cut off,
        // the scale is compressed, and the ordering survives.  When `luminance`
        // underflows to zero -- the aft sky at beta = 0.99 -- the answer is zero, and
        // that is the physics: there are no photons in the band.
        float response = luminance / (luminance + half_saturation);

        star_colour = shifted.rgb * response;

        // Brighter stars spread further on any real detector, so size tracks the same
        // response rather than being a second, independent fudge.
        POINT_SIZE = clamp(base_point_size * (0.5 + response), 1.0, max_point_size);
    }
    if (starfield_flat_sprite) {
        POINT_SIZE = debug_point_size;
    }
}

void fragment() {
    // ALBEDO, not EMISSION: see the note at the top of the file.  `unshaded`
    // means this value IS the pixel, with no light touching it -- which is what a
    // star is.
    ALBEDO = star_colour;
    // A point sprite is square; round it off, and let the brightness fall towards
    // the edge so a star reads as a point source rather than a tile.
    vec2 offset = POINT_COORD - vec2(0.5);
    float radial = clamp(1.0 - 4.0 * dot(offset, offset), 0.0, 1.0);
    ALPHA = starfield_flat_sprite ? 1.0 : radial;
}
