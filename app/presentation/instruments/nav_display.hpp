#pragma once

// The navigation display (rule 20).
//
// Draws the orbit, and the points of the orbit come from the core:
// FlightSession::orbit_track() samples trajectory::state_from_elements over the
// osculating elements. This file does NOT solve Kepler, integrate anything or
// hold a second opinion about the shape of the ellipse (rules 54 and 77). It
// projects points onto a plane and draws lines between them.
//
// ## The projection
//
// The drawing plane is the plane of the orbit itself, built from two points of
// the track received. Seen from above the orbit, then -- the view in which
// apoapsis, periapsis and where the ship is on the lap are all readable at once.
// A fixed projection (equatorial, say) would hide the eccentricity of an
// inclined orbit behind foreshortening, which is precisely the number wanted.

#include "app/presentation/instruments/instrument.hpp"

#include <array>

namespace sf::app {

class NavDisplay final : public Instrument {
public:
    static constexpr double MARGIN = 0.12;

    // The 32-bit float epsilon, the apsis estimator's noise gain, and how much
    // the marker may move between frames without it being noticed.
    //
    // ⚠️ The orbit points arrive here in SCENE UNITS and in 32-bit float. In a
    // nearly circular orbit the difference between the radius at apoapsis and at
    // periapsis is of the order of that float's ulp, and the direction of the
    // apsis is no longer in the data.
    //
    // `APSIS_NOISE_GAIN` is MEASURED, not derived: the estimator's angular error
    // falls as 1/separation, and over a 400 km parking orbit (ulp 0.8 m) the
    // product (error x separation) stays between 224 and 320 degree-metres over
    // the whole range from 7 m to 9 km of separation. In radians and per ulp
    // that is 6.5:
    //
    //     error ~ 6.5 x ulp / separation
    //
    // One degree is the limit because it is what is seen: on this display the
    // ring is 110 px in radius, and a degree is two pixels.
    static constexpr double FLOAT32_EPSILON = 1.1920929e-7;
    static constexpr double APSIS_NOISE_GAIN = 6.5;
    static constexpr double APSIS_STABILITY_DEG = 1.0;

    // Is the apsides' separation larger than what the drawing can resolve?
    //
    // `spread_m` is (apoapsis - periapsis) in metres, from the snapshot, which is
    // exact. `extent_scene` is the orbit's radius in scene units, which is where
    // the 32-bit float is. The question is how much the marker would shake,
    // compared with what is seen, and it is the only thing that decides whether
    // this display has the right to point at an apsis.
    //
    // Scale-free on purpose: it does not ask "is the eccentricity small?", which
    // would depend on the body, but "does the number I want to draw survive the
    // precision it reached me with?", which holds equally at the Earth, at the
    // Moon and around the Sun.
    [[nodiscard]] static bool apsides_resolved(double spread_m, double extent_scene, double render_scale);

    // Two orthonormal axes in the plane of the orbit, from the points received:
    // [u (towards periapsis when it is known), v].
    [[nodiscard]] static std::array<Vec3, 2> plane_from(const std::vector<Vec3>& track, const Vec3& centre,
                                                        bool resolved, bool bound = true);

protected:
    void paint() override;

private:
    [[nodiscard]] static Vec3 periapsis_from_centroid(const std::vector<Vec3>& track, const Vec3& centre);
    [[nodiscard]] static Vec3 refined_apsis(const std::vector<Vec3>& track, const Vec3& centre, std::size_t at);

    void draw_central_body(Vec2 origin, double extent, double box);
    void draw_reference_banner();
    void draw_apsides(Vec2 origin, double extent, double box, bool resolved, bool bound);
    void draw_circular_note();
    void apsis(Vec2 at, const std::string& label);
    void apsis_readout(Vec2 at, const std::string& label, const std::string& value, Align align);
    void draw_ship(Vec2 at, Vec2 origin);
    void draw_readouts();
    void draw_numbers_only();
};

}  // namespace sf::app
