#include "app/presentation/instruments/nav_display.hpp"

#include "app/presentation/format.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace sf::app {
namespace {

constexpr double kTau = 2.0 * std::numbers::pi;

}  // namespace

bool NavDisplay::apsides_resolved(double spread_m, double extent_scene, double render_scale) {
    if (render_scale <= 0.0 || extent_scene <= 0.0) {
        return false;
    }
    const double ulp_m = extent_scene * FLOAT32_EPSILON / render_scale;
    return spread_m > APSIS_NOISE_GAIN * ulp_m / (APSIS_STABILITY_DEG * std::numbers::pi / 180.0);
}

void NavDisplay::paint() {
    title_ = "NAVIGATION";
    draw_frame();
    if (!data_->present) {
        draw_text_at(size_ * 0.5, "NO DATA", 6.0, palette::DIM, Align::Centre);
        return;
    }

    const auto& track = data_->orbit_track;
    const Vec3 centre_3d = data_->reference_position;
    const Vec3 ship_3d = data_->ship_position;

    if (track.size() < 8) {
        draw_numbers_only();
        return;
    }

    // Scale: the farthest point of the orbit fits in the box, with a margin.
    double extent = 0.0;
    for (const auto& point : track) {
        extent = std::max(extent, (point - centre_3d).norm());
    }
    extent = std::max(extent, (ship_3d - centre_3d).norm());
    if (extent <= 0.0) {
        draw_numbers_only();
        return;
    }

    const bool bound = data_->bound;
    const bool resolved =
        apsides_resolved(std::abs(data_->apoapsis_altitude_m - data_->periapsis_altitude_m), extent,
                         data_->render_scale);

    const auto frame = plane_from(track, centre_3d, resolved, bound);
    const Vec3 u = frame[0];
    const Vec3 v = frame[1];

    // The drawing's radius has to fit in BOTH directions.
    //
    // ⚠️ The previous formula was `min(width, height x 1.35) x 0.38`, which on a
    // 480x315 panel gives a 161 px radius with 148 px available above the
    // centre: the orbit ran off the top and the bottom of the display and the
    // ship marker VANISHED for the whole part of the lap it spent there. On a
    // navigation display, "where is the ship" is the question.
    const Vec2 origin{size_.x * 0.5, size_.y * 0.47};
    const double half = std::min(size_.x * 0.5, std::min(origin.y, size_.y - origin.y));
    const double box = half * (1.0 - MARGIN * 2.0);
    const auto to_screen = [&](const Vec3& p) {
        const Vec3 d = p - centre_3d;
        return origin + Vec2{dot(d, u), -dot(d, v)} / extent * box;
    };

    draw_central_body(origin, extent, box);
    draw_reference_banner();

    // The orbit. One line per segment, because a hyperbola comes with widely
    // spaced ends and a long polyline antialiases worse than segments.
    Vec2 previous = to_screen(track[0]);
    for (std::size_t i = 1; i < track.size(); ++i) {
        const Vec2 current = to_screen(track[i]);
        canvas_->line(previous, current, palette::NAV, std::max(unit() * 0.30, 1.0));
        previous = current;
    }

    draw_apsides(origin, extent, box, resolved, bound);
    draw_ship(to_screen(ship_3d), origin);
    draw_readouts();
}

std::array<Vec3, 2> NavDisplay::plane_from(const std::vector<Vec3>& track, const Vec3& centre, bool resolved,
                                           bool bound) {
    // The normal first, from the cross product of two well-separated radii:
    // neighbouring points are almost parallel and their cross product is noise.
    const std::size_t quarter = track.size() / 4;
    Vec3 w = cross(track[0] - centre, track[quarter] - centre);
    if (w.norm() < 1.0e-12) {
        w = Vec3{0.0, 0.0, 1.0};
    }
    w = w.normalized();

    Vec3 u{};
    if (resolved && bound) {
        // The periapsis, from ALL the samples: the ellipse then always comes out
        // in the same orientation and does not rotate under the pilot's eyes
        // every frame, which a `u` taken from "the first point of the sample"
        // would do on a resampled orbit.
        u = periapsis_from_centroid(track, centre);
    } else if (resolved) {
        // Hyperbola: the mean does not work, because the arc is a piece and not a
        // lap. It is not needed either -- a hyperbola's periapsis is a sharp
        // corner, the radius around it changes fast, and the extreme of the
        // sample hits it.
        std::size_t best = 0;
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < track.size(); ++i) {
            const double distance = (track[i] - centre).norm();
            if (distance < nearest) {
                nearest = distance;
                best = i;
            }
        }
        u = (refined_apsis(track, centre, best) - centre).normalized();
    } else {
        // In a circular orbit there IS no periapsis, and taking `u` from the
        // nearest point of the sample is taking it from the rounding: the whole
        // drawing rotated up to 37 degrees per frame. A rotating circle is not
        // noticed -- but the ship marker on it jumped with it.
        //
        // A FIXED world axis, projected onto the plane of the orbit. Which one is
        // irrelevant (the drawing is a circle, it has no orientation to get
        // right); what matters is that it is the SAME one next frame.
        for (const Vec3& axis : {Vec3{0.0, 0.0, 1.0}, Vec3{1.0, 0.0, 0.0}}) {
            const Vec3 projected = axis - w * dot(axis, w);
            if (projected.norm() > 1.0e-3) {
                u = projected.normalized();
                break;
            }
        }
    }
    const Vec3 v = cross(w, u).normalized();
    return {u, v};
}

Vec3 NavDisplay::periapsis_from_centroid(const std::vector<Vec3>& track, const Vec3& centre) {
    // The direction of the periapsis, from ALL the samples and not the two
    // nearest.
    //
    // orbit_track samples uniformly in TRUE ANOMALY, and for that sampling the
    // mean of the positions falls at -p e/2 along the periapsis direction:
    // proportional to the eccentricity, and -- being a mean of N points -- with
    // each sample's noise divided by the root of N.
    //
    // ⚠️ Looking for the NEAREST point was the opposite. Near an apsis the radius
    // is stationary, so the quantity that tells the apsis from its neighbours is
    // the smallest on the whole curve -- the worst possible place to look for an
    // extreme in a noisy signal. Measured, between frames, on a 400 km orbit with
    // 7 to 19 km between the apsides: the extreme jumped 0.24 to 1.75 degrees;
    // the mean, 0.017 to 0.033. Fifty to a hundred times less, and what is left
    // is real precession.
    //
    // The last point REPEATS the first (nu = -pi and nu = +pi are the same place
    // on the orbit): counting it twice puts an extra apoapsis in the mean.
    const std::size_t count = std::max<std::size_t>(track.size() - 1, 1);
    Vec3 sum{};
    for (std::size_t i = 0; i < count; ++i) {
        sum += track[i] - centre;
    }
    const Vec3 offset = sum / static_cast<double>(count);
    if (offset.norm() < 1.0e-12) {
        return (track[0] - centre).normalized();
    }
    // The mean falls on the APOAPSIS side; the periapsis is the other one.
    return (offset * -1.0).normalized();
}

Vec3 NavDisplay::refined_apsis(const std::vector<Vec3>& track, const Vec3& centre, std::size_t at) {
    // The apsis point, BETWEEN the samples and not on one of them. Only for the
    // hyperbola: the closed orbit uses the mean, which is better.
    //
    // ⚠️ orbit_track samples uniformly in TRUE ANOMALY from -pi to +pi, and the
    // periapsis is at nu = 0 -- which with 128 samples falls at index 63.5,
    // EXACTLY between two. Which of the two wins the `<` is decided by rounding,
    // and the drawing's axis jumped 2.83 degrees (one sample spacing) from frame
    // to frame.
    //
    // The radius near an apsis is a parabola in the index, so the vertex comes
    // out of three points in closed form. With the minimum half-way, both
    // candidate indices give the SAME vertex -- 63.5 -- and the jump disappears
    // by construction.
    if (at == 0 || at + 1 >= track.size()) {
        return track[at];
    }
    const double before = (track[at - 1] - centre).norm();
    const double middle = (track[at] - centre).norm();
    const double after = (track[at + 1] - centre).norm();
    const double curvature = before - 2.0 * middle + after;
    if (std::abs(curvature) < 1.0e-12) {
        return track[at];
    }
    const double offset = std::clamp(0.5 * (before - after) / curvature, -1.0, 1.0);
    const Vec3& neighbour = offset >= 0.0 ? track[at + 1] : track[at - 1];
    // Linear interpolation between the two samples: near an apsis the radius is
    // stationary, so chord and arc differ by less than a pixel of this display.
    return track[at] + (neighbour - track[at]) * std::abs(offset);
}

void NavDisplay::draw_central_body(Vec2 origin, double extent, double box) {
    const double radius = data_->reference_radius;
    const double screen_radius = radius / extent * box;
    const Colour colour = data_->reference_colour;
    const double r = std::max(screen_radius, unit() * 1.2);
    canvas_->circle(origin, r, colour.darkened(0.55F));
    canvas_->arc(origin, r, 0.0, kTau, 48, colour, std::max(unit() * 0.28, 1.0));
    // No label under the planet: the banner already says it, at the top left,
    // and says it better.
}

void NavDisplay::draw_reference_banner() {
    // Rule 64: say, unambiguously, against WHICH body the numbers are read.
    //
    // On an Earth-Moon flight the answer never changes and the label under the
    // planet sufficed. On an interplanetary cruise it changes three times --
    // Earth, Sun, Mars -- and "AP 402 km" without the body next to it is not a
    // number, it is a number and a guess.
    const double u = unit();
    const std::string reference = fmt::upper(data_->s.reference.empty() ? std::string{"?"} : data_->s.reference);
    draw_text_at(Vec2{u * 3.0, u * 12.0}, "REFERENCE", 3.4, palette::DIM);
    draw_text_at(Vec2{u * 3.0, u * 17.5}, reference, 5.0, reference == "SUN" ? palette::WARNING : palette::SECONDARY);
}

void NavDisplay::draw_apsides(Vec2 origin, double extent, double box, bool resolved, bool bound) {
    // The apsides are on the LINE OF APSIDES, which is the drawing's `u` axis,
    // and how far out they are comes from the snapshot, in double precision. So
    // there is nothing to search for: the periapsis is at +r_pe along `u` and
    // the apoapsis at -r_ap.
    //
    // When the two do not separate enough for the direction to be knowable,
    // there is no "where": the orbit is circular and the display says so
    // instead of pointing at a random place. A hyperbola escapes the rule
    // because its periapsis is always sharp -- what it does not have is an
    // apoapsis, and `bound` deals with that.
    if (bound && !resolved) {
        draw_circular_note();
        return;
    }
    const double scale = data_->render_scale;
    if (bound) {
        const double apoapsis = data_->s.apoapsis_m * scale;
        apsis(origin + Vec2{-apoapsis / extent * box, 0.0}, "AP");
        apsis_readout(Vec2{unit() * 3.0, unit() * 27.0}, "AP", fmt::distance(data_->apoapsis_altitude_m), Align::Left);
    }
    const double periapsis = data_->s.periapsis_m * scale;
    apsis(origin + Vec2{periapsis / extent * box, 0.0}, "PE");
    apsis_readout(Vec2{size_.x - unit() * 3.0, unit() * 27.0}, "PE", fmt::distance(data_->periapsis_altitude_m),
                  Align::Right);
}

void NavDisplay::draw_circular_note() {
    // The answer that replaces the two markers: in the place of "AP", in the same
    // shape and colour, because whoever was looking for the apoapsis is the one
    // who has to find there why it is not there.
    apsis_readout(Vec2{unit() * 3.0, unit() * 27.0}, "CIRCULAR", fmt::distance(data_->s.altitude_m), Align::Left);
}

void NavDisplay::apsis(Vec2 at, const std::string& label) {
    // The marker says WHERE, and only that. The number sits in the readout: next
    // to the dot it was too small to read without leaving the seat.
    const double r = std::max(unit() * 1.6, 2.0);
    canvas_->circle(at, r, palette::PLAN);
    draw_text_at(at + Vec2{r * 2.0, -r}, label, 3.8, palette::PLAN);
}

void NavDisplay::apsis_readout(Vec2 at, const std::string& label, const std::string& value, Align align) {
    // LEFT the apoapsis and RIGHT the periapsis, because that is where they are
    // drawn: the drawing's axis is the line of apsides with the periapsis at +x.
    draw_text_at(at, label, 3.6, palette::SECONDARY, align);
    draw_text_at(at + Vec2{0.0, unit() * 6.0}, value, 5.0, palette::PLAN, align);
}

void NavDisplay::draw_ship(Vec2 at, Vec2 origin) {
    const double r = std::max(unit() * 2.0, 3.0);
    canvas_->circle(at, r, palette::PRIMARY);
    // The direction of travel: the arrow points along the projected velocity,
    // and the projected velocity is perpendicular to the radius in the sense of
    // the motion. It is taken from the drawing's geometry -- the tangent -- and
    // not from a converted vector, because it is the sense IN THE DRAWING that
    // has to be right.
    const Vec2 radial = (at - origin).normalized();
    Vec2 tangent{-radial.y, radial.x};
    if (data_->retrograde_orbit) {
        tangent = -tangent;
    }
    const double w = std::max(unit() * 0.3, 1.0);
    canvas_->line(at, at + tangent * (r * 4.0), palette::PRIMARY, w);
    const Vec2 head = at + tangent * (r * 4.0);
    canvas_->line(head, head - tangent * (r * 1.6) + radial * r, palette::PRIMARY, w);
    canvas_->line(head, head - tangent * (r * 1.6) - radial * r, palette::PRIMARY, w);
}

void NavDisplay::draw_readouts() {
    const double left = unit() * 2.5;
    const double bottom = size_.y - unit() * 2.0;
    draw_text_at(Vec2{left, bottom - unit() * 10.0}, "ALT", 3.6, palette::SECONDARY);
    draw_text_at(Vec2{left, bottom - unit() * 5.0}, fmt::distance(data_->s.altitude_m), 5.0, palette::PRIMARY);

    draw_text_at(Vec2{size_.x - left, bottom - unit() * 10.0}, "ECC / INC", 3.6, palette::SECONDARY, Align::Right);
    draw_text_at(Vec2{size_.x - left, bottom - unit() * 5.0},
                 fmt::format("%.4f / %.2f°", data_->s.eccentricity, data_->s.inclination_deg), 5.0,
                 palette::PRIMARY, Align::Right);

    const double period = data_->s.period_s;
    if (period > 0.0) {
        draw_text_at(Vec2{size_.x * 0.5, bottom - unit() * 5.0}, "T " + fmt::duration(period), 4.2,
                     palette::SECONDARY, Align::Centre);
    }
}

void NavDisplay::draw_numbers_only() {
    // Without a trajectory there is no drawing, but there are numbers -- and the
    // alternative, an empty box, would hide that the ship is still somewhere.
    draw_field(Vec2{unit() * 3.0, unit() * 18.0}, "ALTITUDE", fmt::distance(data_->s.altitude_m), 7.0);
    draw_field(Vec2{unit() * 3.0, unit() * 38.0}, "APOAPSIS", fmt::distance(data_->apoapsis_altitude_m), 7.0);
    draw_field(Vec2{unit() * 3.0, unit() * 58.0}, "PERIAPSIS", fmt::distance(data_->periapsis_altitude_m), 7.0);
    draw_field(Vec2{unit() * 3.0, unit() * 78.0}, "ECCENTRICITY", fmt::format("%.5f", data_->s.eccentricity), 7.0);
}

}  // namespace sf::app
