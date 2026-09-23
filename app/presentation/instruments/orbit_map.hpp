#pragma once

// The orbital map (rules 20, 53, 54, 55).
//
// Everything this file draws came ready from the core:
//
//   orbit_track()          the ship's osculating ellipse
//   body_orbit_track()     the target's path, sampled from the EPHEMERIS
//   planned_trajectory()   the arc the planner flew
//   maneuvers()            where each burn lights
//
// The map projects points and draws lines. It does not integrate, does not
// solve Kepler, does not propagate and has no opinion about where the ship is
// going (rule 54).

#include "app/presentation/instruments/instrument.hpp"

#include <map>

namespace sf::app {

class OrbitMap final : public Instrument {
public:
    static constexpr double MARGIN = 0.10;

    // The two modes (rule 26).
    //
    //   Local    centred on the body the ship orbits, in scene units, through the
    //            floating origin. The Milestone 7 map, untouched.
    //   System   centred on the Sun, in METRES, from system_map().
    //
    // Two modes and not a continuous scale, because it is not the scale that
    // changes: it is the FRAME. A map anchored on the Earth and one anchored on
    // the Sun answer different questions, and the Earth travels 500 million
    // kilometres while the ship goes to Mars.
    enum class Mode { Local, System };

    Mode mode{Mode::Local};
    double zoom{1.0};
    // Filled by the orchestrator when the mode is System.
    SystemMap system{};
    std::map<std::string, SystemOrbitPath> system_paths{};

    // "%g" did not exist in the Godot formatter, and the first capture of the
    // solar-system map came out with "%.3g AU" on every scale ring. The decimal
    // places are chosen by magnitude, which is what %g would do.
    [[nodiscard]] static std::string au_label(double au);
    [[nodiscard]] static double nice_step(double target);

protected:
    void paint() override;
    [[nodiscard]] double unit() const override;

private:
    void plane(const std::vector<Vec3>& track, const std::vector<Vec3>& plan);
    [[nodiscard]] Vec2 screen(const Vec3& p) const;
    void polyline(const std::vector<Vec3>& points, Colour colour, double width_units);
    [[nodiscard]] bool near_screen(Vec2 at) const;
    void draw_grid();
    void draw_body(const Vec3& at, double radius, const std::string& name, Colour colour);
    void draw_maneuvers();
    void draw_ship();
    void draw_legend();

    void draw_system();
    [[nodiscard]] double log_zoom() const;
    void draw_system_grid();
    [[nodiscard]] bool label_fits(Vec2 at, const std::vector<Vec2>& placed) const;
    void draw_system_anchors();
    void draw_anchor(const Vec3& at, const std::string& label, Colour colour);
    void draw_system_maneuvers();
    void draw_system_legend();

    Vec3 u_{1.0, 0.0, 0.0};
    Vec3 v_{0.0, 1.0, 0.0};
    Vec2 origin_{};
    double box_{1.0};
    Vec3 centre_{};
    double extent_{1.0};
};

}  // namespace sf::app
