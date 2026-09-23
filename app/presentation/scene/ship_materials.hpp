#pragma once

// The materials of the ship and the cockpit, made once and shared (rule 50).
//
// The whole aesthetic choice is in this file: graphite grey, bare aluminium,
// dark composite, gold thermal blanket, white radiator. Modern aerospace,
// industrial, no neon (rule 71).

#include "app/presentation/scene/scene.hpp"

namespace sf::app {

struct ShipMaterials {
    ShipMaterials();

    Material hull_paint;
    Material bare_metal;
    Material dark_composite;
    Material glass;
    Material display_glass;
    Material cockpit_panel;
    Material cabin_shell;
    Material frame_alloy;
    Material machined_trim;
    Material window_seal;
    Material radiator;
    Material thermal_blanket;
    Material engine_bell;
    Material solar_cell;
    Material indicator_off;

    // A self-luminous material for lamps and signs: the colour is the state.
    [[nodiscard]] static Material emissive(Colour colour, double energy = 1.6);
    // Additive and without depth writes: what a flame is. The plume and the RCS
    // jets. With additive blending it is the RGB that counts, not the alpha.
    [[nodiscard]] static Material additive(Colour colour);
};

}  // namespace sf::app
