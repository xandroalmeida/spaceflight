#include "app/presentation/scene/ship_materials.hpp"

namespace sf::app {
namespace {

Material pbr(Colour albedo, double roughness, double metallic) {
    Material m{};
    m.albedo = albedo;
    m.roughness = roughness;
    m.metallic = metallic;
    return m;
}

// A texture, where it is expected (rule 81). `uv_scale` is how many times the
// tile fits on the part, and it is per material and not per mesh: the
// pressurised hull is 9.4 m round and the tanks 10.7 m, so one number serves
// both to within 12 % of texel density. The albedo goes white: the colour now
// comes from the texture, and multiplying the two would darken the part
// relative to what the image shows.
void textured(Material& m, const char* path, Vec2 scale) {
    m.albedo_texture = path;
    m.uv_scale = scale;
    m.albedo = Colour{1.0F, 1.0F, 1.0F, 1.0F};
}

}  // namespace

ShipMaterials::ShipMaterials() {
    hull_paint = pbr(Colour{0.78F, 0.79F, 0.80F}, 0.55, 0.0);
    textured(hull_paint, "textures/spacecraft/hull_panels.png", Vec2{5.0, 4.0});
    bare_metal = pbr(Colour{0.62F, 0.64F, 0.67F}, 0.28, 1.0);
    dark_composite = pbr(Colour{0.13F, 0.14F, 0.16F}, 0.75, 0.0);
    // Gold and rough: multi-layer insulation is the surface that most says
    // "this operates in vacuum" in a picture, and the only saturated colour on
    // the whole ship.
    thermal_blanket = pbr(Colour{0.86F, 0.68F, 0.26F}, 0.62, 0.85);
    // The radiator is almost white and a little emissive, because that is what
    // it does: reject heat. 0.04 and not 0.08: at 0.08 the panels were the
    // brightest thing on the ship on the dark side. A radiator rejecting heat
    // emits in the infrared, which is not seen; what is drawn is a suggestion,
    // and a suggestion cannot dominate the image.
    radiator = pbr(Colour{0.90F, 0.91F, 0.92F}, 0.40, 0.0);
    radiator.emission = Colour{0.42F, 0.26F, 0.20F};
    radiator.emission_energy = 0.04;
    // The engine bell: niobium darkened by use, very metallic and smooth enough
    // to catch the Sun on the rim.
    engine_bell = pbr(Colour{0.34F, 0.30F, 0.29F}, 0.34, 1.0);
    solar_cell = pbr(Colour{0.08F, 0.10F, 0.20F}, 0.25, 0.4);

    // Glass has to look like glass AND let the ship be flown (rule 52). High
    // transparency, almost no tint and a discreet specular -- no strong blue and
    // no environment reflection, which is what usually makes a simulator window
    // useless exactly when there is something outside to see.
    glass = pbr(Colour{0.72F, 0.78F, 0.82F, 0.032F}, 0.03, 0.0);
    glass.blend = Blend::Alpha;
    glass.cull = Cull::None;
    glass.casts_shadow = false;

    // The displays' glass is the same glass, darker, so an unlit display reads as
    // unlit and not as a white surface.
    display_glass = pbr(Colour{0.03F, 0.035F, 0.04F}, 0.12, 0.0);

    // Roughness 0.92 and not 0.72: at 0.72 the panel light left a round, bright
    // specular halo in the middle of the surface, which reads as a rendering
    // defect. A cockpit surface is matte.
    cockpit_panel = pbr(palette::PANEL, 0.92, 0.0);
    textured(cockpit_panel, "textures/cockpit/panel_surface.png", Vec2{2.0, 2.0});

    // Floor and ceiling are single-sided polygons -- the nose is chamfered and a
    // box is not. Without culling, the vertex order no longer decides whether
    // the surface exists, and the texture coordinates are in world metres, so the
    // material's scale stays at 1.
    cabin_shell = pbr(palette::PANEL, 0.92, 0.0);
    cabin_shell.cull = Cull::None;
    textured(cabin_shell, "textures/cockpit/panel_surface.png", Vec2{1.0, 1.0});

    // The window structure: machined aluminium, cold grey, metallic enough that
    // an edge catches light and the face next to it does not. Dark and not very
    // polished on purpose: a window post on a spaceship is painted matte grey
    // precisely so that it does NOT throw light into the pilot's eyes.
    frame_alloy = pbr(Colour{0.27F, 0.285F, 0.30F}, 0.50, 0.80);

    // The edges and bolts of the retaining ring. Metallic 0.35 and NOT 1.0: with
    // metallic 1.0 a surface has no diffuse part at all, and with no reflection
    // probe here the result was a corner that was BLACK everywhere except where
    // the specular lobe hit, and there blown-out white.
    machined_trim = pbr(Colour{0.46F, 0.47F, 0.49F}, 0.45, 0.35);

    // The seal between glass and frame. Black and matte: without it the pane
    // reads as a rectangle drawn ON the frame instead of a part seated in it.
    window_seal = pbr(Colour{0.045F, 0.047F, 0.052F}, 0.95, 0.0);

    indicator_off = pbr(Colour{0.10F, 0.11F, 0.12F}, 0.5, 0.0);
}

Material ShipMaterials::emissive(Colour colour, double energy) {
    Material m{};
    m.albedo = colour;
    m.emission = colour;
    m.emission_energy = energy;
    m.roughness = 0.5;
    return m;
}

Material ShipMaterials::additive(Colour colour) {
    Material m{};
    m.albedo = colour;
    m.blend = Blend::Additive;
    m.unshaded = true;
    m.cull = Cull::None;
    m.casts_shadow = false;
    return m;
}

}  // namespace sf::app
