#pragma once

// The star catalogue as a moving observer sees it: one pass of
// core/relativity/optics.hpp over every star, once per frame.
//
// This is the class that keeps the physics out of the shader.  It produces, per
// star, the aberrated direction and the Doppler factor D -- and NOTHING else.
// What D means (T' = D T, I' = D^4 I) is applied downstream, on the GPU, from
// the table in core/render/blackbody.hpp.  The shader never learns what beta is.
// See docs/architecture/relativistic-shaders.md section 2.
//
// Cost, for the record, because the alternative design trades it against
// correctness: 8786 stars x ~60 flops = 0.5 Mflop per frame.

#include "core/math/vec3.hpp"
#include "core/render/blackbody.hpp"
#include "core/render/star_catalog.hpp"
#include "core/render/tone_response.hpp"

#include <cstddef>
#include <vector>

namespace sf::render {

// Laid out to be handed straight to a renderer: parallel arrays, one entry per
// star, in the catalogue's order.
struct SkyFrame {
    std::vector<math::Vec3> apparent_direction;  // unit, ship frame, aberrated
    std::vector<float> doppler;                  // colour-temperature multiplier
    std::vector<float> beaming;                  // bolometric intensity multiplier base D
    std::size_t star_count{0};
};

// The quantities a HUD or a headless run can print to show that the sky is
// actually doing what section 3 and section 10 of the physics document say.
// Computed from the SAME pass, so a diagnostic cannot drift from the image.
struct SkyDiagnostics {
    double beta{0.0};
    double lorentz_factor{1.0};

    double forward_cone_half_angle{0.0};  // [rad], arccos(beta)
    std::size_t stars_in_forward_cone{0};
    double fraction_in_forward_cone{0.0};

    double max_doppler{0.0};   // ahead: gamma (1 + beta)
    double min_doppler{0.0};   // astern: gamma (1 - beta)

    // Brightest and faintest response the detector actually produces this frame,
    // AFTER the band-limited beaming and the saturation curve.  These are the two
    // numbers that show the aft sky going out.
    double brightest_response{0.0};
    double faintest_response{0.0};

    // A 5800 K star dead ahead and dead astern, in the visible band -- the row of
    // the table in section 10.2, computed live rather than quoted.
    double reference_forward_visible{0.0};
    double reference_aft_visible{0.0};
};

class RelativisticSky {
public:
    RelativisticSky(StarCatalog catalog, PlanckTable table);

    [[nodiscard]] const StarCatalog& catalog() const noexcept { return catalog_; }
    [[nodiscard]] const PlanckTable& planck_table() const noexcept { return table_; }
    [[nodiscard]] std::size_t star_count() const noexcept { return catalog_.size(); }

    // `beta` is the observer's velocity over c, in the coordinate frame -- the
    // same convention as core/relativity/optics.hpp, and the reason that file
    // states it twice.
    void update(const math::Vec3& beta, bool apply_aberration = true,
                bool apply_doppler = true, bool apply_beaming = true);

    [[nodiscard]] const SkyFrame& frame() const noexcept { return frame_; }

    // Exposure, a presentation parameter (core/render/tone_response.hpp).
    void set_half_saturation(double half_saturation);
    [[nodiscard]] double half_saturation() const noexcept { return half_saturation_; }

    [[nodiscard]] SkyDiagnostics diagnostics(const math::Vec3& beta) const;

    // The same diagnostics for a beta the ship is NOT travelling at, so that the
    // headline numbers of sections 3 and 10 can be shown through the shipped code
    // path instead of quoted from the document.  It re-runs the pass at `beta`
    // and then restores the frame, so nothing an observer sees changes -- and
    // nothing in the STATE could change anyway, because this whole file only
    // reads (docs/architecture/rendering.md section 2).
    [[nodiscard]] SkyDiagnostics diagnostics_at(const math::Vec3& beta,
                                                const math::Vec3& restore_to);

    // What the shader computes, in C++: the detector response of one star at the
    // current beta.  Present so that a test can check the whole chain -- optics,
    // band-limited beaming, saturation -- end to end without a GPU, and so that
    // the headless run can print a number that means something.
    [[nodiscard]] double response_of(std::size_t index) const;

private:
    StarCatalog catalog_;
    PlanckTable table_;
    SkyFrame frame_{};
    double half_saturation_{kDefaultHalfSaturation};
    bool apply_aberration_{true};
    bool apply_doppler_{true};
    bool apply_beaming_{true};
};

}  // namespace sf::render
