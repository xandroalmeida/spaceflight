// orbit-cli -- command line access to the scientific core.
//
// This is the verification surface of Milestone 0: everything the simulation
// knows can be interrogated here, without Godot, without a window, and in a form
// that can be diffed against JPL Horizons or an analytic result.

#include "core/celestial/body_catalog.hpp"
#include "core/ephemeris/errors.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "core/gravity/point_mass_gravity.hpp"
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
  orbit-cli propagate <scenario.json> [--csv <file>]

Options:
  --date <epoch>    Any format SPICE accepts: "2026-01-01", "2026-01-01T12:00:00",
                    "2026 JAN 01 12:00 TDB".  A bare timestamp is UTC.  Default: J2000.
  --origin <body>   Origin of the reference frame.  Default: SSB.
  --center <body>   Central body for orbital elements / gravity queries.
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

int command_propagate(const Args& args) {
    if (args.positional.empty()) {
        throw std::runtime_error("usage: orbit-cli propagate <scenario.json>");
    }
    const Context ctx = make_context(args);
    orbitcli::Scenario scenario = orbitcli::Scenario::load(args.positional.front());
    if (const auto csv = args.option("csv"); csv.has_value()) {
        scenario.csv_path = *csv;
    }

    const auto t0 = ctx.time->parse(scenario.epoch_text);
    const auto t1 = t0 + time::Duration::seconds(scenario.duration_seconds);

    const auto catalog = celestial::BodyCatalog::resolve(*ctx.provider, scenario.bodies);
    const coordinates::ReferenceFrame ssb = coordinates::ReferenceFrame::ssb_j2000();
    const gravity::PointMassGravity gravity_model{*ctx.provider, catalog, ssb};

    // Initial state: given relative to a body, integrated in the barycentric frame.
    const auto center_state = ctx.provider->state(scenario.relative_to, t0, ssb);
    propagation::PropagationState initial{};
    initial.state.position = center_state.state.position + scenario.position;
    initial.state.velocity = center_state.state.velocity + scenario.velocity;
    initial.mass = scenario.mass;

    propagation::DormandPrince54Propagator propagator{gravity_model, scenario.integrator};

    const double gm_center = ctx.provider->gravitational_parameter(scenario.relative_to);
    const coordinates::StateVector initial_relative{scenario.position, scenario.velocity};
    const auto elements0 = trajectory::elements_from_state(initial_relative, gm_center);

    std::cout << scenario.describe() << "\n\n"
              << "epoch UTC      : " << ctx.time->to_utc_string(t0, 6) << "\n"
              << "end UTC        : " << ctx.time->to_utc_string(t1, 6) << "\n\n"
              << "initial osculating elements about " << scenario.relative_to.name() << ":\n"
              << elements0.to_string() << "\n\n";

    std::ofstream csv;
    if (!scenario.csv_path.empty()) {
        csv.open(scenario.csv_path);
        if (!csv) {
            throw std::runtime_error("cannot write " + scenario.csv_path);
        }
        csv << std::setprecision(17)
            << "t_tdb_s,x_m,y_m,z_m,vx_ms,vy_ms,vz_ms,rel_x_m,rel_y_m,rel_z_m,"
               "radius_m,speed_ms,sma_m,ecc,energy_j_kg,angular_momentum_m2_s\n";
    }

    // Sampling is done by propagating segment by segment, so every printed row
    // is an exact requested epoch rather than whatever step the integrator took.
    propagation::PropagationState state = initial;
    time::CoordinateTime t = t0;

    propagation::IntegratorStats totals{};
    double specific_energy_initial = 0.0;
    double angular_momentum_initial = 0.0;

    std::cout << std::left << std::setw(16) << "t [s]" << std::setw(22) << "radius [m]"
              << std::setw(22) << "speed [m/s]" << std::setw(22) << "energy [J/kg]" << "h [m^2/s]\n";

    for (int i = 0; i <= scenario.samples; ++i) {
        const double fraction = static_cast<double>(i) / static_cast<double>(scenario.samples);
        const auto target = t0 + time::Duration::seconds(scenario.duration_seconds * fraction);

        if (i > 0) {
            const auto result = propagator.propagate(state, t, target);
            totals.accepted_steps += result.stats.accepted_steps;
            totals.rejected_steps += result.stats.rejected_steps;
            totals.force_evaluations += result.stats.force_evaluations;
            totals.max_error_estimate = std::max(totals.max_error_estimate, result.stats.max_error_estimate);
            totals.wall_time_seconds += result.stats.wall_time_seconds;
            totals.max_step_seconds = std::max(totals.max_step_seconds, result.stats.max_step_seconds);
            totals.min_step_seconds = totals.min_step_seconds == 0.0
                                          ? result.stats.min_step_seconds
                                          : std::min(totals.min_step_seconds, result.stats.min_step_seconds);
            state = result.state;
            t = result.time;
            if (!result.ok()) {
                std::cout << "\npropagation stopped: " << propagation::to_string(result.status) << " -- "
                          << result.message << "\n";
                break;
            }
        }

        const auto body_state = ctx.provider->state(scenario.relative_to, t, ssb);
        const coordinates::StateVector relative{state.state.position - body_state.state.position,
                                                state.state.velocity - body_state.state.velocity};
        const auto el = trajectory::elements_from_state(relative, gm_center);

        if (i == 0) {
            specific_energy_initial = el.specific_energy;
            angular_momentum_initial = el.specific_angular_momentum;
        }

        std::cout << std::left << std::setw(16) << fmt((t - t0).seconds(), 10) << std::setw(22)
                  << fmt(relative.radius(), 15) << std::setw(22) << fmt(relative.speed(), 15)
                  << std::setw(22) << fmt(el.specific_energy, 15)
                  << fmt(el.specific_angular_momentum, 15) << "\n";

        if (csv.is_open()) {
            csv << t.seconds_since_j2000() << "," << state.state.position.x << ","
                << state.state.position.y << "," << state.state.position.z << ","
                << state.state.velocity.x << "," << state.state.velocity.y << ","
                << state.state.velocity.z << "," << relative.position.x << "," << relative.position.y
                << "," << relative.position.z << "," << relative.radius() << "," << relative.speed()
                << "," << el.semi_major_axis << "," << el.eccentricity << "," << el.specific_energy
                << "," << el.specific_angular_momentum << "\n";
        }
    }

    const auto body_state = ctx.provider->state(scenario.relative_to, t, ssb);
    const coordinates::StateVector relative{state.state.position - body_state.state.position,
                                            state.state.velocity - body_state.state.velocity};
    const auto el_final = trajectory::elements_from_state(relative, gm_center);

    totals.mean_step_seconds =
        totals.accepted_steps > 0
            ? (t - t0).seconds() / static_cast<double>(totals.accepted_steps)
            : 0.0;

    std::cout << "\nfinal osculating elements about " << scenario.relative_to.name() << ":\n"
              << el_final.to_string() << "\n\n"
              << "conservation over the run (two-body reference quantities):\n"
              << "  specific energy drift   : "
              << fmt(std::abs(el_final.specific_energy - specific_energy_initial) /
                         std::abs(specific_energy_initial), 6)
              << " relative\n"
              << "  angular momentum drift  : "
              << fmt(std::abs(el_final.specific_angular_momentum - angular_momentum_initial) /
                         angular_momentum_initial, 6)
              << " relative\n"
              << "  (non-zero drift here is physics -- third bodies -- plus integration error;\n"
              << "   see tests/scientific for the isolated two-body check)\n\n"
              << "integrator: " << totals.to_string() << "\n"
              << "proper time elapsed: " << fmt(state.proper_time.seconds(), 17) << " s"
              << "  (coordinate " << fmt((t - t0).seconds(), 17) << " s; equal while Newtonian)\n";

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
