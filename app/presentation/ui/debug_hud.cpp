#include "app/presentation/ui/debug_hud.hpp"

#include "app/presentation/flight_app.hpp"
#include "app/presentation/format.hpp"
#include "app/presentation/input_actions.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sf::app::debug_hud {
namespace {

using fmt::format;

std::vector<std::string> compact_lines(const FlightApp& flight, const SnapshotView& s) {
    std::vector<std::string> lines = {
        format("warp %.0fx   %s", s.time_warp, s.reference.c_str()),
        format("altitude   %.1f km", s.altitude_m / 1000.0),
        format("speed      %.1f m/s", s.speed_ms),
        format("apo/peri   %.1f / %.1f km", s.apoapsis_m / 1000.0, s.periapsis_m / 1000.0),
        format("propellant %.1f kg   throttle %.0f %%", s.propellant_kg, s.throttle * 100.0),
        format("engine     %s", s.engine_mode.c_str()),
        format("pointing   %s  err %.2f deg", s.pointing_mode.c_str(), s.pointing_error_deg),
        "beta       " + fmt::sci(s.beta),
    };
    const auto& sky = flight.sky();
    if (sky.is_ready()) {
        const auto d = sky.diagnostics();
        lines.emplace_back("");
        lines.push_back(format("D ahead %.4f  astern %.4f", d.max_doppler, d.min_doppler));
        lines.push_back(format("look  %s  D %.6f", flight.camera().mode_name().c_str(),
                               sky.doppler_in_direction(flight.camera().look_direction())));
    }
    return lines;
}

std::string outcome_line(const std::string& label, const PredictedActual& row, double scale) {
    // A line that was never recorded prints as such, and not as three zeros. A
    // zero difference is a claim; "not recorded" is the truth when the flight
    // never went through the phase that would measure it.
    if (!row.recorded) {
        return format("  %-12s %14s", label.c_str(), "not recorded");
    }
    return format("  %-12s %14.4f %14.4f %14.4f", label.c_str(), row.predicted * scale, row.actual * scale,
                  row.difference * scale);
}

std::vector<std::string> look_lines(const FlightApp& flight, const SkyDiagnosticsView& d) {
    // Where the camera points, and into what. The ANGLE is camera geometry and is
    // measured here; the Doppler factor is NOT -- it comes from
    // core/relativity/optics.hpp through the sky, because it is physics.
    //
    // The angle is measured from the BARYCENTRIC velocity and not from prograde:
    // this cockpit's prograde is relative to the reference body (7.7 km/s round
    // the Earth) while the sky is aberrated by the velocity in the frame where
    // the stars are at rest (30.7 km/s). In low orbit the two point about 30
    // degrees apart.
    const auto& rig = flight.camera();
    const Vec3 forward = rig.look_direction();
    const Vec3 axis = flight.session().beta_vector().normalized();
    const double angle = axis.norm() > 0.5 ? angle_between(forward, axis) * 180.0 / std::numbers::pi : 0.0;
    const std::string inside = angle <= d.forward_cone_deg ? "  INSIDE the forward cone" : "";
    const std::string where = rig.at_preset() ? rig.mode_name() : rig.mode_name() + " (free)";
    const std::string zoom =
        std::abs(rig.orbit_zoom - 1.0) < 1.0e-5 ? "" : format("   zoom %.2fx", rig.orbit_zoom);
    return {
        format("look           %s   %.1f deg off the aberration axis%s%s", where.c_str(), angle, inside.c_str(),
               zoom.c_str()),
        format("looking into   D = %.6f", flight.sky().doppler_in_direction(forward)),
        "",
    };
}

std::vector<std::string> frame_lines(const FlightApp& flight) {
    // Frame, integrator, and the state the camera is in. Measured and not
    // estimated.
    const auto& rig = flight.camera();
    return {
        format("fps            %.1f   frame %.2f ms", flight.fps(), flight.frame_ms()),
        "integrator     Dormand-Prince 5(4), adaptive; frame asks for time, never for steps",
        "frame          J2000 / SSB   render scale " + fmt::sci(1.0 / flight.session().render_scale()) +
            " m per unit",
        format("camera         %s   focus %s   body scale %s", rig.mode_name().c_str(),
               rig.focus_index < 0 ? "spacecraft" : flight.session().body_name(rig.focus_index).c_str(),
               flight.body_scale_label().c_str()),
        "",
    };
}

std::vector<std::string> control_lines() {
    // Generated from the key table, not written by hand: a copied list of keys
    // is a list of keys that ages in silence.
    std::vector<std::string> out;
    for (const auto& group : input::by_group()) {
        std::string line = "(" + group.name + ":";
        bool first = true;
        for (const auto* binding : group.entries) {
            line += first ? " " : "   ";
            line += input::label(binding->action) + " " + binding->description;
            first = false;
        }
        out.push_back(line + ")");
    }
    return out;
}

}  // namespace

std::vector<std::string> hud_lines(const FlightApp& flight, bool compact) {
    const auto& session = flight.session();
    const auto s = session.snapshot();
    if (!s.valid) {
        return {};
    }
    if (compact) {
        return compact_lines(flight, s);
    }
    std::vector<std::string> lines = {
        format("t (TDB)        %+.3f s since J2000", s.time_tdb_s),
        format("elapsed        %.6f s   warp %.0fx", s.elapsed_s, s.time_warp),
        format("proper time    %.6f s", s.proper_time_s),
        "clock diff     " + fmt::sci(s.clock_difference_s) + " s",
        "",
        "reference      " + s.reference,
        format("altitude       %.3f km", s.altitude_m / 1000.0),
        format("speed          %.3f m/s", s.speed_ms),
        format("acceleration   %.6f m/s^2", s.acceleration_ms2),
        "",
        format("apoapsis       %.3f km", s.apoapsis_m / 1000.0),
        format("periapsis      %.3f km", s.periapsis_m / 1000.0),
        format("eccentricity   %.8f", s.eccentricity),
        format("inclination    %.4f deg", s.inclination_deg),
        format("period         %.2f s", s.period_s),
        "",
        format("mass           %.1f kg", s.mass_kg),
        format("propellant     %.3f kg", s.propellant_kg),
        "flow           " + fmt::sci(s.mass_flow_kg_s) + " kg/s   endurance " + fmt::duration(s.endurance_s),
        format("engine         %s   w = %.3f c", s.engine_mode.c_str(), s.exhaust_velocity_c),
        format("throttle       %.0f %%      thrust %.1f N", s.throttle * 100.0, s.thrust_n),
        "delta-v left   " + fmt::sci(s.delta_v_budget_ms) + " m/s",
        format("along track    %+.3f   orbit energy %+.1f J/kg/s", s.thrust_along_track, s.specific_energy_rate),
        "",
        format("pointing       %s   error %.3f deg", s.pointing_mode.c_str(), s.pointing_error_deg),
        format("nose->prograde %.3f deg", s.angle_to_prograde_deg),
        format("nose->nadir    %.3f deg", s.angle_to_nadir_deg),
        format("spin rate      %.4f deg/s", s.rotation_rate_deg_s),
        format("rcs            %s   %d of %d firing", flight.controls().rcs_activity(flight.rcs_firing_count()).c_str(),
               flight.rcs_firing_count(), flight.rcs_thruster_count()),
        format("target         %s at %.0f km, %.1f m/s", s.target.c_str(), s.target_distance_m / 1000.0,
               s.target_relative_speed_ms),
        "",
        "beta           " + fmt::sci(s.beta),
        "gamma - 1      " + fmt::sci(s.lorentz_factor_minus_one),
        "render res.    " + fmt::sci(s.render_resolution_m) + " m per float ulp at " + s.reference,
        "",
    };
    for (auto& line : mission_lines(flight)) {
        lines.push_back(std::move(line));
    }
    for (auto& line : sky_lines(flight)) {
        lines.push_back(std::move(line));
    }
    for (auto& line : frame_lines(flight)) {
        lines.push_back(std::move(line));
    }
    for (auto& line : control_lines()) {
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<std::string> mission_lines(const FlightApp& flight) {
    const auto& session = flight.session();
    std::vector<std::string> lines;
    // The orbit about the target, when there is one. It is the line that says
    // whether the mission worked, and it sits ABOVE the plan and not below: once
    // in lunar orbit the plan is history.
    const auto o = session.orbit_about_target();
    if (o.valid) {
        lines.push_back(format("about %-9s %s   %.1f km at %.1f m/s", o.body.c_str(),
                               o.captured ? "CAPTURED" : "hyperbolic", o.distance_m / 1000.0, o.speed_ms));
        if (o.captured) {
            lines.push_back(format("  orbit        %.1f x %.1f km altitude, e %.4f, i %.2f deg, %s",
                                   (o.periapsis_m - o.radius_m) / 1000.0, (o.apoapsis_m - o.radius_m) / 1000.0,
                                   o.eccentricity, o.inclination_deg, fmt::duration(o.period_s).c_str()));
        }
        lines.emplace_back("");
    }

    if (!session.has_plan()) {
        lines.push_back(format("mission        none   (%s: plan a transfer, %s: %s)", input::label("mission_plan").c_str(),
                               input::label("execution_model").c_str(), session.execution_model().c_str()));
        lines.emplace_back("");
        return lines;
    }
    const auto p = session.plan();
    if (!p.valid) {
        return lines;
    }
    // Section 15 of M6.2: everything the computer shows BEFORE execution, and
    // every number from the core's MissionMetrics.
    const double to_ignition = p.seconds_to_ignition;
    const double to_insertion = p.seconds_to_insertion;
    lines.push_back(format("mission        %s to %s   (%s: abort)", p.phase.empty() ? "?" : p.phase.c_str(),
                           p.target.c_str(), input::label("mission_abort").c_str()));
    lines.push_back(format("departure      %s   %s branch, %.2f d of flight",
                           to_ignition > 0.0 ? ("in " + fmt::duration(to_ignition)).c_str() : "past",
                           p.branch.empty() ? "?" : p.branch.c_str(), p.time_of_flight_days));
    lines.push_back(
        format("arrival        %s", to_insertion > 0.0 ? ("in " + fmt::duration(to_insertion)).c_str() : "past"));
    lines.push_back(format("injection      %.1f m/s over %s", p.injection_delta_v,
                           fmt::duration(p.injection_duration_s).c_str()));
    lines.push_back(format("midcourse      %.1f m/s   (folded into the injection, not a separate burn)",
                           p.midcourse_delta_v));
    lines.push_back(format("capture        %.1f m/s over %s", p.insertion_delta_v,
                           fmt::duration(p.insertion_duration_s).c_str()));
    lines.push_back(format("total dv       %.1f m/s of %.0f available", p.total_delta_v, p.delta_v_available));
    lines.push_back(format("predicted      %.1f x %.1f km, e %.4f, i %.2f deg, RAAN %.1f deg",
                           p.predicted_periapsis_m / 1000.0, p.predicted_apoapsis_m / 1000.0,
                           p.predicted_eccentricity, p.predicted_inclination_deg, p.predicted_raan_deg));
    lines.push_back(format("propellant     %.2f kg required, %.2f kg left after", p.propellant_required_kg,
                           p.propellant_remaining_kg));
    lines.push_back(format("autopilot      lag %.2f deg mean / %.2f deg peak, RCS %.1f g, duty %.3f",
                           p.pointing_error_mean_deg, p.pointing_error_peak_deg, p.rcs_propellant_kg * 1000.0,
                           p.rcs_duty_cycle));

    // Section 17: predicted against actual, after the orbit has settled.
    const auto outcome = session.mission_outcome();
    if (outcome.recorded) {
        lines.emplace_back("");
        lines.push_back(format("               %14s %14s %14s", "predicted", "actual", "difference"));
        lines.push_back(outcome_line("periapsis km", outcome.periapsis_m, 1.0e-3));
        lines.push_back(outcome_line("apoapsis km", outcome.apoapsis_m, 1.0e-3));
        lines.push_back(outcome_line("eccentricity", outcome.eccentricity, 1.0));
        lines.push_back(outcome_line("inclination", outcome.inclination_deg, 1.0));
        lines.push_back(outcome_line("propellant kg", outcome.propellant_kg, 1.0));
        lines.push_back(outcome_line("capture dv", outcome.capture_delta_v, 1.0));
        // The arrival only as a DIFFERENCE. The absolute epochs are 8.2e8 s since
        // J2000 and a 14-column field would show nine digits of agreement and
        // hide the number anyone wants.
        if (outcome.arrival_tdb_s.recorded) {
            lines.push_back(format("  %-12s %14s %14s %14.1f", "arrival s", "-", "-", outcome.arrival_tdb_s.difference));
        }
        if (!outcome.note.empty()) {
            lines.push_back("  note: " + outcome.note);
        }
    }
    lines.emplace_back("");
    return lines;
}

std::vector<std::string> sky_lines(const FlightApp& flight) {
    // What the optics are doing, in numbers, so that "looks fast" is never the
    // evidence. Every value came out of core/render/relativistic_sky.cpp.
    const auto& sky = flight.sky();
    const auto& session = flight.session();
    if (!sky.is_ready()) {
        return {"sky            no catalogue (scripts/fetch_star_catalog.sh)", ""};
    }
    const auto d = sky.diagnostics();
    double moon_light = 0.0;
    if (const int moon = session.body_index("Moon"); moon >= 0) {
        moon_light = session.body_light_time(moon);
    }
    const auto& world = flight.celestial();
    std::vector<std::string> lines = {
        format("stars          %lld  visual beta %s", d.star_count, flight.visual_beta_label().c_str()),
        format("effects        aberration %s  Doppler %s  beaming %s  retarded %s",
               fmt::on_off(world.effect_aberration).c_str(), fmt::on_off(world.effect_doppler).c_str(),
               fmt::on_off(world.effect_beaming).c_str(), fmt::on_off(world.effect_retarded).c_str()),
        format("forward cone   %.3f deg holds %lld stars (%.2f %%)", d.forward_cone_deg, d.stars_in_forward_cone,
               100.0 * d.fraction_in_forward_cone),
        format("doppler        %.6f astern .. %.6f ahead", d.min_doppler, d.max_doppler),
        // Seven digits, not four: at beta = 1e-4 the whole signal is in the sixth
        // place (1.000464) and at beta = 0.9 it is 51.48. One format has to carry
        // both, and the digits are free.
        "5800 K star    " + fmt::sci(d.reference_forward_visible, 7) + " x ahead, " +
            fmt::sci(d.reference_aft_visible, 7) + " x astern  (visible band)",
        format("exposure       %.4f half-saturation flux", sky.half_saturation()),
        format("light time     Moon %.4f s", moon_light),
    };
    for (auto& line : look_lines(flight, d)) {
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<std::string> sky_projection_lines(FlightApp& flight, double beta) {
    auto& sky = flight.sky();
    if (!sky.is_ready()) {
        return {};
    }
    const Vec3 heading = flight.session().spacecraft_velocity_direction();
    const auto d = sky.diagnostics_at(heading * beta);
    return {
        format("PROJECTION at beta = %.4f (the ship is NOT at this speed; the state is untouched)", beta),
        format("  forward cone %.3f deg holds %lld stars (%.2f %%)", d.forward_cone_deg, d.stars_in_forward_cone,
               100.0 * d.fraction_in_forward_cone),
        format("  doppler      %.4f astern .. %.4f ahead", d.min_doppler, d.max_doppler),
        "  5800 K star  " + fmt::sci(d.reference_forward_visible) + " x ahead, " + fmt::sci(d.reference_aft_visible) +
            " x astern  (visible band, not bolometric)",
        "",
    };
}

std::vector<std::string> two_columns(const std::vector<std::string>& lines) {
    if (lines.size() < 24) {
        return lines;
    }
    const std::size_t middle = lines.size() / 2;
    std::size_t split = middle;
    for (std::size_t offset = 0; offset < middle; ++offset) {
        if (middle - offset > 0 && lines[middle - offset].empty()) {
            split = middle - offset;
            break;
        }
        if (middle + offset < lines.size() && lines[middle + offset].empty()) {
            split = middle + offset;
            break;
        }
    }
    const std::vector<std::string> left(lines.begin(), lines.begin() + static_cast<long>(split));
    const std::vector<std::string> right(lines.begin() + static_cast<long>(std::min(split + 1, lines.size())), lines.end());
    std::size_t width = 0;
    for (const auto& line : left) {
        width = std::max(width, fmt::display_width(line));
    }
    std::vector<std::string> out;
    for (std::size_t i = 0; i < std::max(left.size(), right.size()); ++i) {
        const std::string l = i < left.size() ? left[i] : "";
        const std::string r = i < right.size() ? right[i] : "";
        if (r.empty()) {
            out.push_back(l);
        } else {
            std::string joined = fmt::rpad(l, width) + "   " + r;
            while (!joined.empty() && joined.back() == ' ') {
                joined.pop_back();
            }
            out.push_back(joined);
        }
    }
    return out;
}

}  // namespace sf::app::debug_hud
