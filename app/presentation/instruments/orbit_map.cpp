#include "app/presentation/instruments/orbit_map.hpp"

#include "app/presentation/format.hpp"
#include "app/presentation/input_actions.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sf::app {
namespace {

constexpr double kTau = 2.0 * std::numbers::pi;
constexpr double kAu = 1.495978707e11;

}  // namespace

double OrbitMap::unit() const { return std::max(size_.y / 100.0, 1.0) * 0.55; }

void OrbitMap::paint() {
    // Opaque, no alpha at all. At 82 % the cockpit showed through and the orbit
    // lines crossed the text of the displays; at 98.5 % the panel still read
    // through the map, because the panel is light and the map is dark. A map
    // read over another display does not read. What the map covers, it covers on
    // purpose; `M` closes it.
    canvas_->rect(Rect2{{0.0, 0.0}, size_}, Colour{0.016F, 0.020F, 0.027F}, true, 1.0);
    if (mode == Mode::System) {
        draw_system();
        return;
    }
    if (!data_->present) {
        return;
    }

    const auto& track = data_->orbit_track;
    const auto& plan = data_->planned_trajectory;
    const auto& moon = data_->target_track;
    centre_ = data_->reference_position;

    extent_ = 0.0;
    for (const auto* set : {&track, &plan, &moon}) {
        for (const auto& point : *set) {
            extent_ = std::max(extent_, (point - centre_).norm());
        }
    }
    extent_ = std::max(extent_, (data_->ship_position - centre_).norm());
    if (extent_ <= 0.0) {
        return;
    }
    extent_ /= zoom;

    plane(track, plan);
    box_ = std::min(size_.x, size_.y) * (0.5 - MARGIN);
    origin_ = Vec2{size_.x * 0.5, size_.y * 0.5};

    draw_grid();
    draw_body(centre_, data_->reference_radius, data_->s.reference.empty() ? "?" : data_->s.reference,
              palette::NAV_DIM);
    if (moon.size() > 2) {
        polyline(moon, palette::TARGET.darkened(0.45F), 0.22);
    }
    if (plan.size() > 2) {
        polyline(plan, palette::PLAN, 0.30);
    }
    if (track.size() > 2) {
        polyline(track, palette::NAV, 0.26);
    }
    if (data_->has_target) {
        draw_body(data_->target_position, data_->target_radius,
                  data_->s.target.empty() ? "TARGET" : data_->s.target, palette::TARGET);
    }
    draw_maneuvers();
    draw_ship();
    draw_legend();
}

void OrbitMap::plane(const std::vector<Vec3>& track, const std::vector<Vec3>& plan) {
    // The drawing plane: the one that holds the central body, the SHIP and the
    // TARGET.
    //
    // ⚠️ The first version used the plane of the current orbit, and that is wrong
    // for a transfer map for a simple geometric reason: the parking orbit is
    // inclined 51.6 degrees, the Moon moves near the ecliptic, and projecting a
    // point on a plane loses the normal component. With the Moon almost on the
    // orbit's normal, its marker collapsed onto the Earth -- 384 000 km away,
    // drawn as zero.
    //
    // Building the plane from the ship and the target guarantees that neither is
    // foreshortened: both are IN the plane by construction. What is lost is the
    // relative inclination between them, which this map does not promise to
    // show.
    Vec3 to_ship = data_->ship_position - centre_;
    if (to_ship.norm() < 1.0e-9) {
        to_ship = Vec3{1.0, 0.0, 0.0};
    }
    Vec3 second{};
    if (data_->has_target) {
        second = data_->target_position - centre_;
    }
    if (second.norm() < 1.0e-9) {
        // No target: a point a quarter of the way round whatever trajectory
        // exists, which gives the orbit's normal without being almost parallel
        // to the first.
        const auto& source = track.size() > 8 ? track : plan;
        if (source.size() >= 8) {
            second = source[source.size() / 4] - centre_;
        }
    }
    u_ = to_ship.normalized();
    Vec3 w = cross(u_, second);
    if (w.norm() < 1.0e-12) {
        // Ship and target aligned with the centre: any plane that holds them will
        // do, and the choice only rotates the drawing.
        w = cross(u_, Vec3{0.0, 0.0, 1.0});
        if (w.norm() < 1.0e-12) {
            w = cross(u_, Vec3{0.0, 1.0, 0.0});
        }
    }
    v_ = cross(w.normalized(), u_).normalized();
}

Vec2 OrbitMap::screen(const Vec3& p) const {
    const Vec3 d = p - centre_;
    return origin_ + Vec2{dot(d, u_), -dot(d, v_)} / extent_ * box_;
}

void OrbitMap::polyline(const std::vector<Vec3>& points, Colour colour, double width_units) {
    const double w = std::max(unit() * width_units, 1.0);
    Vec2 previous = screen(points[0]);
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec2 current = screen(points[i]);
        // Segments off the box are dropped one by one instead of the whole line:
        // a translunar arc leaves the screen and comes back, and dropping the set
        // would erase the half that matters.
        if (near_screen(previous) || near_screen(current)) {
            canvas_->line(previous, current, colour, w);
        }
        previous = current;
    }
}

bool OrbitMap::near_screen(Vec2 at) const {
    const Vec2 slack = size_ * 0.5;
    return at.x > -slack.x && at.x < size_.x + slack.x && at.y > -slack.y && at.y < size_.y + slack.y;
}

void OrbitMap::draw_grid() {
    // Scale rings, with the radius labelled. Without them the map is pretty and
    // says no distance at all, which is its only job.
    const double step = nice_step(extent_ * 0.45);
    double ring = step;
    while (ring <= extent_ * 1.45) {
        const double radius = ring / extent_ * box_;
        canvas_->arc(origin_, radius, 0.0, kTau, 72, palette::PANEL_EDGE.darkened(0.2F), std::max(unit() * 0.14, 1.0));
        draw_text_at(origin_ + Vec2{radius + unit() * 1.5, -unit() * 1.0}, fmt::distance(ring / data_->render_scale),
                     3.2, palette::DIM);
        ring += step;
    }
}

double OrbitMap::nice_step(double target) {
    // 1, 2 or 5 times a power of ten. A "nice" step is what makes the ring's
    // label a round number, and a round number is what compares at a glance.
    const double exponent = std::floor(std::log(std::max(target, 1.0e-12)) / std::log(10.0));
    const double base = std::pow(10.0, exponent);
    for (const double multiple : {1.0, 2.0, 5.0, 10.0}) {
        if (base * multiple >= target) {
            return base * multiple;
        }
    }
    return base * 10.0;
}

void OrbitMap::draw_body(const Vec3& at, double radius, const std::string& name, Colour colour) {
    const Vec2 p = screen(at);
    const double r = std::max(radius / extent_ * box_, unit() * 1.4);
    canvas_->circle(p, r, colour.darkened(0.6F));
    canvas_->arc(p, r, 0.0, kTau, 48, colour, std::max(unit() * 0.22, 1.0));
    draw_text_at(p + Vec2{0.0, r + unit() * 4.5}, fmt::upper(name), 3.6, colour, Align::Centre);
}

void OrbitMap::draw_maneuvers() {
    // Rule 55: every burn with a marker and an ETA. The data are the plan's; the
    // map decides nothing about when anything lights.
    for (const auto& burn : data_->maneuvers) {
        if (!burn.located) {
            continue;
        }
        const Vec2 at = screen(widen(burn.position));
        const double r = std::max(unit() * 1.6, 2.5);
        const Colour colour = burn.done ? palette::DIM : (burn.active ? palette::CRITICAL : palette::WARNING);
        const double w = std::max(unit() * 0.26, 1.0);
        canvas_->line(at + Vec2{-r * 2.0, 0.0}, at + Vec2{r * 2.0, 0.0}, colour, w);
        canvas_->line(at + Vec2{0.0, -r * 2.0}, at + Vec2{0.0, r * 2.0}, colour, w);
        canvas_->arc(at, r, 0.0, kTau, 20, colour, w);
        draw_text_at(at + Vec2{r * 3.0, -unit() * 0.5}, fmt::upper(burn.name.empty() ? "BURN" : burn.name), 3.6, colour);
        if (!burn.done) {
            draw_text_at(at + Vec2{r * 3.0, unit() * 3.6}, fmt::countdown(burn.seconds_to_ignition), 3.6, colour);
        }
    }
}

void OrbitMap::draw_ship() {
    const Vec2 at = screen(data_->ship_position);
    const double r = std::max(unit() * 1.8, 3.0);
    canvas_->circle(at, r, palette::PRIMARY);
    canvas_->arc(at, r * 2.6, 0.0, kTau, 24, palette::PRIMARY, std::max(unit() * 0.2, 1.0));
}

void OrbitMap::draw_legend() {
    const double u = unit();
    draw_text_at(Vec2{u * 4.0, u * 6.0}, "ORBITAL MAP", 5.0, palette::PRIMARY);
    draw_text_at(Vec2{u * 4.0, u * 12.0},
                 input::label("orbit_map") + " close   wheel zoom   zoom " + fmt::format("%.2fx", zoom), 3.6,
                 palette::SECONDARY);
    struct Entry {
        const char* text;
        Colour colour;
    };
    const Entry legend[] = {{"current orbit", palette::NAV},
                            {"planned transfer", palette::PLAN},
                            {"target path", palette::TARGET.darkened(0.45F)},
                            {"burn", palette::WARNING}};
    double y = size_.y - u * 6.0;
    for (const auto& entry : legend) {
        canvas_->line(Vec2{u * 4.0, y - u * 1.2}, Vec2{u * 10.0, y - u * 1.2}, entry.colour, std::max(u * 0.3, 1.0));
        draw_text_at(Vec2{u * 11.5, y}, entry.text, 3.6, palette::SECONDARY);
        y -= u * 5.5;
    }
}

// ---------------------------------------------------------------------------
// The SOLAR SYSTEM mode (rules 26-31, 71-74).
// ---------------------------------------------------------------------------

void OrbitMap::draw_system() {
    if (!system.valid) {
        draw_text_at(Vec2{unit() * 4.0, unit() * 6.0}, "SOLAR SYSTEM MAP", 5.0, palette::PRIMARY);
        draw_text_at(Vec2{unit() * 4.0, unit() * 14.0}, "no data", 3.6, palette::DIM);
        return;
    }

    const Vec3 ship = system.ship_position;
    const auto& plan = system.planned_trajectory;

    // The drawing plane: the ECLIPTIC.
    //
    // ⚠️ The first version built the plane from the ship and the destination, as
    // the local map does -- and there that is right, because it guarantees
    // neither is foreshortened. Here it is wrong, and the reason is geometric: on
    // a heliocentric map the ship and the destination can be in CONJUNCTION. On
    // 2026-01-01 the Earth and Mars are 178 degrees apart seen from the Sun, the
    // cross product between them is almost null, and the plane left over is
    // whatever out-of-ecliptic component happened to dominate. Measured: the
    // arrival marker, 1.47 AU from the Sun, was drawn at 1.30 -- and the whole arc
    // came out foreshortened.
    //
    // The ecliptic has no such degeneracy and is the plane the Solar System
    // really is in: no planet strays from it by more than 7 degrees.
    u_ = ship.norm() > 1.0e-9 ? ship.normalized() : Vec3{1.0, 0.0, 0.0};
    // The ecliptic's normal in equatorial J2000: the z axis rotated by the
    // obliquity.
    constexpr double obliquity = 0.40909280422232897;   // 23.4393 degrees
    const Vec3 pole{0.0, -std::sin(obliquity), std::cos(obliquity)};
    // `u` projected ONTO the ecliptic, so that the ship stays to the right of the
    // map and the plane is still the ecliptic.
    u_ = (u_ - pole * dot(u_, pole)).normalized();
    v_ = cross(pole, u_).normalized();
    centre_ = Vec3{};

    // The extent: what has to fit. Whole planetary orbits would reach Neptune
    // and crush everything else into a dot, so what decides is the ship, the
    // destination and the planned arc -- and the orbits are clipped to the box.
    extent_ = ship.norm();
    for (const auto& body : system.bodies) {
        if (body.is_destination) {
            extent_ = std::max(extent_, body.position.norm());
        }
    }
    // And the ANCHORS: the destination at arrival can be much farther than it
    // is now, and an arc that leaves the box is an arc that does not read.
    if (system.destination_at_arrival.has_value()) {
        extent_ = std::max(extent_, system.destination_at_arrival->norm());
    }
    for (const auto& point : plan) {
        extent_ = std::max(extent_, point.norm());
    }
    if (extent_ <= 0.0) {
        extent_ = 1.5e11;
    }
    extent_ = extent_ * 1.25 / log_zoom();

    box_ = std::min(size_.x, size_.y) * (0.5 - MARGIN);
    origin_ = Vec2{size_.x * 0.5, size_.y * 0.5};

    draw_system_grid();

    // The planetary orbits, sampled from the EPHEMERIS (rule 29). Drawn first,
    // so they sit under everything.
    for (const auto& [name, row] : system_paths) {
        if (row.path.size() > 2) {
            polyline(row.path, palette::PANEL_EDGE.lightened(0.10F), 0.16);
        }
    }
    if (plan.size() > 2) {
        polyline(plan, palette::PLAN, 0.30);
    }

    // The bodies, and the labels with basic de-cluttering (rule 73).
    std::vector<Vec2> placed;
    for (const auto& body : system.bodies) {
        const Vec2 at = screen(body.position);
        if (!near_screen(at)) {
            continue;
        }
        Colour colour = body.is_sun ? palette::WARNING : palette::NAV;
        if (body.is_destination) {
            colour = palette::TARGET;
        }
        const double r = std::max(unit() * (body.is_sun ? 2.4 : 1.5), 2.0);
        canvas_->circle(at, r, colour.darkened(0.45F));
        canvas_->arc(at, r, 0.0, kTau, 24, colour, std::max(unit() * 0.2, 1.0));
        if (label_fits(at, placed)) {
            placed.push_back(at);
            draw_text_at(at + Vec2{0.0, r + unit() * 4.0}, fmt::upper(body.name), 3.4, colour, Align::Centre);
        }
    }

    draw_system_anchors();
    draw_system_maneuvers();

    const Vec2 ship_at = screen(ship);
    canvas_->circle(ship_at, std::max(unit() * 1.6, 3.0), palette::PRIMARY);
    canvas_->arc(ship_at, std::max(unit() * 4.0, 7.0), 0.0, kTau, 24, palette::PRIMARY, std::max(unit() * 0.2, 1.0));

    draw_system_legend();
}

double OrbitMap::log_zoom() const {
    // LOGARITHMIC zoom (rule 28).
    //
    // The map has to cover from 1e6 m to 1e12 m -- six orders of magnitude. With
    // linear zoom, the step that is comfortable near the Earth crosses Neptune's
    // whole orbit in a click, and the step that works at Neptune does not move
    // near the Earth. Raising the factor to a power gives a constant step in
    // ORDERS OF MAGNITUDE, which is how distance reads here.
    return std::pow(10.0, std::log(zoom) / std::log(40.0) * 3.0);
}

void OrbitMap::draw_system_grid() {
    // Rings labelled in AU, because it is the unit Solar System distances are
    // compared in (rule 97).
    const double step = nice_step(extent_ * 0.45 / kAu) * kAu;
    double ring = step;
    while (ring <= extent_ * 1.45) {
        const double radius = ring / extent_ * box_;
        canvas_->arc(origin_, radius, 0.0, kTau, 96, palette::PANEL_EDGE.darkened(0.3F), std::max(unit() * 0.12, 1.0));
        draw_text_at(origin_ + Vec2{radius + unit() * 1.5, -unit() * 1.0}, au_label(ring / kAu), 3.2, palette::DIM);
        ring += step;
    }
}

std::string OrbitMap::au_label(double au) {
    if (au >= 10.0) {
        return fmt::format("%.0f AU", au);
    }
    if (au >= 1.0) {
        return fmt::format("%.1f AU", au);
    }
    if (au >= 0.1) {
        return fmt::format("%.2f AU", au);
    }
    return fmt::format("%.3f AU", au);
}

bool OrbitMap::label_fits(Vec2 at, const std::vector<Vec2>& placed) const {
    // De-cluttering (rule 73): a label is drawn only if it does not fall on top
    // of another. Without this the four inner planets become a smear of text
    // whenever the map is open wide enough to show Mars.
    const double spacing = unit() * 9.0;
    for (const auto& other : placed) {
        if (at.distance_to(other) < spacing) {
            return false;
        }
    }
    return true;
}

void OrbitMap::draw_system_anchors() {
    // Where the origin WAS at departure and where the destination WILL BE at
    // arrival (rule 30).
    //
    // ⚠️ Neither is where the body is drawn, and that is why they exist. The
    // Earth travels 500 million kilometres while the ship goes to Mars, and Mars
    // a third of its orbit: an arc that ends far from the MARS marker is not a
    // defect -- it ends where Mars will be. Without these two rings, whoever looks
    // at the map has every right to conclude the opposite.
    if (system.origin_at_departure.has_value()) {
        draw_anchor(*system.origin_at_departure, "DEPARTURE", palette::NAV);
    }
    if (system.destination_at_arrival.has_value()) {
        draw_anchor(*system.destination_at_arrival, "ARRIVAL", palette::TARGET);
    }
}

void OrbitMap::draw_anchor(const Vec3& at, const std::string& label, Colour colour) {
    const Vec2 p = screen(at);
    if (!near_screen(p)) {
        return;
    }
    const double r = std::max(unit() * 2.2, 4.0);
    // A dashed ring, so as not to be taken for the body: the body is where it
    // is, this is where it will be.
    for (int i = 0; i < 8; ++i) {
        const double a0 = kTau * i / 8.0;
        canvas_->arc(p, r, a0, a0 + kTau / 16.0, 6, colour.darkened(0.2F), std::max(unit() * 0.18, 1.0));
    }
    draw_text_at(p + Vec2{r * 1.6, -r}, label, 3.2, colour.darkened(0.2F));
}

void OrbitMap::draw_system_maneuvers() {
    for (const auto& burn : system.maneuvers) {
        if (!burn.located) {
            continue;
        }
        const Vec2 at = screen(burn.position);
        if (!near_screen(at)) {
            continue;
        }
        const double r = std::max(unit() * 1.2, 2.0);
        const Colour colour = burn.done ? palette::DIM : (burn.active ? palette::CRITICAL : palette::WARNING);
        const double w = std::max(unit() * 0.24, 1.0);
        canvas_->line(at + Vec2{-r * 2.0, 0.0}, at + Vec2{r * 2.0, 0.0}, colour, w);
        canvas_->line(at + Vec2{0.0, -r * 2.0}, at + Vec2{0.0, r * 2.0}, colour, w);
        draw_text_at(at + Vec2{r * 3.0, -unit() * 0.5}, fmt::upper(burn.name.empty() ? "BURN" : burn.name), 3.2,
                     colour);
    }
}

void OrbitMap::draw_system_legend() {
    const double u = unit();
    draw_text_at(Vec2{u * 4.0, u * 6.0}, "SOLAR SYSTEM MAP", 5.0, palette::PRIMARY);
    draw_text_at(Vec2{u * 4.0, u * 12.0},
                 input::label("orbit_map") + " close   " + input::label("map_mode") +
                     " local/system   wheel zoom   zoom " + fmt::format("%.2fx", zoom),
                 3.6, palette::SECONDARY);
    // Rule 71: say which the frame is, always. A map without a declared frame is
    // the defect M7 found in the orbital map.
    draw_text_at(Vec2{u * 4.0, u * 17.0},
                 "REFERENCE " + fmt::upper(system.centre.empty() ? std::string{"SUN"} : system.centre) + "   (" +
                     system.frame + ")",
                 3.4, palette::DIM);

    struct Entry {
        const char* text;
        Colour colour;
    };
    const Entry legend[] = {{"planned transfer", palette::PLAN},
                            {"planet orbits", palette::PANEL_EDGE.lightened(0.10F)},
                            {"destination", palette::TARGET},
                            {"burn", palette::WARNING},
                            {"○ departure / arrival: where the bodies WILL be", palette::NAV}};
    double y = size_.y - u * 6.0;
    for (const auto& entry : legend) {
        canvas_->line(Vec2{u * 4.0, y - u * 1.2}, Vec2{u * 10.0, y - u * 1.2}, entry.colour, std::max(u * 0.3, 1.0));
        draw_text_at(Vec2{u * 11.5, y}, entry.text, 3.6, palette::SECONDARY);
        y -= u * 5.5;
    }
}

}  // namespace sf::app
