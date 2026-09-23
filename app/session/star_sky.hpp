#pragma once

// StarSky: the relativistic star field, as much of it as the renderer gets.
//
// Deliberately a SEPARATE class from FlightSession.  The session owns the
// state; this owns a catalogue and a colour table, and the only thing that
// passes between them is a velocity.  The presentation carries that vector and
// computes nothing with it -- which is the rule of this layer
// (docs/architecture/relativistic-shaders.md section 6).
//
// What crosses into the renderer, per frame, per star:
//
//   position   aberrated direction x sky radius   <- core/relativity/optics.hpp
//   custom     (T_rest, F_rest, D_colour, D_beam) <- core/relativity/optics.hpp
//
// and once, at load, the Planck table as a width x 1 RGBA float texture.  The
// shader turns D into colour and brightness and never learns what beta is.

#include "core/math/vec3.hpp"
#include "core/render/relativistic_sky.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sf::app {

// One star, laid out for direct upload as an instance of the star quad.
struct StarVertex {
    float position[3];
    float custom[4];   // rest temperature, rest flux, Doppler D, beaming D
};

struct SkyDiagnosticsView {
    bool valid{false};
    double beta{0.0};
    double lorentz_factor{1.0};
    double forward_cone_deg{0.0};
    long long stars_in_forward_cone{0};
    double fraction_in_forward_cone{0.0};
    double max_doppler{0.0};
    double min_doppler{0.0};
    double brightest_response{0.0};
    double faintest_response{0.0};
    double reference_forward_visible{0.0};
    double reference_aft_visible{0.0};
    long long star_count{0};
};

struct LinearColour {
    double r{0.0};
    double g{0.0};
    double b{0.0};
};

class StarSky {
public:
    StarSky();
    ~StarSky();

    // Reads BSC5.  An empty path means "wherever the build was told the
    // catalogues live".
    bool load_catalogue(const std::string& path);

    // A catalogue built from the arguments instead of from a file, for the
    // starfield validation harness (docs/validation/starfield-debug.md).
    //
    // Six stars on the axes are a better first instrument than 8786 real ones:
    // with the real sky, "the projection is wrong" and "the photometry returned
    // zero" produce the SAME empty screen, and the harness has to be able to tell
    // those apart before it is allowed to believe anything about aberration.
    //
    // The magnitude limit is NOT applied -- a synthetic star is asked for by
    // name and must not be filtered away silently.
    bool load_synthetic(const std::vector<math::Vec3>& directions,
                        const std::vector<double>& temperatures,
                        const std::vector<double>& magnitudes);

    [[nodiscard]] bool is_ready() const { return sky_ != nullptr; }
    [[nodiscard]] const std::string& last_error() const { return last_error_; }
    [[nodiscard]] std::string describe_catalogue() const;
    [[nodiscard]] int star_count() const;

    // Drop everything fainter than this before building anything: a presentation
    // choice, and BSC5 reaches V = 7.96, two magnitudes past the naked eye.
    // Must be called BEFORE load_catalogue to take effect on the next load.
    void set_magnitude_limit(double limit) { magnitude_limit_ = limit; }
    [[nodiscard]] double magnitude_limit() const { return magnitude_limit_; }

    // Exposure: the luminance that reads half scale
    // (core/render/tone_response.hpp).
    bool set_half_saturation(double half_saturation);
    [[nodiscard]] double half_saturation() const { return half_saturation_; }

    // The colour table: rgb is the linear-sRGB chromaticity of a black body,
    // alpha is ln(band efficiency).  Built by the core, never by hand
    // (docs/architecture/relativistic-shaders.md section 3.2).
    [[nodiscard]] const render::PlanckTable* planck_table() const;
    [[nodiscard]] double planck_table_reference_temperature() const;

    // One pass of optics.hpp over the whole catalogue.  `beta` is the observer's
    // velocity over c, in the coordinate frame.
    void update(const math::Vec3& beta, double sky_radius, bool aberration = true,
                bool doppler = true, bool beaming = true);

    // The per-star instance data for the renderer.  Four FLOATS of custom data,
    // not four bytes: a temperature of 25944 K quantised into [0,1] would turn
    // the whole effect into a uniform grey with no error anywhere.
    [[nodiscard]] std::vector<StarVertex> vertices() const;

    // What section 3 and section 10 of the physics document claim, measured on
    // the data that was just drawn.
    [[nodiscard]] SkyDiagnosticsView diagnostics() const;

    // The same, for a speed the ship is not travelling at.  Labelled as a
    // projection wherever it is printed: it is what the optics WOULD do, run
    // through the same code, and it leaves both the state and the drawn frame
    // exactly as it found them.
    [[nodiscard]] SkyDiagnosticsView diagnostics_at(const math::Vec3& beta);

    // The Doppler factor for one direction on the sky, at the beta of the last
    // update().  Exists so that a camera can report what it is pointing into --
    // turning the head becomes a measurement -- without the presentation ever
    // computing gamma (1 - beta.n) itself.
    //
    // `to_source` is in SCENE axes, which are the coordinate frame's axes: the
    // RenderTransform translates and scales and never rotates
    // (docs/architecture/rendering.md section 2).
    [[nodiscard]] double doppler_in_direction(const math::Vec3& to_source) const;

    // ---- the oracle side of the validation harness -------------------------
    //
    // Every number the GPU is checked against comes from here, which is to say
    // from core/: the harness measures a screenshot and compares it with what the
    // shipped C++ says the answer is.  A screenshot compared against a human
    // impression is not a test (docs/validation/relativistic-rendering-visual.md
    // section 2).
    //
    // Out-of-range indices THROW: six call sites silently returning zero would
    // be six ways for a harness to conclude the GPU is right when nothing was
    // compared at all.
    [[nodiscard]] math::Vec3 apparent_direction(int index) const;
    [[nodiscard]] math::Vec3 rest_direction(int index) const;
    [[nodiscard]] double expected_response(int index) const;
    [[nodiscard]] LinearColour expected_colour(int index) const;
    [[nodiscard]] double doppler_of(int index) const;
    [[nodiscard]] double beaming_of(int index) const;
    [[nodiscard]] double star_temperature(int index) const;
    [[nodiscard]] double star_magnitude(int index) const;

    [[nodiscard]] const math::Vec3& beta() const { return beta_; }
    [[nodiscard]] double sky_radius() const { return sky_radius_; }

private:
    [[nodiscard]] std::size_t checked_index(int index, const char* what) const;

    std::unique_ptr<render::RelativisticSky> sky_;
    math::Vec3 beta_{};
    double sky_radius_{1.0};
    double magnitude_limit_{7.0};
    double half_saturation_{render::kDefaultHalfSaturation};
    std::string last_error_;
};

}  // namespace sf::app
