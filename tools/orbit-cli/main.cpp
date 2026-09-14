// orbit-cli -- command line access to the scientific core.
//
// This is the verification surface of Milestone 0: everything the simulation
// knows can be interrogated here, without Godot, without a window, and in a form
// that can be diffed against JPL Horizons or an analytic result.

#include "core/celestial/body_catalog.hpp"
#include "core/ephemeris/errors.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "core/gravity/composite_force_model.hpp"
#include "core/gravity/oblateness_gravity.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/navigation/b_plane.hpp"
#include "core/navigation/mission.hpp"
#include "core/navigation/targeting.hpp"
#include "core/navigation/trajectory_planner.hpp"
#include "core/trajectory/lambert.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tools/orbit-cli/scenario.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace sf;
using sf::math::Vec3;

constexpr const char* kUsage = R"(orbit-cli -- spaceflight core command line

Usage:
  orbit-cli kernels
  orbit-cli bodies [--date <epoch>]
  orbit-cli time <epoch>
  orbit-cli body <name> [--date <epoch>] [--origin <body>] [--frame j2000|eclipj2000]
  orbit-cli elements <name> [--center <body>] [--date <epoch>]
  orbit-cli gravity [--center <body>] [--position x,y,z] [--date <epoch>]
  orbit-cli propagate <scenario.json> [--csv <file>] [--samples <n>] [--j2]
  orbit-cli lambert --to <body> --depart <epoch> --tof <days> [--center <body>] [--from <body|x,y,z>]
  orbit-cli intercept <scenario.json> --to <body> --tof <days> [--csv <file>]
                    [--no-retarget] [--tolerance-km <n>]
                    [--flyby-altitude-km <n>] [--b-plane-angle <deg>]
                    [--insert] [--insert-apoapsis-km <n>]

Options:
  --date <epoch>    Any format SPICE accepts: "2026-01-01", "2026-01-01T12:00:00",
                    "2026 JAN 01 12:00 TDB".  A bare timestamp is UTC.  Default: J2000.
  --origin <body>   Origin of the reference frame.  Default: SSB.
  --center <body>   Central body for orbital elements / gravity queries.
  --j2              Add the central body's J2 oblateness term (docs/physics/geopotential.md).
  --tof <days>      Time of flight for the Lambert transfer.
  --from <x,y,z>    Departure position relative to --center, in metres; or a body name.
  --flyby-altitude-km <n>
                    Aim a FLYBY at this altitude instead of the body's centre, by
                    B-plane targeting (docs/physics/b-plane.md).  Without it the
                    intercept aims at the centre, which is an impact.
  --b-plane-angle <deg>
                    Which side the flyby passes, as the angle of B from T towards
                    R.  0 is perpendicular to the J2000 pole, 90 is polar.
                    Default 0.  There is no natural choice; this is the mission's.
  --insert          Also report the lunar-orbit-insertion burn at periapsis.
  --insert-apoapsis-km <n>
                    Capture into an ellipse with this apoapsis altitude instead of
                    circularising.
  --kernels <dir>   Kernel directory.  Default: $SPACEFLIGHT_KERNEL_DIR or the build-time path.

All output is SI: metres, metres per second, seconds, kilograms.
)";

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> options;

    [[nodiscard]] std::optional<std::string> option(const std::string& name) const {
        for (const auto& [key, value] : options) {
            if (key == name) {
                return value;
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] std::string option_or(const std::string& name, const std::string& fallback) const {
        return option(name).value_or(fallback);
    }
    [[nodiscard]] bool has_flag(const std::string& name) const { return option(name).has_value(); }
};

Args parse_args(int argc, char** argv) {
    Args args{};
    std::vector<std::string> raw{argv + 1, argv + argc};
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const std::string& token = raw[i];
        if (token.rfind("--", 0) == 0) {
            const std::string key = token.substr(2);
            std::string value = "1";
            if (const auto eq = key.find('='); eq != std::string::npos) {
                args.options.emplace_back(key.substr(0, eq), key.substr(eq + 1));
                continue;
            }
            if (i + 1 < raw.size() && raw[i + 1].rfind("--", 0) != 0) {
                value = raw[++i];
            }
            args.options.emplace_back(key, value);
        } else if (args.command.empty()) {
            args.command = token;
        } else {
            args.positional.push_back(token);
        }
    }
    return args;
}

celestial::BodyId resolve_body(const std::string& name) {
    const auto lookup = celestial::body_from_name(name);
    if (!lookup.ok) {
        throw std::runtime_error("unknown body \"" + name +
                                 "\" (try a NAIF id, e.g. 399, or run `orbit-cli bodies`)");
    }
    return lookup.id;
}

coordinates::FrameAxes resolve_axes(const std::string& name) {
    std::string lower;
    std::transform(name.begin(), name.end(), std::back_inserter(lower),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "j2000") return coordinates::FrameAxes::J2000;
    if (lower == "eclipj2000") return coordinates::FrameAxes::ECLIPJ2000;
    if (lower == "iau_earth") return coordinates::FrameAxes::IAU_EARTH;
    if (lower == "iau_moon") return coordinates::FrameAxes::IAU_MOON;
    if (lower == "iau_sun") return coordinates::FrameAxes::IAU_SUN;
    if (lower == "iau_mars") return coordinates::FrameAxes::IAU_MARS;
    throw std::runtime_error("unknown frame \"" + name + "\"");
}

std::string fmt(double value, int precision = 12) {
    std::ostringstream os;
    os << std::setprecision(precision) << value;
    return os.str();
}

void print_vector(const std::string& label, const Vec3& v, const std::string& unit) {
    std::cout << label << "\n"
              << "    x  " << std::setw(26) << fmt(v.x, 17) << " " << unit << "\n"
              << "    y  " << std::setw(26) << fmt(v.y, 17) << " " << unit << "\n"
              << "    z  " << std::setw(26) << fmt(v.z, 17) << " " << unit << "\n"
              << "    |.|" << std::setw(26) << fmt(v.norm(), 17) << " " << unit << "\n";
}

struct Context {
    std::unique_ptr<ephemeris::SpiceEphemerisProvider> provider;
    std::unique_ptr<ephemeris::SpiceTimeConverter> time;
    std::shared_ptr<const ephemeris::SpiceKernelSet> kernels;
};

Context make_context(const Args& args) {
    const std::filesystem::path dir =
        args.option("kernels").has_value()
            ? std::filesystem::path{*args.option("kernels")}
            : ephemeris::SpiceKernelSet::default_directory();

    auto kernels = std::make_shared<const ephemeris::SpiceKernelSet>(
        ephemeris::SpiceKernelSet::from_directory(dir));

    Context ctx{};
    ctx.kernels = kernels;
    ctx.provider = std::make_unique<ephemeris::SpiceEphemerisProvider>(kernels);
    ctx.time = std::make_unique<ephemeris::SpiceTimeConverter>(kernels);
    return ctx;
}

time::CoordinateTime epoch_from(const Args& args, const Context& ctx) {
    const auto text = args.option("date");
    if (!text.has_value()) {
        return time::CoordinateTime::j2000();
    }
    return ctx.time->parse(*text);
}

Vec3 parse_vec3(const std::string& text) {
    Vec3 v{};
    std::string copy = text;
    std::replace(copy.begin(), copy.end(), ',', ' ');
    std::istringstream is{copy};
    if (!(is >> v.x >> v.y >> v.z)) {
        throw std::runtime_error("cannot parse vector \"" + text + "\" (expected x,y,z)");
    }
    return v;
}

int command_kernels(const Args& args) {
    const Context ctx = make_context(args);
    std::cout << "kernel directory: "
              << (args.option("kernels").value_or(
                     ephemeris::SpiceKernelSet::default_directory().string()))
              << "\n"
              << ctx.kernels->summary() << "\n\n";

    std::cout << "coverage (from loaded SPK files)\n";
    for (const auto id : celestial::BodyCatalog::default_solar_system_ids()) {
        const auto window = ctx.provider->coverage(id);
        std::cout << "  " << std::left << std::setw(22) << id.name() << std::right;
        if (!window.valid) {
            std::cout << "no SPK coverage\n";
            continue;
        }
        std::cout << ctx.time->to_utc_string(window.begin, 0) << "  ..  "
                  << ctx.time->to_utc_string(window.end, 0) << "\n";
    }
    return 0;
}

int command_bodies(const Args& args) {
    const Context ctx = make_context(args);
    const auto catalog = celestial::BodyCatalog::default_solar_system(*ctx.provider);

    std::cout << std::left << std::setw(22) << "body" << std::setw(8) << "NAIF"
              << std::setw(26) << "GM [m^3/s^2]" << "mean radius [m]\n";
    for (const auto& body : catalog.bodies()) {
        std::cout << std::left << std::setw(22) << body.name << std::setw(8) << body.id.naif_id()
                  << std::setw(26) << fmt(body.gm, 16)
                  << (body.radius > 0.0 ? fmt(body.radius, 10) : std::string{"-"}) << "\n";
    }
    std::cout << "\nGM values come from the loaded PCK (gm_de440.tpc), not from constants in the\n"
                 "source: the dynamics must use the same masses that generated the ephemeris.\n";
    return 0;
}

int command_time(const Args& args) {
    if (args.positional.empty()) {
        throw std::runtime_error("usage: orbit-cli time <epoch>");
    }
    const Context ctx = make_context(args);
    const auto t = ctx.time->parse(args.positional.front());

    std::cout << "input          : " << args.positional.front() << "\n"
              << "UTC            : " << ctx.time->to_utc_string(t, 6) << "\n"
              << "TDB            : " << ctx.time->to_tdb_string(t, 6) << "\n"
              << "TDB s past J2000: " << fmt(t.seconds_since_j2000(), 17) << "\n"
              << "  whole        : " << fmt(t.whole_seconds(), 17) << "\n"
              << "  fraction     : " << fmt(t.fractional_seconds(), 17) << "\n"
              << "Julian date TDB: " << fmt(t.julian_date_tdb(), 17) << "\n"
              << "TDB - TT       : "
              << fmt(t.seconds_since_j2000() - ctx.time->seconds_in_scale(t, time::TimeScale::TT), 6)
              << " s\n"
              << "TDB - UTC      : "
              << fmt(t.seconds_since_j2000() - ctx.time->seconds_in_scale(t, time::TimeScale::UTC), 6)
              << " s\n";
    return 0;
}

int command_body(const Args& args) {
    if (args.positional.empty()) {
        throw std::runtime_error("usage: orbit-cli body <name> [--date <epoch>]");
    }
    const Context ctx = make_context(args);
    const auto body = resolve_body(args.positional.front());
    const auto origin = resolve_body(args.option_or("origin", "0"));
    const auto axes = resolve_axes(args.option_or("frame", "j2000"));
    const auto t = epoch_from(args, ctx);

    const coordinates::ReferenceFrame frame{origin, axes};
    const auto state = ctx.provider->state(body, t, frame);

    std::cout << "body           : " << body.name() << " (NAIF " << body.naif_id() << ")\n"
              << "epoch UTC      : " << ctx.time->to_utc_string(t, 6) << "\n"
              << "epoch TDB      : " << ctx.time->to_tdb_string(t, 6) << "\n"
              << "TDB s past J2000: " << fmt(t.seconds_since_j2000(), 17) << "\n"
              << "reference frame: " << frame.to_string()
              << (frame.is_inertial() ? "  (inertial)" : "  (body-fixed, non-inertial)") << "\n"
              << "aberration     : none (geometric state)\n\n";

    print_vector("position [m]", state.state.position, "m");
    std::cout << "\n";
    print_vector("velocity [m/s]", state.state.velocity, "m/s");

    const double r = state.state.position.norm();
    std::cout << "\ndistance       : " << fmt(units::m_to_au(r), 12) << " au\n";
    return 0;
}

int command_elements(const Args& args) {
    if (args.positional.empty()) {
        throw std::runtime_error("usage: orbit-cli elements <name> [--center <body>] [--date <epoch>]");
    }
    const Context ctx = make_context(args);
    const auto body = resolve_body(args.positional.front());
    const auto center = resolve_body(args.option_or("center", "10"));
    const auto t = epoch_from(args, ctx);

    const coordinates::ReferenceFrame frame{center, coordinates::FrameAxes::J2000};
    const auto state = ctx.provider->state(body, t, frame);
    const double gm = ctx.provider->gravitational_parameter(center);

    const auto el = trajectory::elements_from_state(state.state, gm);

    std::cout << "body           : " << body.name() << "\n"
              << "central body   : " << center.name() << "  GM = " << fmt(gm, 16) << " m^3/s^2\n"
              << "epoch UTC      : " << ctx.time->to_utc_string(t, 6) << "\n"
              << "frame          : " << frame.to_string() << "\n\n"
              << el.to_string() << "\n\n"
              << "These are osculating elements: the conic the body would follow if every\n"
              << "other gravity source vanished at this instant.  They are a description, not\n"
              << "a prediction (docs/physics/gravity-model.md).\n";
    return 0;
}

int command_gravity(const Args& args) {
    const Context ctx = make_context(args);
    const auto center = resolve_body(args.option_or("center", "399"));
    const auto t = epoch_from(args, ctx);
    const Vec3 relative = parse_vec3(args.option_or("position", "6778000,0,0"));

    const auto catalog = celestial::BodyCatalog::default_solar_system(*ctx.provider);
    const coordinates::ReferenceFrame ssb = coordinates::ReferenceFrame::ssb_j2000();
    const gravity::PointMassGravity model{*ctx.provider, catalog, ssb};

    const Vec3 center_position = ctx.provider->position(center, t, ssb);
    const Vec3 absolute = center_position + relative;

    const auto contributions = model.contributions(absolute, t);

    Vec3 total{};
    for (const auto& c : contributions) {
        total += c.acceleration;
    }

    std::cout << "epoch UTC      : " << ctx.time->to_utc_string(t, 6) << "\n"
              << "position       : " << relative << " m relative to " << center.name() << "\n"
              << "               : " << absolute << " m in SSB/J2000\n\n"
              << std::left << std::setw(22) << "source" << std::setw(24) << "distance [m]"
              << std::setw(24) << "|a| [m/s^2]" << "fraction of total\n";

    const double total_magnitude = total.norm();
    for (const auto& c : contributions) {
        const double magnitude = c.acceleration.norm();
        std::cout << std::left << std::setw(22) << c.body.name() << std::setw(24) << fmt(c.distance, 12)
                  << std::setw(24) << fmt(magnitude, 12)
                  << fmt(total_magnitude > 0.0 ? magnitude / total_magnitude : 0.0, 6) << "\n";
    }
    std::cout << "\n";
    print_vector("total acceleration", total, "m/s^2");
    std::cout << "\nEvery source contributes at every instant: there is no sphere of influence\n"
                 "switching in this model (rule section 11).\n";
    return 0;
}

int command_lambert(const Args& args) {
    const Context ctx = make_context(args);
    const auto center = resolve_body(args.option_or("center", "399"));
    const auto t0 = epoch_from(args, ctx);

    const auto tof_days = args.option("tof");
    if (!tof_days.has_value()) {
        throw std::runtime_error("orbit-cli lambert requires --tof <days>");
    }
    const auto tof = time::Duration::days(std::stod(*tof_days));
    const auto t1 = t0 + tof;

    const auto target_name = args.option("to");
    if (!target_name.has_value()) {
        throw std::runtime_error("orbit-cli lambert requires --to <body>");
    }
    const auto target = resolve_body(*target_name);

    const coordinates::ReferenceFrame frame = coordinates::ReferenceFrame::centered_on(center);
    const double gm = ctx.provider->gravitational_parameter(center);

    // Departure: a body (we then also know its velocity, so we can quote a real
    // delta-v) or a bare position (we can only quote the required velocity).
    Vec3 r1{};
    Vec3 v_departure_body{};
    bool departure_is_body = false;
    std::string departure_label;

    const std::string from = args.option_or("from", "");
    if (from.empty() || from.find(',') != std::string::npos) {
        r1 = parse_vec3(from.empty() ? "6778000,0,0" : from);
        departure_label = "position " + std::string{from.empty() ? "6778000,0,0" : from};
    } else {
        const auto body = resolve_body(from);
        const auto state = ctx.provider->state(body, t0, frame);
        r1 = state.state.position;
        v_departure_body = state.state.velocity;
        departure_is_body = true;
        departure_label = body.name();
    }

    // Arrival: where the target WILL BE, not where it is now.  This is the whole
    // point of solving Lambert against an ephemeris.
    const auto target_state = ctx.provider->state(target, t1, frame);
    const Vec3 r2 = target_state.state.position;

    const auto solution = trajectory::solve_lambert(r1, r2, tof, gm);

    std::cout << "central body   : " << center.name() << "  GM = " << fmt(gm, 16) << " m^3/s^2\n"
              << "departure      : " << departure_label << " at " << ctx.time->to_utc_string(t0, 3)
              << "\n"
              << "arrival        : " << target.name() << " at " << ctx.time->to_utc_string(t1, 3)
              << "\n"
              << "time of flight : " << fmt(tof.days(), 10) << " d (" << fmt(tof.seconds(), 12)
              << " s)\n"
              << "transfer angle : " << fmt(units::rad_to_deg(solution.transfer_angle), 10)
              << " deg\n"
              << "transfer a     : " << fmt(solution.semi_major_axis, 12) << " m ("
              << (solution.semi_major_axis > 0.0 ? "elliptic" : "hyperbolic") << ")\n"
              << "bisection      : " << solution.iterations << " iterations, achieved tof "
              << fmt(solution.achieved_time_of_flight, 15) << " s\n\n";

    print_vector("departure velocity required", solution.departure_velocity, "m/s");
    std::cout << "\n";
    print_vector("arrival velocity", solution.arrival_velocity, "m/s");

    if (departure_is_body) {
        std::cout << "\n";
        print_vector("departure delta-v (from the body's own velocity)",
                     solution.departure_velocity - v_departure_body, "m/s");
    }

    const Vec3 arrival_relative = solution.arrival_velocity - target_state.state.velocity;
    std::cout << "\n";
    print_vector("arrival velocity relative to " + target.name(), arrival_relative, "m/s");

    std::cout << "\nThis is a two-body solution. Propagated in the full model the arrival will\n"
                 "miss by the perturbation accumulated over the transfer -- see\n"
                 "docs/physics/lambert.md section 5. Lambert is the first guess, not the answer.\n";
    return 0;
}

// Plans a Lambert intercept from a scenario's initial state, executes it as a
// finite burn, and reports how far the full model ends up from the two-body
// plan.  That last number is the point of the command (docs/physics/lambert.md
// section 5).
int command_intercept(const Args& args) {
    if (args.positional.empty()) {
        throw std::runtime_error("usage: orbit-cli intercept <scenario.json> --to <body> --tof <days>");
    }
    const Context ctx = make_context(args);
    orbitcli::Scenario scenario = orbitcli::Scenario::load(args.positional.front());
    if (const auto epoch = args.option("date"); epoch.has_value()) {
        scenario.epoch_text = *epoch;
    }

    if (!scenario.craft.has_value()) {
        throw std::runtime_error("intercept needs a scenario with spacecraft.engine configured");
    }
    const auto target_name = args.option("to");
    const auto tof_option = args.option("tof");
    if (!target_name.has_value() || !tof_option.has_value()) {
        throw std::runtime_error("intercept requires --to <body> and --tof <days>");
    }

    const auto target = resolve_body(*target_name);
    const auto tof = time::Duration::days(std::stod(*tof_option));
    const auto t0 = ctx.time->parse(scenario.epoch_text);
    const auto t1 = t0 + tof;

    const auto center = scenario.relative_to;
    const coordinates::ReferenceFrame ssb = coordinates::ReferenceFrame::ssb_j2000();
    const coordinates::ReferenceFrame centered = coordinates::ReferenceFrame::centered_on(center);
    const double gm = ctx.provider->gravitational_parameter(center);

    // Two-body plan, in the frame centred on the body we are leaving.
    const Vec3 r1 = scenario.position;
    const auto target_arrival = ctx.provider->state(target, t1, centered);
    const auto solution = trajectory::solve_lambert(r1, target_arrival.state.position, tof, gm);

    const Vec3 delta_v = solution.departure_velocity - scenario.velocity;

    std::cout << scenario.describe() << "\n\n"
              << "intercept target : " << target.name() << "\n"
              << "departure        : " << ctx.time->to_utc_string(t0, 3) << "\n"
              << "arrival          : " << ctx.time->to_utc_string(t1, 3) << "  (tof "
              << fmt(tof.days(), 8) << " d)\n"
              << "transfer angle   : " << fmt(units::rad_to_deg(solution.transfer_angle), 8)
              << " deg, a = " << fmt(solution.semi_major_axis, 12) << " m\n\n";
    print_vector("Lambert departure delta-v", delta_v, "m/s");

    const double delta_v_magnitude = delta_v.norm();
    const double budget = scenario.craft->delta_v_budget(scenario.craft->initial_mass());
    std::cout << "\nrequired  : " << fmt(delta_v_magnitude, 12) << " m/s\n"
              << "available : " << fmt(budget, 12) << " m/s\n";
    if (delta_v_magnitude > budget) {
        std::cout << "\nThe ship cannot fly this transfer. Try a longer time of flight.\n";
        return 2;
    }

    // Everything from here runs the FULL model.  `fly` maps a requested departure
    // velocity to where the ship actually ends up, executing the difference from
    // the current velocity as a finite burn.  That map is what the differential
    // corrector inverts.
    const auto catalog = celestial::BodyCatalog::resolve(*ctx.provider, scenario.bodies);
    const auto center_state = ctx.provider->state(center, t0, ssb);

    propagation::PropagationState initial{};
    initial.state.position = center_state.state.position + scenario.position;
    initial.state.velocity = center_state.state.velocity + scenario.velocity;
    initial.mass = scenario.craft->initial_mass();

    const auto target_at_arrival_ssb = ctx.provider->state(target, t1, ssb).state.position;

    struct Flight {
        navigation::MissionResult mission;
        navigation::ManeuverPlan plan;
    };

    auto fly_until = [&](const Vec3& departure_velocity, propagation::Trajectory* arc,
                         bool stop_on_impact, time::CoordinateTime end) -> Flight {
        const Vec3 impulse = departure_velocity - scenario.velocity;
        Flight flight{};

        auto maneuver = navigation::maneuver_for_delta_v(
            *scenario.craft, scenario.craft->initial_mass(), impulse.norm(), t0,
            navigation::GuidanceMode::Inertial, center, 1.0, "lambert injection");
        maneuver.ignition = t0;
        maneuver.inertial_direction = impulse.normalized();
        flight.plan.add(std::move(maneuver));

        gravity::CompositeForceModel forces;
        forces.add(std::make_unique<gravity::PointMassGravity>(*ctx.provider, catalog, ssb));
        for (const auto body : scenario.j2_bodies) {
            forces.add(gravity::OblatenessGravity::for_body(*ctx.provider, *ctx.provider, body, ssb));
        }
        navigation::ManeuverExecutor executor{*ctx.provider, *scenario.craft, flight.plan, ssb};
        forces.add_reference(executor);

        auto integrator = scenario.integrator;
        integrator.stop_inside_body = stop_on_impact;
        propagation::DormandPrince54Propagator propagator{forces, integrator};
        flight.mission = navigation::run_mission(propagator, executor, initial, t0, end, arc);
        return flight;
    };
    auto fly = [&](const Vec3& departure_velocity, propagation::Trajectory* arc,
                   bool stop_on_impact) -> Flight {
        return fly_until(departure_velocity, arc, stop_on_impact, t1);
    };

    // The B-plane probe flies PAST the nominal arrival. Closest approach has to be
    // INTERIOR to the window or it is pinned at the end of it -- and then the time
    // of closest approach stops responding to the departure velocity, the third
    // row of the Jacobian goes to zero, and Newton has nothing to solve. Measured
    // before the fix: d(time component)/dv = 2e-6 m per m/s, against 1e6 for the
    // other two.
    const auto t_probe_end = t1 + time::Duration::seconds(tof.seconds() * 0.25);

    // ---- closest approach, refined ------------------------------------------
    //
    // The B-plane is read off the osculating hyperbola about the target, and that
    // only describes the trajectory where the target dominates -- so it is
    // evaluated at the closest approach (docs/physics/b-plane.md section 6).
    //
    // Refined, not just argmin over samples: the corrector differences this map,
    // and an argmin over a fixed grid is a STAIRCASE. The time of closest approach
    // is itself one of the three targeted quantities, and quantised to the sample
    // spacing it would be useless as well as non-differentiable.
    struct Approach {
        time::CoordinateTime time{};
        double distance{std::numeric_limits<double>::infinity()};
        Vec3 relative_position{};
        Vec3 relative_velocity{};
        bool valid{false};
    };

    auto separation_at = [&](const propagation::Trajectory& arc_in,
                             time::CoordinateTime t) -> double {
        const auto state = arc_in.state_at(t);
        const auto body = ctx.provider->state(target, t, ssb);
        return (state.state.position - body.state.position).norm();
    };

    auto find_approach = [&](const propagation::Trajectory& arc_in) -> Approach {
        Approach best{};
        if (arc_in.empty()) {
            return best;
        }
        // Coarse bracket first.
        const auto samples = arc_in.sample(2000);
        std::size_t index = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto body = ctx.provider->state(target, samples[i].first, ssb);
            const double distance =
                (samples[i].second.state.position - body.state.position).norm();
            if (distance < best.distance) {
                best.distance = distance;
                best.time = samples[i].first;
                index = i;
            }
        }
        if (samples.size() < 3) {
            return best;
        }

        // Golden-section on the bracketing interval. 60 iterations takes a 4.5-day
        // arc's 200 s sampling down to well below a microsecond, which is far finer
        // than the finite-difference step needs.
        const std::size_t lo_index = index > 0 ? index - 1 : 0;
        const std::size_t hi_index = std::min(index + 1, samples.size() - 1);
        double lo = samples[lo_index].first.seconds_since_j2000();
        double hi = samples[hi_index].first.seconds_since_j2000();
        constexpr double kInvPhi = 0.6180339887498949;
        for (int i = 0; i < 60 && hi - lo > 1.0e-6; ++i) {
            const double a = hi - (hi - lo) * kInvPhi;
            const double b = lo + (hi - lo) * kInvPhi;
            const auto ta = time::CoordinateTime::from_seconds_since_j2000(a);
            const auto tb = time::CoordinateTime::from_seconds_since_j2000(b);
            if (!arc_in.contains(ta) || !arc_in.contains(tb)) {
                break;
            }
            if (separation_at(arc_in, ta) < separation_at(arc_in, tb)) {
                hi = b;
            } else {
                lo = a;
            }
        }
        const auto t_ca = time::CoordinateTime::from_seconds_since_j2000(0.5 * (lo + hi));
        if (!arc_in.contains(t_ca)) {
            return best;
        }
        const auto state = arc_in.state_at(t_ca);
        const auto body = ctx.provider->state(target, t_ca, ssb);
        best.time = t_ca;
        best.relative_position = state.state.position - body.state.position;
        best.relative_velocity = state.state.velocity - body.state.velocity;
        best.distance = best.relative_position.norm();
        best.valid = true;
        return best;
    };

    const double target_gm = ctx.provider->gravitational_parameter(target);
    const double target_radius_for_aim = ctx.provider->mean_radius(target);
    const auto flyby_altitude = args.option("flyby-altitude-km");
    const auto b_plane_angle =
        units::Angle::degrees(std::stod(args.option_or("b-plane-angle", "0")));

    Vec3 departure_velocity = solution.departure_velocity;

    if (!args.has_flag("no-retarget")) {
        navigation::TargetingConfig targeting{};
        if (const auto tolerance = args.option("tolerance-km"); tolerance.has_value()) {
            targeting.position_tolerance = std::stod(*tolerance) * 1000.0;
        }
        if (const auto step = args.option("fd-step"); step.has_value()) {
            targeting.velocity_step = std::stod(*step);
        }

        if (flyby_altitude.has_value()) {
            if (!(target_radius_for_aim > 0.0) || !(target_gm > 0.0)) {
                throw std::runtime_error(
                    "--flyby-altitude-km needs a target with a radius and a GM in the kernels");
            }
            const double wanted_periapsis =
                target_radius_for_aim + std::stod(*flyby_altitude) * 1000.0;
            // How close the periapsis has to land. --tolerance-km means PERIAPSIS
            // here, which is what somebody asking for a 100 km flyby means by it.
            const double wanted_periapsis_tolerance =
                std::stod(args.option_or("tolerance-km", "1.0")) * 1000.0;

            // The map the corrector inverts: departure velocity -> (B.T, B.R,
            // along-track timing error). Three outputs for three inputs, and all
            // three in METRES, so that the norm the corrector tests against a
            // tolerance means something (section 5).
            double v_infinity_estimate = 0.0;
            auto b_plane_of = [&](const Vec3& v) -> std::optional<navigation::BPlane> {
                propagation::Trajectory probe;
                (void)fly_until(v, &probe, false, t_probe_end);
                const Approach approach = find_approach(probe);
                if (!approach.valid) {
                    return std::nullopt;
                }
                try {
                    return navigation::b_plane_from_state(approach.relative_position,
                                                          approach.relative_velocity, target_gm);
                } catch (const std::domain_error&) {
                    return std::nullopt;   // not hyperbolic about the target
                }
            };

            // v_infinity is needed to turn "100 km altitude" into a |B|, and it is
            // itself a property of the trajectory. So: measure it, aim, re-measure.
            // It barely moves -- the transfer geometry sets it, not the aim point --
            // and the outer loop is there to prove that rather than to assume it.
            navigation::TargetingResult correction{};

            // Stage one: the ORDINARY position targeting, aimed at the body centre.
            //
            // Not an optimisation -- a necessity. From the raw Lambert solution the
            // trajectory misses by 363 000 km, and at that distance there is no
            // hyperbolic flyby to read a B-plane off: the three Jacobian columns
            // come out nearly parallel and Newton has no direction to move in.
            // Measured. So the first stage gets the spacecraft to the Moon at all,
            // and only then is there a flyby to shape.
            const auto reach = navigation::correct_departure(
                [&](const Vec3& v) { return fly(v, nullptr, false).mission.state.state.position; },
                solution.departure_velocity, target_at_arrival_ssb, targeting);
            std::cout << "\nstage 1, reach the body (position targeting):\n"
                      << "  initial miss   : " << fmt(reach.initial_miss / 1000.0, 10) << " km\n"
                      << "  final miss     : " << fmt(reach.miss_distance / 1000.0, 10)
                      << " km   " << reach.message << "\n";

            Vec3 guess = reach.departure_velocity;
            std::cout << "\nstage 2, open the periapsis (B-plane targeting), aiming a flyby at "
                      << fmt(std::stod(*flyby_altitude), 6) << " km altitude:\n";

            for (int pass = 0; pass < 4; ++pass) {
                const auto measured = b_plane_of(guess);
                if (!measured.has_value()) {
                    std::cout << "  pass " << pass + 1
                              << "        : no hyperbolic approach to read v_inf from; "
                                 "the transfer does not reach the target\n";
                    break;
                }
                v_infinity_estimate = measured->v_infinity;
                const auto aim = navigation::aim_for_periapsis(wanted_periapsis, target_gm,
                                                               v_infinity_estimate,
                                                               b_plane_angle);

                // The corrector's tolerance is on |B|, but what the mission cares
                // about is the PERIAPSIS. Differentiating b^2 = r_p^2 + 2 mu r_p /
                // v_inf^2 gives
                //
                //     dr_p / db = b / (r_p + mu / v_inf^2)
                //
                // which here is 0.61: a 10 km slack on B is 6 km of periapsis, and
                // the first version of this stopped 5.4 km short of the altitude
                // asked for because of exactly that. So the tolerance is converted
                // rather than shared.
                const double aim_magnitude = std::hypot(aim.b_dot_t, aim.b_dot_r);
                const double dr_p_db =
                    aim_magnitude / (wanted_periapsis +
                                     target_gm / (v_infinity_estimate * v_infinity_estimate));
                auto b_targeting = targeting;
                b_targeting.position_tolerance = wanted_periapsis_tolerance / dr_p_db;
                // The third constraint is the time of flight that was asked for:
                // closest approach should happen at the nominal arrival epoch.
                const double t_target = t1.seconds_since_j2000();

                std::cout << "  pass " << pass + 1 << "        : v_inf "
                          << fmt(v_infinity_estimate, 8) << " m/s, aim |B| "
                          << fmt(aim_magnitude / 1000.0, 9) << " km (focusing x"
                          << fmt(aim_magnitude / wanted_periapsis, 5) << "), r_p now "
                          << fmt(measured->periapsis_radius / 1000.0, 9) << " km\n";

                correction = navigation::correct_departure(
                    [&](const Vec3& v) -> Vec3 {
                        propagation::Trajectory probe;
                        (void)fly_until(v, &probe, false, t_probe_end);
                        const Approach approach = find_approach(probe);
                        if (!approach.valid) {
                            return Vec3{1.0e12, 1.0e12, 1.0e12};   // push the solver away
                        }
                        try {
                            const auto bp = navigation::b_plane_from_state(
                                approach.relative_position, approach.relative_velocity,
                                target_gm);
                            return Vec3{bp.b_dot_t, bp.b_dot_r,
                                        bp.v_infinity *
                                            (approach.time.seconds_since_j2000() - t_target)};
                        } catch (const std::domain_error&) {
                            return Vec3{1.0e12, 1.0e12, 1.0e12};
                        }
                    },
                    guess, Vec3{aim.b_dot_t, aim.b_dot_r, 0.0}, b_targeting);
                guess = correction.departure_velocity;

                const auto achieved = b_plane_of(guess);
                if (!achieved.has_value()) {
                    break;
                }
                if (std::abs(achieved->periapsis_radius - wanted_periapsis) <
                    wanted_periapsis_tolerance) {
                    std::cout << "  converged     : r_p "
                              << fmt(achieved->periapsis_radius / 1000.0, 9) << " km, wanted "
                              << fmt(wanted_periapsis / 1000.0, 9) << " km\n";
                    break;
                }
            }

            std::cout << "  initial miss   : " << fmt(correction.initial_miss / 1000.0, 10)
                      << " km (in the B-plane)\n"
                      << "  final miss     : " << fmt(correction.miss_distance / 1000.0, 10)
                      << " km\n"
                      << "  iterations     : " << correction.iterations << " ("
                      << correction.evaluations << " trajectories)\n"
                      << "  status         : " << correction.message << "\n";
            print_vector("  correction to the Lambert velocity",
                         correction.departure_velocity - solution.departure_velocity, "m/s");
            departure_velocity = correction.departure_velocity;
        } else {

        const auto correction = navigation::correct_departure(
            // Targeting flies THROUGH the target if it has to: an iterate that
            // clips the Moon is still a useful data point for the Jacobian, and
            // stopping there would put a cliff in the middle of the map.
            [&](const Vec3& v) { return fly(v, nullptr, false).mission.state.state.position; },
            solution.departure_velocity, target_at_arrival_ssb, targeting);

        std::cout << "\ndifferential targeting (full model):\n"
                  << "  initial miss   : " << fmt(correction.initial_miss / 1000.0, 10) << " km\n"
                  << "  final miss     : " << fmt(correction.miss_distance / 1000.0, 10) << " km\n"
                  << "  iterations     : " << correction.iterations << " ("
                  << correction.evaluations << " trajectories)\n"
                  << "  status         : " << correction.message << "\n";
        print_vector("  correction to the Lambert velocity",
                     correction.departure_velocity - solution.departure_velocity, "m/s");
        departure_velocity = correction.departure_velocity;
        }
    }

    propagation::Trajectory arc;
    const Flight flight = fly(departure_velocity, &arc, true);
    const auto& mission = flight.mission;

    // Closest approach over the whole arc, which is the number an intercept is
    // actually judged by -- the state at the nominal arrival epoch says nothing
    // if the ship passed the target an hour earlier.
    double closest_approach = std::numeric_limits<double>::infinity();
    time::CoordinateTime closest_time = mission.time;
    for (const auto& [t, state] : arc.sample(4000)) {
        const auto body = ctx.provider->state(target, t, ssb);
        const double distance = (state.state.position - body.state.position).norm();
        if (distance < closest_approach) {
            closest_approach = distance;
            closest_time = t;
        }
    }

    std::cout << "\nburn:\n" << mission.describe_burns() << "\n";
    if (!mission.ok()) {
        std::cout << "\npropagation stopped: " << propagation::to_string(mission.status) << " -- "
                  << mission.message << "\n";
    }

    const auto target_final = ctx.provider->state(target, mission.time, ssb);
    const Vec3 miss = mission.state.state.position - target_final.state.position;
    const double target_radius = ctx.provider->mean_radius(target);

    std::cout << "\narrival (full model):\n";
    print_vector("position relative to " + target.name(), miss, "m");
    std::cout << "\nmiss at arrival  : " << fmt(miss.norm() / 1000.0, 10) << " km";
    if (target_radius > 0.0) {
        std::cout << "  (" << fmt(miss.norm() / target_radius, 6) << " target radii)";
    }
    std::cout << "\nclosest approach : " << fmt(closest_approach / 1000.0, 10) << " km at "
              << ctx.time->to_utc_string(closest_time, 0);
    if (target_radius > 0.0 && closest_approach < target_radius) {
        std::cout << "\n                   *** that is INSIDE " << target.name()
                  << ": this is an impact trajectory, not a flyby."
                     "\n                       Aim one with --flyby-altitude-km"
                     " (docs/physics/b-plane.md) ***";
    } else if (target_radius > 0.0) {
        std::cout << "  (altitude " << fmt((closest_approach - target_radius) / 1000.0, 8)
                  << " km)";
    }
    std::cout << "\nrelative speed   : "
              << fmt((mission.state.state.velocity - target_final.state.velocity).norm(), 10)
              << " m/s\n"
              << (args.has_flag("no-retarget")
                      ? "\nThis is the raw two-body plan. The miss is the perturbation it ignored\n"
                        "plus the gravity loss of executing an impulse as a finite burn. Drop\n"
                        "--no-retarget to let the differential corrector close it.\n"
                      : "\nThe corrector inverted the FULL model, so the residual miss is the\n"
                        "corrector's own tolerance, not a modelling error.\n")
              << "\nintegrator: " << mission.stats.to_string() << "\n";

    // ---- lunar orbit insertion ----------------------------------------------
    //
    // Arriving is not staying. The flyby is hyperbolic by construction, so without
    // a burn the spacecraft passes the target and leaves. This plans the burn at
    // periapsis, then EXECUTES it in the same full model -- finite, with its own
    // gravity loss -- and reports the orbit it actually ends up in.
    if (args.has_flag("insert")) {
        propagation::Trajectory approach_arc;
        const Flight approach_flight =
            fly_until(departure_velocity, &approach_arc, false, t_probe_end);
        const Approach approach = find_approach(approach_arc);
        if (!approach.valid || !(target_gm > 0.0)) {
            std::cout << "\ninsertion: no usable approach to insert from\n";
        } else {
            const double apoapsis_altitude =
                std::stod(args.option_or("insert-apoapsis-km", "0")) * 1000.0;
            const double apoapsis_radius =
                apoapsis_altitude > 0.0 ? target_radius + apoapsis_altitude : 0.0;

            const auto bp = navigation::b_plane_from_state(approach.relative_position,
                                                           approach.relative_velocity, target_gm);
            const auto burn = navigation::plan_insertion(bp.periapsis_radius, target_gm,
                                                         bp.v_infinity, apoapsis_radius);

            std::cout << "\ninsertion at periapsis (" << ctx.time->to_utc_string(approach.time, 0)
                      << "):\n"
                      << "  v_infinity     : " << fmt(bp.v_infinity, 10) << " m/s\n"
                      << "  periapsis      : " << fmt(bp.periapsis_radius / 1000.0, 10)
                      << " km  (altitude " << fmt((bp.periapsis_radius - target_radius) / 1000.0, 8)
                      << " km)\n"
                      << "  speed there    : " << fmt(burn.periapsis_speed, 10) << " m/s\n"
                      << "  target speed   : " << fmt(burn.target_speed, 10) << " m/s\n"
                      << "  delta-v needed : " << fmt(burn.delta_v, 10) << " m/s  (retrograde)\n"
                      << "  orbit period   : " << fmt(burn.period / 60.0, 8) << " min\n";

            // Execute it. Retrograde guidance about the target, centred on
            // periapsis, so the finite burn straddles the point it was planned for
            // instead of starting there and drifting off.
            const double mass_at_periapsis = approach_flight.mission.state.mass;
            auto insertion = navigation::maneuver_for_delta_v(
                *scenario.craft, mass_at_periapsis, burn.delta_v, approach.time,
                navigation::GuidanceMode::Retrograde, target, 1.0, "lunar orbit insertion",
                navigation::BurnCentering::CenterOnIgnition);

            navigation::ManeuverPlan plan;
            {
                const Vec3 impulse = departure_velocity - scenario.velocity;
                auto injection = navigation::maneuver_for_delta_v(
                    *scenario.craft, scenario.craft->initial_mass(), impulse.norm(), t0,
                    navigation::GuidanceMode::Inertial, center, 1.0, "lambert injection");
                injection.ignition = t0;
                injection.inertial_direction = impulse.normalized();
                plan.add(std::move(injection));
            }
            plan.add(std::move(insertion));

            gravity::CompositeForceModel forces;
            forces.add(std::make_unique<gravity::PointMassGravity>(*ctx.provider, catalog, ssb));
            for (const auto body : scenario.j2_bodies) {
                forces.add(gravity::OblatenessGravity::for_body(*ctx.provider, *ctx.provider,
                                                                body, ssb));
            }
            navigation::ManeuverExecutor executor{*ctx.provider, *scenario.craft, plan, ssb};
            forces.add_reference(executor);

            auto integrator = scenario.integrator;
            integrator.stop_inside_body = true;
            propagation::DormandPrince54Propagator propagator{forces, integrator};

            // Two orbits past the burn, so the result is an ORBIT and not a lucky
            // instant: the elements are reported after a full revolution.
            const auto t_end = approach.time + time::Duration::seconds(burn.period * 2.0);
            propagation::Trajectory captured_arc;
            const auto captured = navigation::run_mission(propagator, executor, initial, t0,
                                                          t_end, &captured_arc);

            std::cout << "\n" << captured.describe_burns() << "\n";
            if (!captured.ok()) {
                std::cout << "propagation stopped: " << propagation::to_string(captured.status)
                          << " -- " << captured.message << "\n";
            }

            const auto moon_final = ctx.provider->state(target, captured.time, ssb);
            coordinates::StateVector relative_state{};
            relative_state.position = captured.state.state.position - moon_final.state.position;
            relative_state.velocity = captured.state.state.velocity - moon_final.state.velocity;
            const auto relative = trajectory::elements_from_state(relative_state, target_gm);

            std::cout << "\norbit about " << target.name() << ", one revolution after the burn:\n"
                      << "  periapsis      : " << fmt(relative.periapsis_radius / 1000.0, 10)
                      << " km  (altitude "
                      << fmt((relative.periapsis_radius - target_radius) / 1000.0, 8) << " km)\n"
                      << "  apoapsis       : " << fmt(relative.apoapsis_radius / 1000.0, 10)
                      << " km  (altitude "
                      << fmt((relative.apoapsis_radius - target_radius) / 1000.0, 8) << " km)\n"
                      << "  eccentricity   : " << fmt(relative.eccentricity, 10) << "\n"
                      << "  inclination    : " << fmt(relative.inclination.degrees(), 8)
                      << " deg\n"
                      << "  period         : " << fmt(relative.period / 60.0, 8) << " min\n"
                      << "  propellant left: " << fmt(captured.state.mass -
                                                      scenario.craft->dry_mass(), 10) << " kg\n";
            if (relative.eccentricity < 1.0) {
                std::cout << "\nCAPTURED: the orbit is closed about " << target.name() << ".\n";
            } else {
                std::cout << "\nNOT captured: still hyperbolic (e = "
                          << fmt(relative.eccentricity, 6) << ").\n";
            }
        }
    }

    if (const auto csv = args.option("csv"); csv.has_value()) {
        std::ofstream out{*csv};
        if (!out) {
            throw std::runtime_error("cannot write " + *csv);
        }
        out << std::setprecision(17)
            << "t_tdb_s,x_m,y_m,z_m,vx_ms,vy_ms,vz_ms,rel_x_m,rel_y_m,rel_z_m,"
               "rel_vx_ms,rel_vy_ms,rel_vz_ms,radius_m,speed_ms,sma_m,ecc,inc_deg,raan_deg,"
               "argp_deg,energy_j_kg,angular_momentum_m2_s\n";
        for (const auto& [t, state] : arc.sample(2000)) {
            const auto body_state = ctx.provider->state(center, t, ssb);
            const coordinates::StateVector relative{
                state.state.position - body_state.state.position,
                state.state.velocity - body_state.state.velocity};
            const auto el = trajectory::elements_from_state(relative, gm);
            out << t.seconds_since_j2000() << "," << state.state.position.x << ","
                << state.state.position.y << "," << state.state.position.z << ","
                << state.state.velocity.x << "," << state.state.velocity.y << ","
                << state.state.velocity.z << "," << relative.position.x << ","
                << relative.position.y << "," << relative.position.z << "," << relative.velocity.x
                << "," << relative.velocity.y << "," << relative.velocity.z << ","
                << relative.radius() << "," << relative.speed() << "," << el.semi_major_axis << ","
                << el.eccentricity << "," << el.inclination.degrees() << ","
                << el.raan.degrees() << "," << el.argument_of_periapsis.degrees()
                << "," << el.specific_energy << "," << el.specific_angular_momentum << "\n";
        }
        std::cout << "csv written: " << *csv << "\n";
    }
    return mission.ok() ? 0 : 1;
}

int command_propagate(const Args& args) {
    if (args.positional.empty()) {
        throw std::runtime_error("usage: orbit-cli propagate <scenario.json>");
    }
    const Context ctx = make_context(args);
    orbitcli::Scenario scenario = orbitcli::Scenario::load(args.positional.front());
    if (const auto epoch = args.option("date"); epoch.has_value()) {
        scenario.epoch_text = *epoch;
    }
    if (const auto csv = args.option("csv"); csv.has_value()) {
        scenario.csv_path = *csv;
    }
    if (const auto samples = args.option("samples"); samples.has_value()) {
        scenario.samples = std::max(1, std::stoi(*samples));
    }
    if (args.has_flag("j2") && scenario.j2_bodies.empty()) {
        scenario.j2_bodies.push_back(scenario.relative_to);
    }

    const auto t0 = ctx.time->parse(scenario.epoch_text);
    const auto t1 = t0 + time::Duration::seconds(scenario.duration_seconds);

    const auto catalog = celestial::BodyCatalog::resolve(*ctx.provider, scenario.bodies);
    const coordinates::ReferenceFrame ssb = coordinates::ReferenceFrame::ssb_j2000();

    gravity::CompositeForceModel forces;
    forces.add(std::make_unique<gravity::PointMassGravity>(*ctx.provider, catalog, ssb));
    for (const auto body : scenario.j2_bodies) {
        forces.add(gravity::OblatenessGravity::for_body(*ctx.provider, *ctx.provider, body, ssb));
    }

    // Initial state: given relative to a body, integrated in the barycentric frame.
    const auto center_state = ctx.provider->state(scenario.relative_to, t0, ssb);
    propagation::PropagationState initial{};
    initial.state.position = center_state.state.position + scenario.position;
    initial.state.velocity = center_state.state.velocity + scenario.velocity;
    initial.mass = scenario.initial_mass();

    // The plan.  A burn given as a delta-v is turned into a duration here, using
    // the mass the ship will have when it reaches that burn -- which is known in
    // advance because each earlier burn consumes q times its own duration.
    navigation::ManeuverPlan plan;
    double running_mass = initial.mass;
    if (!scenario.maneuvers.empty()) {
        for (const auto& entry : scenario.maneuvers) {
            const auto& engine = scenario.craft->engine();
            navigation::Maneuver maneuver{};
            maneuver.name = entry.name;
            maneuver.ignition = t0 + time::Duration::seconds(entry.ignition_s);
            maneuver.throttle = entry.throttle;
            maneuver.guidance = entry.guidance;
            maneuver.reference = entry.reference.value_or(scenario.relative_to);
            maneuver.inertial_direction = entry.inertial_direction;

            if (entry.duration_s.has_value()) {
                maneuver.duration = time::Duration::seconds(*entry.duration_s);
            } else {
                maneuver.duration = time::Duration::seconds(engine.burn_duration_for_delta_v(
                    running_mass, std::abs(*entry.delta_v_ms), entry.throttle));
                if (*entry.delta_v_ms < 0.0 && maneuver.guidance == navigation::GuidanceMode::Prograde) {
                    maneuver.guidance = navigation::GuidanceMode::Retrograde;
                }
            }
            running_mass -= engine.mass_flow_at(maneuver.throttle) * maneuver.duration.seconds();
            plan.add(std::move(maneuver));
        }
    }

    const double gm_center = ctx.provider->gravitational_parameter(scenario.relative_to);
    const coordinates::StateVector initial_relative{scenario.position, scenario.velocity};
    const auto elements0 = trajectory::elements_from_state(initial_relative, gm_center);

    // A ship with an engine gets an executor even when the plan is empty, so that
    // the force model printed below is the one that actually ran.
    std::optional<navigation::ManeuverExecutor> executor;
    if (scenario.craft.has_value()) {
        executor.emplace(*ctx.provider, *scenario.craft, plan, ssb);
        forces.add_reference(*executor);
    }

    std::cout << scenario.describe() << "\n\n"
              << "force model    : " << forces.describe() << "\n"
              << "plan           :\n" << plan.describe() << "\n"
              << "epoch UTC      : " << ctx.time->to_utc_string(t0, 6) << "\n"
              << "end UTC        : " << ctx.time->to_utc_string(t1, 6) << "\n\n"
              << "initial osculating elements about " << scenario.relative_to.name() << ":\n"
              << elements0.to_string() << "\n\n";

    // One continuous integration, then sampled through the dense output.  Not
    // segment-by-segment: restarting the propagator at every sample restarts the
    // step controller too, which makes the trajectory depend on how many rows the
    // user asked to see.  See ADR-0006.
    propagation::Trajectory arc;
    propagation::DormandPrince54Propagator propagator{forces, scenario.integrator};

    propagation::PropagationStatus status = propagation::PropagationStatus::Success;
    std::string message;
    propagation::IntegratorStats stats{};
    propagation::PropagationState final_state{};
    time::CoordinateTime final_time = t0;
    std::string burn_report;

    if (executor.has_value()) {
        const auto mission =
            navigation::run_mission(propagator, *executor, initial, t0, t1, &arc);
        status = mission.status;
        message = mission.message;
        stats = mission.stats;
        final_state = mission.state;
        final_time = mission.time;
        burn_report = mission.describe_burns();
    } else {
        propagator.set_trajectory_recorder(&arc);
        const auto result = propagator.propagate(initial, t0, t1);
        status = result.status;
        message = result.message;
        stats = result.stats;
        final_state = result.state;
        final_time = result.time;
    }

    if (status != propagation::PropagationStatus::Success) {
        std::cout << "propagation stopped: " << propagation::to_string(status) << " -- " << message
                  << "\n\n";
    }

    std::ofstream csv;
    if (!scenario.csv_path.empty()) {
        csv.open(scenario.csv_path);
        if (!csv) {
            throw std::runtime_error("cannot write " + scenario.csv_path);
        }
        csv << std::setprecision(17)
            << "t_tdb_s,x_m,y_m,z_m,vx_ms,vy_ms,vz_ms,rel_x_m,rel_y_m,rel_z_m,"
               "rel_vx_ms,rel_vy_ms,rel_vz_ms,"
               "radius_m,speed_ms,sma_m,ecc,inc_deg,raan_deg,argp_deg,"
               "energy_j_kg,angular_momentum_m2_s\n";
    }

    std::cout << std::left << std::setw(16) << "t [s]" << std::setw(22) << "radius [m]"
              << std::setw(22) << "speed [m/s]" << std::setw(22) << "energy [J/kg]"
              << "h [m^2/s]\n";

    const auto rows = arc.sample(static_cast<std::size_t>(scenario.samples) + 1);
    const std::size_t print_every =
        rows.size() > 24 ? (rows.size() + 23) / 24 : 1;  // keep the table readable

    double specific_energy_initial = 0.0;
    double angular_momentum_initial = 0.0;
    trajectory::OrbitalElements elements_final{};

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& [t, state] = rows[i];
        const auto body_state = ctx.provider->state(scenario.relative_to, t, ssb);
        const coordinates::StateVector relative{state.state.position - body_state.state.position,
                                                state.state.velocity - body_state.state.velocity};
        const auto el = trajectory::elements_from_state(relative, gm_center);

        if (i == 0) {
            specific_energy_initial = el.specific_energy;
            angular_momentum_initial = el.specific_angular_momentum;
        }
        elements_final = el;

        if (i % print_every == 0 || i + 1 == rows.size()) {
            std::cout << std::left << std::setw(16) << fmt((t - t0).seconds(), 10) << std::setw(22)
                      << fmt(relative.radius(), 15) << std::setw(22) << fmt(relative.speed(), 15)
                      << std::setw(22) << fmt(el.specific_energy, 15)
                      << fmt(el.specific_angular_momentum, 15) << "\n";
        }

        if (csv.is_open()) {
            csv << t.seconds_since_j2000() << "," << state.state.position.x << ","
                << state.state.position.y << "," << state.state.position.z << ","
                << state.state.velocity.x << "," << state.state.velocity.y << ","
                << state.state.velocity.z << "," << relative.position.x << ","
                << relative.position.y << "," << relative.position.z << ","
                << relative.velocity.x << "," << relative.velocity.y << ","
                << relative.velocity.z << "," << relative.radius() << "," << relative.speed()
                << "," << el.semi_major_axis << "," << el.eccentricity << ","
                << el.inclination.degrees() << "," << el.raan.degrees() << ","
                << el.argument_of_periapsis.degrees() << "," << el.specific_energy << ","
                << el.specific_angular_momentum << "\n";
        }
    }

    std::cout << "\nfinal osculating elements about " << scenario.relative_to.name() << ":\n"
              << elements_final.to_string() << "\n\n"
              << "conservation over the run (two-body reference quantities):\n"
              << "  specific energy drift   : "
              << fmt(std::abs(elements_final.specific_energy - specific_energy_initial) /
                         std::abs(specific_energy_initial), 6)
              << " relative\n"
              << "  angular momentum drift  : "
              << fmt(std::abs(elements_final.specific_angular_momentum - angular_momentum_initial) /
                         angular_momentum_initial, 6)
              << " relative\n"
              << "  (non-zero drift here is physics -- third bodies, J2 -- plus integration error;\n"
              << "   see tests/scientific for the isolated two-body check)\n\n"
              << (burn_report.empty() ? "" : "burns:\n" + burn_report + "\n\n")
              << "integrator: " << stats.to_string() << "\n"
              << "dense output: " << arc.size() << " segments, "
              << fmt(static_cast<double>(arc.size() * sizeof(propagation::DenseSegment)) / 1024.0, 4)
              << " kB; sampled " << rows.size() << " states at no extra force evaluation\n"
              << "final mass: " << fmt(final_state.mass, 12) << " kg"
              << (scenario.craft ? "  (propellant left " +
                                       fmt(scenario.craft->propellant_at(final_state.mass), 10) +
                                       " kg, remaining budget " +
                                       fmt(scenario.craft->delta_v_budget(final_state.mass), 10) +
                                       " m/s)"
                                 : std::string{})
              << "\n"
              << "proper time elapsed: " << fmt(final_state.proper_time.seconds(), 17) << " s"
              << "  (coordinate " << fmt((final_time - t0).seconds(), 17)
              << " s; equal while Newtonian)\n";

    if (csv.is_open()) {
        std::cout << "csv written: " << scenario.csv_path << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);

    if (args.command.empty() || args.command == "help" || args.has_flag("help")) {
        std::cout << kUsage;
        return args.command.empty() ? 1 : 0;
    }

    try {
        if (args.command == "kernels")   return command_kernels(args);
        if (args.command == "bodies")    return command_bodies(args);
        if (args.command == "time")      return command_time(args);
        if (args.command == "body")      return command_body(args);
        if (args.command == "elements")  return command_elements(args);
        if (args.command == "gravity")   return command_gravity(args);
        if (args.command == "propagate") return command_propagate(args);
        if (args.command == "lambert")   return command_lambert(args);
        if (args.command == "intercept") return command_intercept(args);

        std::cerr << "unknown command \"" << args.command << "\"\n\n" << kUsage;
        return 1;
    } catch (const ephemeris::EphemerisUnavailable& e) {
        std::cerr << "no data: " << e.what() << "\n";
        return 3;
    } catch (const ephemeris::SpiceError& e) {
        std::cerr << "spice error: " << e.what() << "\n";
        return 4;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
