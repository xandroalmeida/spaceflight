#include "app/presentation/instruments/displays.hpp"

#include "app/presentation/format.hpp"
#include "app/presentation/input_actions.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sf::app {
namespace {

constexpr double kTau = 2.0 * std::numbers::pi;

double rad_to_deg(double r) { return r * 180.0 / std::numbers::pi; }

std::string replace_underscores(std::string text) {
    std::replace(text.begin(), text.end(), '_', ' ');
    return text;
}

std::string lower(std::string text) {
    for (auto& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

}  // namespace

// --- FlightDisplay -------------------------------------------------------------

std::pair<Vec2, bool> FlightDisplay::project(const Vec3& direction, const Basis& basis, Vec2 centre,
                                             double radius) {
    // Integration -> body -> screen. `basis` has the columns x (nose), y, z.
    const Vec3 body{dot(basis.x, direction), dot(basis.y, direction), dot(basis.z, direction)};
    const double along = std::clamp(body.x, -1.0, 1.0);
    const double angle = rad_to_deg(std::acos(along));
    Vec2 lateral{-body.y, -body.z};   // screen: right = -y, up = +z
    if (lateral.length() < 1.0e-6) {
        // Exactly on the nose (or exactly behind): the azimuth is undefined and
        // any choice will do, because the radius is zero or the maximum.
        lateral = Vec2{0.0, -1.0};
    } else {
        lateral = lateral.normalized();
    }
    const bool behind = angle > FIELD_OF_VIEW_DEG;
    const double r = radius * std::min(angle / FIELD_OF_VIEW_DEG, 1.0);
    return {centre + lateral * r, behind};
}

void FlightDisplay::paint() {
    title_ = "FLIGHT";
    draw_frame();
    if (!data_->present) {
        draw_text_at(size_ * 0.5, "NO DATA", 6.0, palette::DIM, Align::Centre);
        return;
    }
    const Vec2 centre{size_.x * 0.5, size_.y * 0.52};
    const double radius = std::min(size_.x, size_.y) * 0.40;
    draw_rose(centre, radius);
    draw_markers(centre, radius);
    draw_nose(centre, radius);
    draw_rates();
}

void FlightDisplay::draw_rose(Vec2 centre, double radius) {
    // Three rings, at 30, 60 and 90 degrees from the nose, labelled. They are the
    // display's scale: without them a marker at half radius means nothing.
    for (const double ring : {1.0 / 3.0, 2.0 / 3.0, 1.0}) {
        const Colour colour = ring < 1.0 ? palette::DIM : palette::SECONDARY;
        canvas_->arc(centre, radius * ring, 0.0, kTau, 64, colour, std::max(unit() * 0.22, 1.0));
    }
    for (const int degrees : {30, 60}) {
        draw_text_at(centre + Vec2{radius * degrees / FIELD_OF_VIEW_DEG + unit(), -unit()},
                     fmt::format("%d°", degrees), 3.4, palette::DIM);
    }
    // Axis cross: the roll reference. The only thing on this display that speaks
    // of the ship's long axis.
    draw_tick(centre - Vec2{radius, 0.0}, centre + Vec2{radius, 0.0}, palette::DIM, 0.18);
    draw_tick(centre - Vec2{0.0, radius}, centre + Vec2{0.0, radius}, palette::DIM, 0.18);
}

void FlightDisplay::draw_markers(Vec2 centre, double radius) {
    struct Marker {
        const char* key;
        Colour colour;
    };
    static const Marker markers[] = {
        {"prograde", palette::PROGRADE},  {"retrograde", palette::PROGRADE}, {"normal", palette::NAV},
        {"anti_normal", palette::NAV},    {"radial_out", palette::NAV_DIM},  {"radial_in", palette::NAV_DIM},
        {"target", palette::TARGET},      {"anti_target", palette::TARGET},
    };
    const double marker_radius = std::max(unit() * 2.4, 3.0);
    for (const auto& marker : markers) {
        const auto direction = data_->directions.by_name(marker.key);
        if (!direction.has_value()) {
            continue;
        }
        const auto [at, behind] = project(*direction, data_->ship_basis, centre, radius);
        Colour colour = marker.colour;
        if (behind) {
            colour = palette::faded(colour, 0.62F);
        }
        draw_marker(at, marker_radius, colour, marker.key);
    }
}

void FlightDisplay::draw_nose(Vec2 centre, double radius) {
    // The nose is ALWAYS at the centre, because the centre is the nose. What is
    // drawn here is the ship symbol and, when there is a pointing command, the
    // line from the nose to its target -- the "flight director" of rule 18:
    // follow the line and the error closes.
    draw_marker(centre, std::max(unit() * 3.0, 4.0), palette::PRIMARY, "nose");

    const std::string& mode = data_->s.pointing_mode;
    if (mode == "HOLD" || mode.empty()) {
        return;
    }
    const auto direction = data_->directions.by_name(lower(mode));
    if (!direction.has_value()) {
        return;
    }
    const auto placed = project(*direction, data_->ship_basis, centre, radius);
    canvas_->line(centre, placed.first, palette::WARNING, std::max(unit() * 0.28, 1.0));
    const double error = data_->s.pointing_error_deg;
    draw_text_at(Vec2{size_.x - unit() * 2.5, size_.y - unit() * 4.5}, fmt::format("%s  %.2f°", mode.c_str(), error),
                 4.4, error > 2.0 ? palette::WARNING : palette::OK, Align::Right);
}

void FlightDisplay::draw_rates() {
    const double rate = data_->s.rotation_rate_deg_s;
    draw_text_at(Vec2{unit() * 2.5, size_.y - unit() * 4.5}, fmt::format("SPIN %.3f °/s", rate), 4.4,
                 rate > 2.0 ? palette::WARNING : palette::SECONDARY);
    // Speed, at the top: the number read more often than any other.
    draw_text_at(Vec2{size_.x - unit() * 2.5, unit() * 6.0}, fmt::speed(data_->s.speed_ms), 5.6, palette::PRIMARY,
                 Align::Right);
}

// --- TargetDisplay -------------------------------------------------------------

void TargetDisplay::paint() {
    title_ = "TARGET";
    draw_frame();

    const std::string& target = data_->s.target;
    if (target.empty()) {
        draw_text_at(Vec2{size_.x * 0.5, size_.y * 0.45}, "NO TARGET", 6.5, palette::DIM, Align::Centre);
        draw_text_at(Vec2{size_.x * 0.5, size_.y * 0.62},
                     input::label("target_prev") + " / " + input::label("target_next") + " to select", 3.8,
                     palette::DIM, Align::Centre);
        return;
    }

    const double u = unit();
    draw_text_at(Vec2{u * 3.0, u * 15.0}, fmt::upper(target), 9.0, palette::TARGET);

    const double distance = data_->s.target_distance_m;
    const double relative_speed = data_->s.target_relative_speed_ms;
    const double closing = data_->closing_speed_ms;

    draw_field(Vec2{u * 3.0, u * 27.0}, "DISTANCE", fmt::distance(distance), 7.5);
    draw_field(Vec2{u * 3.0, u * 45.0}, "REL SPEED", fmt::speed(relative_speed), 7.5);
    // The sign is the information: closing or opening. A magnitude would hide it.
    draw_field(Vec2{u * 3.0, u * 63.0}, "CLOSING",
               std::string{closing < 0.0 ? "-" : "+"} + fmt::speed(std::abs(closing)), 7.5,
               closing > 0.0 ? palette::OK : palette::WARNING);

    // The mission phase, which during a two-hundred-day cruise is the difference
    // between "all is well" and "something should have happened" (rule 62).
    //
    // ⚠️ A STRING that comes ready from the core. The classification depends on
    // where the destination's sphere of influence is and where the burns fall,
    // and that is physics -- core/navigation/mission_execution.hpp. This display
    // prints.
    const std::string& phase = data_->mission_phase;
    if (!phase.empty() && phase != "IDLE") {
        draw_text_at(Vec2{u * 3.0, u * 8.0}, replace_underscores(phase), 4.4, palette::PLAN);
    }

    draw_bearing(Vec2{size_.x * 0.76, size_.y * 0.42}, std::min(size_.x * 0.20, size_.y * 0.28));
    draw_intercept(Vec2{u * 3.0, size_.y - u * 5.5}, distance, closing);
}

void TargetDisplay::draw_bearing(Vec2 centre, double radius) {
    // Where the target is relative to the nose, in the same projection as the
    // flight display -- the same screen convention, so that the two do not
    // contradict each other.
    canvas_->arc(centre, radius, 0.0, kTau, 48, palette::DIM, std::max(unit() * 0.22, 1.0));
    draw_tick(centre - Vec2{radius, 0.0}, centre + Vec2{radius, 0.0}, palette::DIM, 0.16);
    draw_tick(centre - Vec2{0.0, radius}, centre + Vec2{0.0, radius}, palette::DIM, 0.16);
    draw_marker(centre, std::max(unit() * 1.6, 2.0), palette::PRIMARY, "nose");

    if (!data_->directions.target.has_value()) {
        return;
    }
    const Basis& basis = data_->ship_basis;
    const Vec3 direction = *data_->directions.target;
    const Vec3 body{dot(basis.x, direction), dot(basis.y, direction), dot(basis.z, direction)};
    const double angle = rad_to_deg(std::acos(std::clamp(body.x, -1.0, 1.0)));
    Vec2 lateral{-body.y, -body.z};
    lateral = lateral.length() > 1.0e-6 ? lateral.normalized() : Vec2{0.0, -1.0};
    const Vec2 at = centre + lateral * (radius * std::min(angle / 180.0, 1.0));
    draw_marker(at, std::max(unit() * 1.8, 2.5), palette::TARGET, "target");
    draw_text_at(centre + Vec2{0.0, radius + unit() * 5.0}, fmt::format("%.1f° off nose", angle), 3.6,
                 palette::SECONDARY, Align::Centre);
}

void TargetDisplay::draw_intercept(Vec2 at, double distance, double closing) {
    const double plan_arrival = data_->plan_seconds_to_arrival;
    if (plan_arrival > 0.0) {
        draw_text_at(at, "ARRIVAL (PLANNED)  " + fmt::countdown(plan_arrival), 4.4, palette::PLAN);
        return;
    }
    if (closing <= 0.0) {
        draw_text_at(at, "NO INTERCEPT -- opening", 4.4, palette::SECONDARY);
        return;
    }
    // Labelled "linear" where it is read, and not only in this comment: the
    // number ignores gravity and a pilot who took it for an arrival prediction
    // would be off by hours.
    draw_text_at(at, "CLOSE IN " + fmt::duration(distance / closing) + " (linear)", 4.4, palette::SECONDARY);
}

// --- SystemDisplay -------------------------------------------------------------

void SystemDisplay::paint() {
    canvas_->rect(Rect2{{0.0, 0.0}, size_}, palette::BACKGROUND, true, 1.0);
    canvas_->rect(Rect2{{1.0, 1.0}, size_ - Vec2{2.0, 2.0}}, palette::PANEL_EDGE, false, 1.0);
    if (!data_->present) {
        return;
    }
    const double h = size_.y;
    const double pad = h * 0.07;
    const double column = (size_.x - pad * 2.0) / static_cast<double>(COLUMNS);
    for (int i = 1; i < COLUMNS; ++i) {
        const double x = pad + column * i;
        canvas_->line(Vec2{x, h * 0.12}, Vec2{x, h * 0.88}, palette::PANEL_EDGE, 1.0);
    }
    engine(pad + column * 0.0, column, h);
    thrust(pad + column * 1.0, column, h);
    propellant(pad + column * 2.0, column, h);
    mass(pad + column * 3.0, column, h);
    rcs(pad + column * 4.0, column, h);
    relativity(pad + column * 5.0, column, h);
}

void SystemDisplay::label(Vec2 at, const std::string& text, double h, Colour colour) {
    canvas_->text(at, text, std::max(static_cast<int>(h * 0.105), 8), colour);
}

void SystemDisplay::value(Vec2 at, const std::string& text, double h, Colour colour, double scale) {
    canvas_->text(at, text, std::max(static_cast<int>(h * scale), 9), colour);
}

void SystemDisplay::cell(double x, double width, double h, const std::string& label_text,
                         const std::string& value_text, Colour colour) {
    const double inset = width * 0.06;
    label(Vec2{x + inset, h * 0.26}, label_text, h);
    value(Vec2{x + inset, h * 0.56}, value_text, h, colour);
}

void SystemDisplay::engine(double x, double width, double h) {
    const double inset = width * 0.06;
    const double throttle = data_->s.throttle;
    const double thrust = data_->s.thrust_n;
    label(Vec2{x + inset, h * 0.26}, "MAIN ENGINE  " + (data_->s.engine_mode.empty() ? std::string{"?"} : data_->s.engine_mode), h);

    // The throttle rule (rule 14). The bar is the COMMAND; the word next to it
    // is the STATE, which comes from the thrust the core is actually producing.
    // With the tank empty the key keeps working and the bar keeps rising -- and
    // the state says SAFE, which is the only one of the two readings that is
    // about the ship.
    const Rect2 bar{Vec2{x + inset, h * 0.38}, Vec2{width - inset * 2.0, h * 0.16}};
    draw_bar(bar, throttle, thrust > 0.0 ? palette::engine(data_->s.exhaust_velocity_c) : palette::DIM);
    const std::string status = thrust > 0.0 ? "RUNNING" : (data_->engine_armed ? "ARMED" : "SAFE");
    const Colour colour = thrust > 0.0 ? palette::OK : palette::SECONDARY;
    value(Vec2{x + inset, h * 0.86}, fmt::percent(throttle) + "  " + status, h, colour, 0.15);
}

void SystemDisplay::thrust(double x, double width, double h) {
    const double thrust_n = data_->s.thrust_n;
    cell(x, width, h, "THRUST", fmt::force(thrust_n),
         thrust_n > 0.0 ? palette::engine(data_->s.exhaust_velocity_c) : palette::PRIMARY);
    value(Vec2{x + width * 0.06, h * 0.86}, fmt::sci(data_->s.mass_flow_kg_s, 3) + " kg/s", h, palette::SECONDARY,
          0.125);
}

void SystemDisplay::propellant(double x, double width, double h) {
    const double inset = width * 0.06;
    const double propellant_kg = data_->s.propellant_kg;
    const double capacity = std::max(data_->propellant_capacity_kg, 1.0);
    const double fraction = propellant_kg / capacity;
    Colour colour = palette::OK;
    if (fraction < 0.25) {
        colour = palette::WARNING;
    }
    if (fraction < 0.08) {
        colour = palette::CRITICAL;
    }
    label(Vec2{x + inset, h * 0.26}, "PROPELLANT", h);
    draw_bar(Rect2{Vec2{x + inset, h * 0.38}, Vec2{width - inset * 2.0, h * 0.16}}, fraction, colour);
    value(Vec2{x + inset, h * 0.86}, fmt::mass(propellant_kg) + "  " + fmt::percent(fraction), h, colour, 0.15);
}

void SystemDisplay::mass(double x, double width, double h) {
    cell(x, width, h, "TOTAL MASS", fmt::mass(data_->s.mass_kg));
    value(Vec2{x + width * 0.06, h * 0.86}, "ΔV " + fmt::speed(data_->s.delta_v_budget_ms), h,
          palette::SECONDARY, 0.125);
}

void SystemDisplay::rcs(double x, double width, double h) {
    const double inset = width * 0.06;
    const bool enabled = data_->rcs_enabled;
    const int firing = data_->rcs_firing;
    const int total = std::max(data_->rcs_thrusters, 1);

    label(Vec2{x + inset, h * 0.26}, "REACTION CONTROL", h);
    value(Vec2{x + inset, h * 0.55}, std::string{"RCS "} + (enabled ? "ON" : "OFF"), h,
          enabled ? palette::OK : palette::WARNING, 0.165);
    // Only the activity. The pointing mode and error were here too and the line
    // ran over the relativity cell -- and they are on the flight display already,
    // large, with the director line pointing at them.
    value(Vec2{x + inset, h * 0.86}, data_->rcs_activity, h, firing > 0 ? palette::NAV : palette::SECONDARY, 0.125);

    // One lamp per thruster, in the order the core declares them, lit by the
    // ACTUATION and not by the key (rule 15). On a strip of this shape they fit
    // in a single row of twelve, right of the cell -- and a diagonal command is
    // seen at a glance to open four at different fractions.
    const auto& throttles = data_->rcs_throttles;
    const double lamps_x = x + width * 0.54;
    const double lamp_w = (width * 0.42) / static_cast<double>(total);
    for (int i = 0; i < total; ++i) {
        const double open = i < static_cast<int>(throttles.size()) ? throttles[static_cast<std::size_t>(i)] : 0.0;
        const Rect2 lamp{Vec2{lamps_x + lamp_w * i + 1.0, h * 0.34}, Vec2{std::max(lamp_w - 2.0, 1.0), h * 0.30}};
        if (open <= 0.002) {
            canvas_->rect(lamp, palette::PANEL_EDGE, true, 1.0);
        } else {
            canvas_->rect(lamp, palette::RCS.with_alpha(static_cast<float>(0.35 + 0.65 * open)), true, 1.0);
            canvas_->rect(lamp, palette::RCS, false, 1.0);
        }
    }
    value(Vec2{lamps_x, h * 0.86}, fmt::format("%d/%d", firing, total), h, palette::SECONDARY, 0.125);
}

void SystemDisplay::relativity(double x, double width, double h) {
    const double inset = width * 0.06;
    const double beta = data_->s.beta;
    const Colour colour = beta > 1.0e-3 ? palette::PRIMARY : palette::SECONDARY;
    label(Vec2{x + inset, h * 0.26}, "RELATIVITY", h);
    // Rule 22 in full in one cell: beta, gamma-1, the difference between
    // coordinate and proper time, and the proper acceleration. At 7.7 km/s that
    // is 1.0e-4, 5.1e-9, tens of femtoseconds and zero -- and that is exactly
    // what has to read. Two significant digits and not three: with three the
    // two lines ran off the right edge. The technical read-out has every digit.
    value(Vec2{x + inset, h * 0.55},
          "β " + fmt::sci(beta, 2) + fmt::format("    a %.2f m/s²", data_->proper_acceleration_ms2), h,
          colour, 0.13);
    value(Vec2{x + inset, h * 0.86},
          "γ-1 " + fmt::sci(data_->s.lorentz_factor_minus_one, 2) + "   Δt " +
              fmt::sci(data_->s.clock_difference_s, 2) + " s",
          h, palette::SECONDARY, 0.10);
}

// --- MinimalHud ----------------------------------------------------------------

double MinimalHud::unit() const { return std::max(size_.y / 100.0, 1.0) * 0.62; }

void MinimalHud::paint() {
    if (!data_->present) {
        return;
    }
    const double u = unit();

    // In the cockpit the bottom strip is not drawn, and that is rule 38 applied
    // and not economy: the panel already shows speed, altitude, throttle and
    // propellant, on instruments, and repeating them on a strip over them covers
    // exactly the part of the screen where they are. What is left are the
    // corners -- warp, target, mission phase -- that the panel does not show.
    if (data_->cockpit_view) {
        corner_top_left();
        corner_top_right();
        return;
    }

    const double band = size_.y * BAND_HEIGHT;
    const double base = size_.y - band;

    // The strip is not an opaque bar: a very weak shade, enough for white text
    // not to vanish over the Earth's polar ice.
    canvas_->rect(Rect2{Vec2{0.0, base}, Vec2{size_.x, band}}, Colour{0.0F, 0.0F, 0.0F, 0.38F}, true, 1.0);
    canvas_->line(Vec2{0.0, base}, Vec2{size_.x, base}, palette::PANEL_EDGE, 1.0);

    // Six cells, each with ONE value. The first version put apoapsis and
    // periapsis in one cell: in a parking orbit that is "400.0 km / 400.0 km" and
    // fits, but on the way to the Moon it is "362 658 km / -4 202.4 km" --
    // twenty-three characters over the throttle cell. One cell, one number.
    const double y = base + u * 9.0;
    big(Vec2{u * 6.0, y}, "SPEED", fmt::speed(data_->s.speed_ms));
    big(Vec2{size_.x * 0.19, y}, "ALTITUDE", fmt::distance(data_->s.altitude_m));
    big(Vec2{size_.x * 0.36, y}, "APOAPSIS", fmt::distance(data_->apoapsis_altitude_m));
    big(Vec2{size_.x * 0.53, y}, "PERIAPSIS", fmt::distance(data_->periapsis_altitude_m));

    throttle(Vec2{size_.x * 0.70, base + u * 3.0}, size_.x * 0.14, u);

    const double propellant = data_->s.propellant_kg;
    const double capacity = std::max(data_->propellant_capacity_kg, 1.0);
    big(Vec2{size_.x * 0.88, y}, "PROPELLANT", fmt::mass(propellant),
        propellant / capacity < 0.08 ? palette::CRITICAL : palette::PRIMARY);

    corner_top_left();
    corner_top_right();
}

void MinimalHud::big(Vec2 at, const std::string& label, const std::string& value, Colour colour) {
    const double u = unit();
    draw_text_at(at, label, 3.4, palette::SECONDARY);
    draw_text_at(at + Vec2{0.0, u * 6.2}, value, 6.0, colour);
}

void MinimalHud::throttle(Vec2 at, double width, double u) {
    const double throttle_value = data_->s.throttle;
    const double thrust = data_->s.thrust_n;
    draw_text_at(at, "THROTTLE", 3.4, palette::SECONDARY);
    draw_bar(Rect2{at + Vec2{0.0, u * 2.0}, Vec2{width, u * 3.4}}, throttle_value,
             thrust > 0.0 ? palette::engine(data_->s.exhaust_velocity_c) : palette::DIM);
    draw_text_at(at + Vec2{0.0, u * 9.6}, fmt::percent(throttle_value) + "   " + fmt::force(thrust), 4.0,
                 palette::PRIMARY);
}

void MinimalHud::corner_top_left() {
    const double u = unit();
    const Vec2 at{u * 6.0, u * 8.0};
    const bool paused = data_->paused;
    draw_text_at(at, fmt::warp(data_->s.time_warp) + "   " + data_->s.reference, 5.0,
                 paused ? palette::WARNING : palette::PRIMARY);
    if (paused) {
        draw_text_at(at + Vec2{0.0, u * 6.5}, "PAUSED", 5.0, palette::WARNING);
        return;
    }
    draw_text_at(at + Vec2{0.0, u * 6.5}, data_->camera_mode + "  " + data_->rcs_activity, 4.0, palette::SECONDARY);
}

void MinimalHud::corner_top_right() {
    const double u = unit();
    const double right = size_.x - u * 6.0;
    const std::string& target = data_->s.target;
    if (!target.empty()) {
        draw_text_at(Vec2{right, u * 8.0}, "TARGET " + fmt::upper(target), 4.6, palette::TARGET, Align::Right);
        draw_text_at(Vec2{right, u * 14.0},
                     fmt::distance(data_->s.target_distance_m) + "   " + fmt::speed(data_->s.target_relative_speed_ms),
                     4.0, palette::SECONDARY, Align::Right);
    }
    const std::string& phase = data_->mission_phase;
    if (phase.empty() || phase == "IDLE") {
        return;
    }
    draw_text_at(Vec2{right, u * 22.0}, phase, 5.2, palette::PLAN, Align::Right);
    if (!data_->next_event.empty()) {
        draw_text_at(Vec2{right, u * 28.0}, data_->next_event, 4.4, palette::PLAN, Align::Right);
        draw_text_at(Vec2{right, u * 34.0}, fmt::countdown(data_->next_event_seconds), 6.0, palette::PLAN,
                     Align::Right);
    }
}

}  // namespace sf::app
