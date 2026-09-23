#pragma once

// The four cockpit displays and the minimal HUD.

#include "app/presentation/instruments/instrument.hpp"

#include <array>
#include <utility>

namespace sf::app {

// The primary flight display (rule 18).
//
// It has NO artificial horizon, and the reason is in rule 18: a horizon is the
// line where the ground meets the sky, and in orbit there is neither. What
// exists is where the nose points and where the directions that matter are, and
// that is what this display draws.
//
// ## The projection
//
// The centre is the NOSE. Each direction is taken into the body frame, and the
// angle between it and the nose becomes a radius: an azimuthal equidistant
// projection, in which the distance from the centre IS the pointing error in
// degrees. A marker at half radius is at half the maximum angle, with no
// interpretation.
//
// Right on screen is -y of the body and up is +z, which is exactly how the
// cockpit camera is mounted (CameraRig::COCKPIT_ALIGN). The two have to agree: a
// marker that appears on the right of the display and on the left through the
// window is worse than no marker.
//
// Directions behind -- more than 90 degrees from the nose -- are drawn on the
// rim, faded, instead of being dropped. "It is behind you" is information.
class FlightDisplay final : public Instrument {
public:
    static constexpr double FIELD_OF_VIEW_DEG = 90.0;

    // Integration frame -> body -> screen. Returns the point and whether the
    // direction is behind the nose.
    [[nodiscard]] static std::pair<Vec2, bool> project(const Vec3& direction, const Basis& basis, Vec2 centre,
                                                       double radius);

protected:
    void paint() override;

private:
    void draw_rose(Vec2 centre, double radius);
    void draw_markers(Vec2 centre, double radius);
    void draw_nose(Vec2 centre, double radius);
    void draw_rates();
};

// The target display (rule 21).
//
// Distance, relative speed, relative direction, and the intercept when there is
// one. The "estimated intercept" is a LINEAR EXTRAPOLATION -- distance to close
// over the rate at which it closes -- and it is labelled as such on the display,
// because it is not a trajectory. When a plan is armed the display shows ITS
// arrival instead; the difference between the two is large, because the
// extrapolation ignores gravity entirely.
class TargetDisplay final : public Instrument {
protected:
    void paint() override;

private:
    void draw_bearing(Vec2 centre, double radius);
    void draw_intercept(Vec2 at, double distance, double closing);
};

// The systems display: engine, propellant, RCS, relativity (rules 14, 15, 19, 22).
//
// It is the panel's wide strip -- 1280 x 122, almost eleven to one -- and the
// layout is written FOR that shape instead of inherited from the square
// displays. Everything here is a fraction of the real height.
//
// Relativity has one cell and only one. Rule 22 asks for it to appear as a real
// instrument and NOT to occupy half the cockpit in conventional flight; at 1e-4 c
// it fits in a line, and that is what it deserves.
class SystemDisplay final : public Instrument {
public:
    static constexpr int COLUMNS = 6;

protected:
    void paint() override;

private:
    void label(Vec2 at, const std::string& text, double h, Colour colour = palette::SECONDARY);
    void value(Vec2 at, const std::string& text, double h, Colour colour = palette::PRIMARY,
               double scale = 0.175);
    void cell(double x, double width, double h, const std::string& label, const std::string& value,
              Colour colour = palette::PRIMARY);
    void engine(double x, double width, double h);
    void thrust(double x, double width, double h);
    void propellant(double x, double width, double h);
    void mass(double x, double width, double h);
    void rcs(double x, double width, double h);
    void relativity(double x, double width, double h);
};

// The minimal HUD (rule 38).
//
// For the external views and for whoever does not want the cockpit: a strip at
// the bottom with what is flown and a corner with the mission. Nothing more.
//
// The hierarchy is deliberate and is the subject of rule 38: speed, altitude
// and throttle in large type; the rest small and grey. A HUD in which
// everything weighs the same forces the pilot to READ, and reading is what a
// pilot has no time for.
class MinimalHud final : public Instrument {
public:
    static constexpr double BAND_HEIGHT = 0.10;   // fraction of the screen height

protected:
    void paint() override;
    // The HUD is sized by the SCREEN height and not by the control's: it covers
    // the whole screen, and a unit taken from its height would give ten-centimetre
    // letters.
    [[nodiscard]] double unit() const override;

private:
    void big(Vec2 at, const std::string& label, const std::string& value, Colour colour = palette::PRIMARY);
    void throttle(Vec2 at, double width, double u);
    void corner_top_left();
    void corner_top_right();
};

}  // namespace sf::app
