#pragma once

// SpaceflightSky: the relativistic star field, as much of it as Godot gets.
//
// Deliberately a SEPARATE class from SpaceflightSimulation.  The simulation owns
// the state; this owns a catalogue and a colour table, and the only thing that
// passes between them is a velocity.  GDScript carries that vector and computes
// nothing with it -- which is the rule of this layer
// (docs/architecture/relativistic-shaders.md section 6).
//
// What crosses into the engine, per frame:
//
//   ARRAY_VERTEX   aberrated direction x sky radius   <- core/relativity/optics.hpp
//   ARRAY_CUSTOM0  (T_rest, F_rest, D, 0)             <- core/relativity/optics.hpp
//
// and once, at load, the Planck table as an RGBAF image.  The shader turns D into
// colour and brightness and never learns what beta is.

#include "core/render/relativistic_sky.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <memory>

namespace spaceflight_godot {

class SpaceflightSky : public godot::Node {
    GDCLASS(SpaceflightSky, godot::Node)

public:
    SpaceflightSky();
    ~SpaceflightSky() override;

    // Reads BSC5.  An empty path means "wherever the build was told the
    // catalogues live", which is what a run from the editor wants.
    bool load_catalogue(const godot::String& path);
    [[nodiscard]] bool is_ready() const { return sky_ != nullptr; }
    godot::String get_last_error() const;
    godot::String describe_catalogue() const;

    int get_star_count() const;

    // Drop everything fainter than this before building anything: a presentation
    // choice, and BSC5 reaches V = 7.96, two magnitudes past the naked eye.
    // Must be called BEFORE load_catalogue to take effect on the next load.
    void set_magnitude_limit(double limit);
    double get_magnitude_limit() const;

    // Exposure: the luminance that reads half scale
    // (core/render/tone_response.hpp).
    void set_half_saturation(double half_saturation);
    double get_half_saturation() const;

    // The colour table, as a width x 1 RGBAF image: rgb is the linear-sRGB
    // chromaticity of a black body, alpha is ln(band efficiency).  Built by the
    // core, never by hand (docs/architecture/relativistic-shaders.md section 3.2).
    godot::Ref<godot::Image> get_planck_table_image() const;
    double get_planck_table_reference_temperature() const;

    // One pass of optics.hpp over the whole catalogue.  `beta` is the observer's
    // velocity over c, in the coordinate frame.
    void update_sky(const godot::Vector3& beta, double sky_radius);

    // The arrays for ArrayMesh::add_surface_from_arrays, plus the surface format
    // flags that make CUSTOM0 four floats instead of four bytes -- without which
    // a temperature of 25944 K would be quantised into [0,1] and the whole effect
    // would come out as a uniform grey.
    godot::Dictionary get_surface_arrays() const;

    // What section 3 and section 10 of the physics document claim, measured on
    // the data that was just drawn.
    godot::Dictionary get_diagnostics() const;

    // The same, for a speed the ship is not travelling at.  Labelled as a
    // projection wherever it is printed: it is what the optics WOULD do, run
    // through the same code, and it leaves both the state and the drawn frame
    // exactly as it found them.
    godot::Dictionary get_diagnostics_at(const godot::Vector3& beta);

protected:
    static void _bind_methods();

private:
    std::unique_ptr<sf::render::RelativisticSky> sky_;
    sf::math::Vec3 beta_{};
    double sky_radius_{1.0};
    double magnitude_limit_{7.0};
    double half_saturation_{sf::render::kDefaultHalfSaturation};
    std::string last_error_;
};

}  // namespace spaceflight_godot
