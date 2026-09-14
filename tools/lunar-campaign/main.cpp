// The Earth-to-Moon campaign: many departure epochs, one row each, classified.
//
// This replaces tools/validation/lunar_mission_campaign.py, and the reason is
// not taste.  That script ran `orbit-cli intercept` and read its PROSE with
// regular expressions, so the campaign's answer depended on the wording of a
// print statement -- and it could only record what the CLI happened to print.
// Section 3 of the Milestone 6.1 brief asks for forty quantities per epoch and
// section 4 asks for every failure to carry a classification; neither survives a
// screen scrape.
//
// ---------------------------------------------------------------------------
// Milestone 6.2 section 7: what changed, and why it matters
//
// This tool used to call core/navigation/transfer_planner.hpp directly, while the
// game called a second planner of its own.  The campaign therefore measured a
// code path the player never executed, which is the one thing a validation
// campaign must not do.
//
// It now goes through navigation::plan_mission -- the same public entry
// point, with the same request type, that godot/gdextension/src/mission_planner
// calls when a pilot presses J.  The 365/365 is a statement about the shipped
// planner or it is a statement about nothing.
//
// `--map` is the deliberate exception: a departure x time-of-flight grid is a
// diagnostic about the SEARCH SPACE rather than a mission, it propagates
// nothing, and routing it through a mission request would mean inventing a
// mission to ask a question that has none.
// ---------------------------------------------------------------------------
//
//   lunar-campaign <scenario.json> --epochs 100 [options]
//
// Modes:
//   (default)             one transfer per epoch, one CSV row each
//   --map                 the departure x time-of-flight grid for ONE epoch
//   --oberth-sweep        one epoch, the capture burn walked around periapsis
//   --compare             the same epochs under IMPULSIVE and FINITE_BURN
//   --autopilot-sweep     one geometry, the controller bandwidth swept (s. 9)
//   --execution-campaign  planner + autopilot + finite burns, scored apart (s. 19)
//
// See docs/validation/lunar-navigation-hardening.md,
//     docs/validation/autopilot-hardening.md,
//     docs/validation/mission-execution-campaign.csv.

#include "core/celestial/body_catalog.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_kernel_set.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "core/navigation/mission_planner.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/conversions.hpp"
#include "tools/orbit-cli/scenario.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace sf;

namespace {

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::vector<std::string> flags;

    [[nodiscard]] bool has(const std::string& name) const {
        return std::find(flags.begin(), flags.end(), name) != flags.end();
    }
    [[nodiscard]] std::optional<std::string> option(const std::string& name) const {
        const auto it = options.find(name);
        return it == options.end() ? std::nullopt : std::optional<std::string>{it->second};
    }
    [[nodiscard]] std::string option_or(const std::string& name, const std::string& fallback) const {
        return option(name).value_or(fallback);
    }
    [[nodiscard]] double number(const std::string& name, double fallback) const {
        const auto value = option(name);
        return value.has_value() ? std::stod(*value) : fallback;
    }
    [[nodiscard]] int integer(const std::string& name, int fallback) const {
        const auto value = option(name);
        return value.has_value() ? std::stoi(*value) : fallback;
    }
};

Args parse(int argc, char** argv) {
    Args args{};
    for (int i = 1; i < argc; ++i) {
        std::string token{argv[i]};
        if (token.rfind("--", 0) != 0) {
            args.positional.push_back(token);
            continue;
        }
        const std::string name = token.substr(2);
        if (i + 1 < argc && std::string{argv[i + 1]}.rfind("--", 0) != 0) {
            args.options[name] = argv[++i];
        } else {
            args.flags.push_back(name);
        }
    }
    return args;
}

std::vector<double> comma_separated(const std::string& text) {
    std::vector<double> values;
    std::istringstream stream{text};
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            values.push_back(std::stod(token));
        }
    }
    return values;
}

struct Context {
    std::shared_ptr<const ephemeris::SpiceKernelSet> kernels;
    std::unique_ptr<ephemeris::SpiceEphemerisProvider> provider;
    std::unique_ptr<ephemeris::SpiceTimeConverter> time;
};

Context make_context(const Args& args) {
    const std::filesystem::path dir =
        args.option("kernels").has_value()
            ? std::filesystem::path{*args.option("kernels")}
            : ephemeris::SpiceKernelSet::default_directory();
    Context ctx{};
    ctx.kernels = std::make_shared<const ephemeris::SpiceKernelSet>(
        ephemeris::SpiceKernelSet::from_directory(dir));
    ctx.provider = std::make_unique<ephemeris::SpiceEphemerisProvider>(ctx.kernels);
    ctx.time = std::make_unique<ephemeris::SpiceTimeConverter>(ctx.kernels);
    return ctx;
}

// ---------------------------------------------------------------------------
// The request, from the command line.
//
// Every default here is the value the 365/365 campaign was qualified with, and
// most of them are simply the defaults of the request type -- which is the
// point: the game and the campaign share those defaults because they share the
// struct.
// ---------------------------------------------------------------------------
navigation::MissionRequest request_from(const Args& args,
                                              const orbitcli::Scenario& scenario) {
    navigation::MissionRequest request{};
    request.origin = scenario.relative_to;
    request.destination = celestial::bodies::moon;

    request.departure_window.span =
        time::Duration::hours(args.number("departure-window-hours", 2.0));
    request.departure_window.samples = args.integer("departure-samples", 16);

    if (const auto tofs = args.option("tof-days"); tofs.has_value()) {
        request.time_of_flight.days = comma_separated(*tofs);
    }

    request.target_orbit.periapsis_altitude = args.number("flyby-altitude-km", 100.0) * 1000.0;
    request.target_orbit.apoapsis_altitude =
        args.number("target-apoapsis-km", args.number("flyby-altitude-km", 100.0)) * 1000.0;
    request.target_orbit.minimum_periapsis_altitude =
        args.number("min-periapsis-km", 80.0) * 1000.0;
    request.target_orbit.maximum_apoapsis_altitude =
        args.number("max-apoapsis-km", 120.0) * 1000.0;
    request.target_orbit.maximum_eccentricity = args.number("max-ecc", 0.01);
    if (const auto inclination = args.option("target-inclination-deg"); inclination.has_value()) {
        // Section 13: accepted, reported against, never steered towards.
        request.target_orbit.inclination = units::Angle::degrees(std::stod(*inclination));
    }

    request.effort.periapsis_tolerance = args.number("periapsis-tolerance-km", 2.0) * 1000.0;
    request.effort.b_plane_angle = units::Angle::degrees(args.number("b-plane-angle", 0.0));
    request.effort.minimum_departure_perigee_altitude =
        args.number("min-departure-perigee-km", 120.0) * 1000.0;
    // The stricter reading of section 8's hard constraint: refuse a departure
    // conic below the floor instead of pricing it.  Off by default -- the screen
    // reads the UNCORRECTED conic and the corrector routinely lifts a perigee
    // that started below the surface, so refusing refuses flyable transfers.
    request.effort.refuse_departure_conic_below_floor = args.has("refuse-departure-conic");
    request.effort.screened_candidates = args.integer("screened", 24);
    request.effort.flown_candidates = args.integer("flown", 6);
    request.effort.b_plane_passes = args.integer("bplane-passes", 4);
    // --step-budget overrides BOTH, because a caller who names a budget means
    // the one their run is going to use, and silently applying it to the model
    // they are not flying would be a knob that does nothing.
    if (const auto budget = args.option("step-budget"); budget.has_value()) {
        const auto steps = static_cast<std::size_t>(std::stod(*budget));
        request.effort.step_budget = steps;
        request.effort.autopilot_step_budget = steps;
    }
    request.effort.capture_burn_offset_seconds = args.number("burn-offset-s", 0.0);
    request.effort.settling_threshold =
        units::Angle::degrees(args.number("settling-threshold-deg", 0.5));

    const std::string objective = args.option_or("objective", "balanced");
    if (objective == "min-total-dv") {
        request.objective = navigation::OptimizationObjective::MinimumTotalDeltaV;
    } else if (objective == "min-injection-dv") {
        request.objective = navigation::OptimizationObjective::MinimumInjectionDeltaV;
    } else if (objective == "shortest-tof") {
        request.objective = navigation::OptimizationObjective::ShortestTimeOfFlight;
    } else {
        request.objective = navigation::OptimizationObjective::Balanced;
    }

    const std::string execution = args.option_or("execution", "finite");
    if (execution == "impulsive") {
        request.spacecraft.execution = navigation::ExecutionModel::Impulsive;
    } else if (execution == "autopilot") {
        request.spacecraft.execution = navigation::ExecutionModel::Autopilot;
    } else {
        request.spacecraft.execution = navigation::ExecutionModel::FiniteBurn;
    }

    // The autopilot's controller bandwidth, exposed because it is the quantity
    // the whole IMPULSIVE/FINITE/AUTOPILOT comparison turns out to be about: a
    // critically damped PD tracking a target that rotates at omega settles at a
    // lag of 2 zeta omega / omega_n, and at periapsis of a 100 km lunar orbit
    // omega is 8.9e-4 rad/s.
    // Defaulted from the request type, not from a literal: the qualified
    // bandwidth lives in attitude::PointingGains and the campaign must not be
    // able to disagree with the ship the game flies.
    request.spacecraft.pointing.natural_frequency =
        args.number("autopilot-omega-n", request.spacecraft.pointing.natural_frequency);
    request.spacecraft.pointing.damping_ratio =
        args.number("autopilot-zeta", request.spacecraft.pointing.damping_ratio);
    request.spacecraft.vehicle = &*scenario.craft;
    return request;
}

navigation::SimulationState state_from(const Context& ctx, const orbitcli::Scenario& scenario,
                                       time::CoordinateTime epoch) {
    navigation::SimulationState state{};
    state.provider = ctx.provider.get();
    state.orientation = ctx.provider.get();
    state.catalog = celestial::BodyCatalog::resolve(*ctx.provider, scenario.bodies);
    state.j2_bodies = scenario.j2_bodies;
    state.vehicle.position = scenario.position;
    state.vehicle.velocity = scenario.velocity;
    state.epoch = epoch;
    state.integrator = scenario.integrator;
    return state;
}

// ---------------------------------------------------------------------------
// Section 21: three fuel scenarios, so that "the ship can always pay" stops
// being an accident of the scenario file.
//
// The lunar-intercept scenario carries 19 tonnes of propellant behind a torch
// drive, which is 26 942 m/s of budget against a mission that costs about
// 4 100.  Under that ship the rocket equation is never a constraint and the
// planner's INSUFFICIENT_PROPELLANT path has never once been executed.
//
// The scenarios scale the TANK and nothing else -- same engine, same hull, same
// trajectory -- so that the only thing that varies is whether the ship can pay.
// ---------------------------------------------------------------------------
enum class FuelScenario { Abundant, Limited, Marginal, Insufficient };

std::string_view to_string(FuelScenario scenario) {
    switch (scenario) {
        case FuelScenario::Abundant: return "ABUNDANT_FUEL";
        case FuelScenario::Limited:  return "LIMITED_FUEL";
        case FuelScenario::Marginal: return "MARGINAL_FUEL";
        case FuelScenario::Insufficient: return "INSUFFICIENT_FUEL";
    }
    return "?";
}

std::optional<FuelScenario> fuel_from_string(const std::string& name) {
    if (name == "abundant") return FuelScenario::Abundant;
    if (name == "limited")  return FuelScenario::Limited;
    if (name == "marginal") return FuelScenario::Marginal;
    if (name == "insufficient") return FuelScenario::Insufficient;
    return std::nullopt;
}

// The delta-v each scenario leaves the ship, as a multiple of what an
// Earth-Moon transfer from this parking orbit actually costs.
//
// 4 100 m/s is the MEASURED median over the 365-epoch campaign (4 086.7 exactly;
// departure plus capture), not a textbook figure.  What makes the scenarios
// interesting is the SPREAD around it, which is wide:
//
//     min 3 900.5    median 4 086.7    max 4 824.4  m/s
//
// so a budget near the median is affordable on a good date and not on a bad one.
// A scenario is therefore defined by its margin on the median:
//
//   ABUNDANT      the scenario ship, 26 942 000 m/s -- 6 600x, never binds
//   LIMITED       1.35x = 5 535 m/s -- comfortable; exists to show that a merely
//                 comfortable tank is still not a constraint
//   MARGINAL      1.00x = 4 100 m/s -- inside the measured band where the answer
//                 depends on the departure date
//   INSUFFICIENT  0.95x = 3 895 m/s -- below the cheapest reachable transfer, so
//                 every epoch must be refused, and refused BY NAME
//
// Calibrating MARGINAL took two wrong tries, and both are worth recording
// because they are the same mistake in different clothes.
//
// FIRST TRY, 1.02x = 4 182 m/s.  Reasoning: 103 of the 365 campaign epochs cost
// more than that, so 28 % should be refused.  Measured: nothing was refused.
// The 103 came from the FINITE campaign's `departure_delta_v`, which has the
// corrector's authority folded into it; the PLANNING leg is impulsive and its
// missions cost 3 907 to 4 132 m/s.  A budget compared against the wrong column
// is not a budget.
//
// SECOND TRY, spreading the epochs across the year instead of five consecutive
// days.  Reasoning: the expensive departures cluster.  Measured: still nothing
// refused -- and the reason is the interesting one.  The planner does not fail
// when the tank is tight, it RE-OPTIMISES: over twelve spread epochs the
// marginal tank changed the chosen mission once (2026-03-31, time of flight
// 5.00 -> 3.75 days, saving 6 m/s) and flew everything else unchanged.  The
// screen in TransferSession::screen refuses candidates the ship cannot pay for,
// so a tight budget narrows the search rather than defeating it.
//
// Which is the right behaviour, and it means a scenario only exercises the
// refusal path if its budget is below the CHEAPEST reachable transfer, not below
// the freely-chosen one.
//
// THIRD TRY, 0.95x = 3 895 m/s: refused 12 of 12, every one as
// INSUFFICIENT_DEPARTURE_DV.  The path works -- and a tank that refuses
// everything is not "marginal", it is insufficient, so it got that name.
//
// The affordability boundary is therefore bracketed by measurement: every
// sampled epoch lies between 3 895 m/s (all refused) and 4 182 m/s (all flown),
// a band of 7 %.  MARGINAL now sits inside it at 1.00x, where the answer is
// expected to depend on the departure date.  What the campaign measures is the
// split; it is deliberately not predicted here.
constexpr double kMeasuredMissionDeltaV = 4100.0;   // [m/s]

spacecraft::Spacecraft with_fuel(const spacecraft::Spacecraft& craft, FuelScenario scenario) {
    if (scenario == FuelScenario::Abundant) {
        return craft;
    }
    const double margin = scenario == FuelScenario::Limited      ? 1.35
                          : scenario == FuelScenario::Marginal     ? 1.00
                                                                   : 0.95;
    const double wanted = kMeasuredMissionDeltaV * margin;
    // Invert Tsiolkovsky for the propellant that buys exactly `wanted` from a
    // full tank: m0 = m_dry * exp(dv/w), propellant = m0 - m_dry.
    const double exhaust = craft.engine().effective_exhaust_velocity();
    const double propellant = craft.dry_mass() * (std::exp(wanted / exhaust) - 1.0);
    return spacecraft::Spacecraft{craft.name() + " (" + std::string{to_string(scenario)} + ")",
                                  craft.dry_mass(), propellant, craft.engine_modes()};
}

// ---------------------------------------------------------------------------
// Section 20: a parking orbit built from elements rather than copied.
//
// The scenario file's state is ONE orbit -- 400 km, in the Moon's orbital plane,
// with the departure point 170 degrees from where the Moon will be.  The phasing
// is most of why the campaign works, so the matrix keeps it: the orbit's node
// and the ship's argument of latitude are taken from the scenario and only the
// radius and the inclination are replaced.
// ---------------------------------------------------------------------------
// NOTE on what "400 km" means here, because it is not what the scenario file
// means by it.
//
// `radius` is measured from the destination-agnostic MEAN radius, 6 371 008 m,
// which is the radius every altitude in this codebase is quoted against
// (`post_burn_periapsis_altitude = r - mean_radius`).  The lunar-intercept
// scenario's own 400 km parking orbit has a = 6 778 000 m, i.e. 400 km above the
// EQUATORIAL radius of 6 378 km -- 407 km above the mean one.
//
// The two differ by 7.0 km, which is small and is not nothing: it is a different
// orbit, with a different period and a different phase at departure.  So the
// matrix axis is labelled by the convention it actually uses, and the campaign
// does not rebuild the orbit at all unless a matrix axis was asked for.
coordinates::StateVector parking_orbit(const orbitcli::Scenario& scenario, double gm,
                                       double radius, double inclination_deg) {
    const coordinates::StateVector reference{scenario.position, scenario.velocity};
    auto elements = trajectory::elements_from_state(reference, gm);
    elements.semi_major_axis = radius;
    elements.eccentricity = 0.0;
    elements.inclination = units::Angle::degrees(inclination_deg);
    // The phase is the ARGUMENT OF LATITUDE, u = argp + nu, and it is taken that
    // way rather than from `true_anomaly` alone.
    //
    // For an exactly circular orbit -- which the lunar-intercept scenario is, to
    // e = 9.1e-13 -- elements_from_state already reports argp = 0 and puts u in
    // `true_anomaly`, so the two readings agree.  They stop agreeing the moment
    // somebody points this at a parking orbit with real eccentricity in it, and
    // then dropping argp would rotate the departure point by tens of degrees --
    // which is most of the cost of a transfer.
    const double argument_of_latitude = elements.argument_of_periapsis.radians() +
                                        elements.true_anomaly.radians();
    elements.argument_of_periapsis = units::Angle::radians(0.0);
    elements.true_anomaly = units::Angle::radians(argument_of_latitude);
    return trajectory::state_from_elements(elements, gm);
}

// A percentile from a sorted copy.  Linear interpolation between the two
// neighbouring order statistics -- the same convention as numpy's default, so
// the numbers in the report can be reproduced by anyone.
double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::sort(values.begin(), values.end());
    if (values.size() == 1) {
        return values.front();
    }
    const double position = fraction * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = std::min(lower + 1, values.size() - 1);
    const double weight = position - static_cast<double>(lower);
    return values[lower] * (1.0 - weight) + values[upper] * weight;
}

void print_distribution(const std::string& label, std::vector<double> values,
                        const std::string& unit) {
    if (values.empty()) {
        std::cout << "  " << std::left << std::setw(30) << label << "  (no samples)\n";
        return;
    }
    std::cout << "  " << std::left << std::setw(30) << label << std::right << std::fixed
              << std::setprecision(6) << std::setw(14) << percentile(values, 0.0)
              << std::setw(14) << percentile(values, 0.5) << std::setw(14)
              << percentile(values, 0.95) << std::setw(14) << percentile(values, 1.0) << "  "
              << unit << "\n";
}

// ---------------------------------------------------------------------------

int run_campaign(const Args& args, const Context& ctx, orbitcli::Scenario scenario) {
    const int epochs = args.integer("epochs", 100);
    const double spacing_days = args.number("epoch-spacing-days", 1.0);
    const auto request = request_from(args, scenario);
    const auto t0 = ctx.time->parse(scenario.epoch_text);

    std::cout << "Earth-Moon campaign  (through navigation::plan_mission)\n"
              << "  scenario        : " << scenario.name << "\n"
              << "  first epoch     : " << ctx.time->to_utc_string(t0, 0) << "\n"
              << "  epochs          : " << epochs << ", one every " << spacing_days << " day(s)\n"
              << "  execution       : "
              << navigation::to_string(request.spacecraft.execution) << "\n"
              << "  objective       : " << navigation::to_string(request.objective) << "\n"
              << "  departure window: " << request.departure_window.span.hours() << " h, "
              << request.departure_window.samples << " samples\n"
              << "  time of flight  : ";
    for (std::size_t i = 0; i < request.time_of_flight.days.size(); ++i) {
        std::cout << (i > 0 ? ", " : "") << request.time_of_flight.days[i];
    }
    std::cout << " d\n"
              << "  target orbit    : " << request.target_orbit.periapsis_altitude / 1000.0
              << " x " << request.target_orbit.apoapsis_altitude / 1000.0
              << " km, periapsis >= "
              << request.target_orbit.minimum_periapsis_altitude / 1000.0 << " km, apoapsis <= "
              << request.target_orbit.maximum_apoapsis_altitude / 1000.0 << " km, e <= "
              << request.target_orbit.maximum_eccentricity << "\n"
              << "  ship            : " << scenario.craft->name() << ", budget "
              << scenario.craft->delta_v_budget(scenario.craft->initial_mass()) << " m/s\n\n";

    // ONE thread by default, and that is a measurement rather than caution.
    //
    // CSPICE is a Fortran translation with global state, and the provider is
    // obliged to serialise every call behind one mutex.  Every force evaluation
    // makes four of those calls, so threads here do not share work, they queue
    // for it -- and pay the cache-line traffic of queuing.  Measured on this
    // campaign: 7.5 s per epoch on two threads, 29 s per epoch on six.
    //
    // The way to use more than one core is more than one PROCESS, which is what
    // scripts/lunar_campaign.sh does: separate processes have separate SPICE.
    const unsigned int workers =
        std::max(1u, static_cast<unsigned int>(args.integer("jobs", 1)));

    std::vector<navigation::TransferRecord> records(static_cast<std::size_t>(epochs));
    std::atomic<int> next{0};
    std::mutex console;
    const auto started = std::chrono::steady_clock::now();

    auto worker = [&]() {
        for (;;) {
            const int index = next.fetch_add(1);
            if (index >= epochs) {
                return;
            }
            const auto epoch = t0 + time::Duration::days(spacing_days * index);
            const auto state = state_from(ctx, scenario, epoch);
            navigation::TransferRecord record{};
            try {
                record = navigation::plan_mission(state, request).diagnostics;
            } catch (const std::exception& e) {
                record.requested_epoch = epoch;
                record.failure = navigation::TransferFailure::NumericalFailure;
                record.detail = std::string{"exception: "} + e.what();
            }
            records[static_cast<std::size_t>(index)] = record;
            const std::lock_guard<std::mutex> lock{console};
            std::cout << "  [" << std::setw(4) << index + 1 << "/" << epochs << "] "
                      << ctx.time->to_utc_string(epoch, 0) << "  "
                      << (record.success ? "SUCCESS" : "FAILURE") << "  "
                      << navigation::to_string(record.failure) << "\n"
                      << std::flush;
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (unsigned int i = 0; i < workers; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const double wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    // ---- the CSV ---------------------------------------------------------
    if (const auto path = args.option("csv"); path.has_value()) {
        std::ofstream out{*path};
        if (!out) {
            std::cerr << "cannot write " << *path << "\n";
            return 2;
        }
        out << navigation::TransferRecord::csv_header() << "\n";
        for (const auto& record : records) {
            out << record.csv_row() << "\n";
        }
        std::cout << "\nwrote " << records.size() << " rows to " << *path << "\n";
    }

    // ---- the summary -----------------------------------------------------
    std::map<std::string, int> by_reason;
    int successes = 0;
    std::vector<double> periapsis_error_km;
    std::vector<double> insertion_dv;
    std::vector<double> departure_dv;
    std::vector<double> eccentricity;
    std::vector<double> periapsis_altitude_km;
    std::vector<double> apoapsis_altitude_km;
    std::vector<double> inclination_deg;
    std::vector<double> fuel_kg;
    std::vector<double> runtime_s;
    std::vector<double> tof_days;
    std::vector<double> transfer_angle_deg;

    for (const auto& record : records) {
        ++by_reason[std::string{navigation::to_string(record.failure)}];
        runtime_s.push_back(record.wall_time_seconds);
        if (!record.success) {
            continue;
        }
        ++successes;
        periapsis_error_km.push_back(std::abs(record.periapsis_error) / 1000.0);
        insertion_dv.push_back(record.required_capture_delta_v);
        departure_dv.push_back(record.departure_delta_v);
        eccentricity.push_back(record.post_burn_eccentricity);
        periapsis_altitude_km.push_back(record.post_burn_periapsis_altitude / 1000.0);
        apoapsis_altitude_km.push_back(record.post_burn_apoapsis_altitude / 1000.0);
        inclination_deg.push_back(units::rad_to_deg(record.post_burn_inclination_rad));
        fuel_kg.push_back(record.propellant_used);
        tof_days.push_back(record.time_of_flight_s / 86400.0);
        transfer_angle_deg.push_back(units::rad_to_deg(record.transfer_angle_rad));
    }

    std::cout << "\n================ campaign summary ================\n"
              << "epochs            : " << epochs << "\n"
              << "successes         : " << successes << "  ("
              << std::fixed << std::setprecision(1)
              << 100.0 * static_cast<double>(successes) / std::max(1, epochs) << " %)\n"
              << "wall time         : " << std::setprecision(2) << wall << " s on " << workers
              << " thread(s)\n\n"
              << "failure distribution\n";
    for (const auto& [reason, count] : by_reason) {
        if (reason == "NONE") {
            continue;
        }
        std::cout << "  " << std::left << std::setw(36) << reason << std::right << std::setw(5)
                  << count << "\n";
    }

    std::cout << "\ndistributions over the successful cases\n"
              << "  " << std::left << std::setw(30) << "" << std::right << std::setw(14)
              << "min" << std::setw(14) << "median" << std::setw(14) << "p95" << std::setw(14)
              << "max" << "\n";
    print_distribution("flyby periapsis error", periapsis_error_km, "km");
    print_distribution("departure delta-v", departure_dv, "m/s");
    print_distribution("insertion delta-v", insertion_dv, "m/s");
    print_distribution("final eccentricity", eccentricity, "");
    print_distribution("periapsis altitude", periapsis_altitude_km, "km");
    print_distribution("apoapsis altitude", apoapsis_altitude_km, "km");
    // Section 13: reported, not corrected.  The spread is real and it is not a
    // defect -- nothing in the specification asks for an inclination.
    print_distribution("final inclination", inclination_deg, "deg");
    print_distribution("propellant used", fuel_kg, "kg");
    print_distribution("time of flight", tof_days, "d");
    print_distribution("transfer angle", transfer_angle_deg, "deg");
    print_distribution("runtime per epoch", runtime_s, "s");

    return successes == epochs ? 0 : 1;
}

// ---------------------------------------------------------------------------

int run_map(const Args& args, const Context& ctx, orbitcli::Scenario scenario) {
    const auto request = request_from(args, scenario);
    const auto t0 = ctx.time->parse(scenario.epoch_text);
    const auto state = state_from(ctx, scenario, t0);
    // The grid is a question about the search space rather than about a mission,
    // so it is asked of the search directly -- through the same translation the
    // public entry point uses, so the cells describe the grid a real request
    // would walk.
    const auto grid = navigation::map_transfer_grid(navigation::inputs_for(state, request),
                                                    navigation::config_for(request));

    std::cout << "departure x time-of-flight map for "
              << ctx.time->to_utc_string(t0, 0) << "\n"
              << grid.size() << " cells\n\n";

    if (const auto path = args.option("csv"); path.has_value()) {
        std::ofstream out{*path};
        out << "departure_coast_s,time_of_flight_days,branch,classification,transfer_angle_deg,"
               "lambert_delta_v_ms,departure_perigee_altitude_m,v_infinity_estimate_ms\n";
        out << std::setprecision(17);
        for (const auto& cell : grid) {
            out << cell.departure_coast_s << "," << cell.time_of_flight_days << ","
                << (cell.direction == trajectory::TransferDirection::Prograde ? "prograde"
                                                                             : "retrograde")
                << "," << navigation::to_string(cell.classification) << ","
                << cell.transfer_angle_deg << "," << cell.lambert_delta_v << ","
                << cell.departure_perigee_altitude << "," << cell.v_infinity_estimate << "\n";
        }
        std::cout << "wrote " << grid.size() << " cells to " << *path << "\n\n";
    }

    std::map<std::string, int> counts;
    for (const auto& cell : grid) {
        ++counts[std::string{navigation::to_string(cell.classification)}];
    }
    for (const auto& [name, count] : counts) {
        std::cout << "  " << std::left << std::setw(28) << name << std::right << std::setw(6)
                  << count << "\n";
    }

    // The map, as a picture.  Rows are time of flight, columns are departure
    // coast; one character per cell, per branch.
    for (const auto direction : {trajectory::TransferDirection::Prograde,
                                 trajectory::TransferDirection::Retrograde}) {
        std::cout << "\n"
                  << (direction == trajectory::TransferDirection::Prograde ? "prograde"
                                                                          : "retrograde")
                  << " branch   ( . no solution   x degenerate   E departure conic hits Earth"
                     "   $ unaffordable   # feasible )\n";
        for (const double tof : request.time_of_flight.days) {
            std::cout << "  tof " << std::fixed << std::setprecision(2) << std::setw(6) << tof
                      << " d  ";
            for (const auto& cell : grid) {
                if (cell.direction != direction ||
                    std::abs(cell.time_of_flight_days - tof) > 1.0e-9) {
                    continue;
                }
                switch (cell.classification) {
                    case navigation::GridClass::NoSolution:             std::cout << '.'; break;
                    case navigation::GridClass::DegenerateGeometry:     std::cout << 'x'; break;
                    case navigation::GridClass::DepartureConicHitsBody: std::cout << 'E'; break;
                    case navigation::GridClass::TooExpensive:           std::cout << '$'; break;
                    case navigation::GridClass::Feasible:               std::cout << '#'; break;
                }
            }
            std::cout << "\n";
        }
    }
    std::cout << "\n";
    return 0;
}

// ---------------------------------------------------------------------------

int run_oberth_sweep(const Args& args, const Context& ctx, orbitcli::Scenario scenario) {
    auto request = request_from(args, scenario);
    const auto t0 = ctx.time->parse(scenario.epoch_text);
    const auto state = state_from(ctx, scenario, t0);

    std::vector<double> offsets{-120.0, -60.0, -30.0, 0.0, 30.0, 60.0, 120.0};
    if (const auto given = args.option("offsets"); given.has_value()) {
        offsets = comma_separated(*given);
    }

    std::cout << "Oberth sensitivity at " << ctx.time->to_utc_string(t0, 0) << ", execution "
              << navigation::to_string(request.spacecraft.execution) << "\n\n"
              << "  offset     r at midpoint     v at midpoint    burn      e after"
                 "     periapsis    apoapsis    result\n"
              << "     [s]              [km]           [m/s]     [s]"
                 "                     [km]        [km]\n";

    std::ofstream csv;
    if (const auto path = args.option("csv"); path.has_value()) {
        csv.open(*path);
        csv << navigation::TransferRecord::csv_header() << "\n";
    }

    for (const double offset : offsets) {
        request.effort.capture_burn_offset_seconds = offset;
        const auto record = navigation::plan_mission(state, request).diagnostics;
        if (csv.is_open()) {
            csv << record.csv_row() << "\n";
        }
        std::cout << std::fixed << std::setprecision(1) << std::setw(8) << offset
                  << std::setprecision(3) << std::setw(18)
                  << record.burn_midpoint_radius / 1000.0 << std::setw(16)
                  << record.burn_midpoint_speed << std::setw(10) << record.burn_duration_s
                  << std::setprecision(6) << std::setw(12) << record.post_burn_eccentricity
                  << std::setprecision(3) << std::setw(13)
                  << record.post_burn_periapsis_altitude / 1000.0 << std::setw(12)
                  << record.post_burn_apoapsis_altitude / 1000.0 << "    "
                  << (record.success ? "SUCCESS" : navigation::to_string(record.failure)) << "\n";
    }
    std::cout << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Pin the geometry a FINITE_BURN search found, so that every model that follows
// flies the SAME transfer.
//
// Section 11 of the Milestone 6.1 brief and section 19 of 6.2 both depend on
// this: let each model run its own search and they differ in departure point,
// flight time and arrival v_infinity, and the comparison stops being one.
// ---------------------------------------------------------------------------
std::optional<navigation::TransferConfig::PinnedDeparture> pin_for(
    const navigation::SimulationState& state, navigation::MissionRequest request) {
    request.pinned = {};
    request.spacecraft.execution = navigation::ExecutionModel::FiniteBurn;
    const auto search = navigation::plan_mission(state, request);
    if (!search.ok()) {
        return std::nullopt;
    }
    navigation::TransferConfig::PinnedDeparture pinned{};
    pinned.active = true;
    pinned.coast_s = search.diagnostics.departure_coast_s;
    pinned.time_of_flight_days = search.diagnostics.time_of_flight_s / 86400.0;
    pinned.direction = search.diagnostics.lambert_direction;
    return pinned;
}

int run_compare(const Args& args, const Context& ctx, orbitcli::Scenario scenario) {
    const int epochs = args.integer("epochs", 10);
    const double spacing_days = args.number("epoch-spacing-days", 1.0);
    auto request = request_from(args, scenario);
    const auto t0 = ctx.time->parse(scenario.epoch_text);

    std::cout << "IMPULSIVE vs FINITE_BURN vs AUTOPILOT over " << epochs << " epoch(s)\n"
              << "all three fly the geometry the finite search chose, so what is left between\n"
              << "them is the burn and nothing else.\n\n"
              << "  epoch                  model         e          r_p      r_a     burn"
                 "   dv     lag mean/peak   result\n"
              << "                                                 [km]     [km]      [s]"
                 "  [m/s]        [deg]\n";

    int planner_faults = 0;
    int execution_faults = 0;
    int all_three = 0;
    std::vector<double> impulsive_ecc;
    std::vector<double> finite_ecc;
    std::vector<double> autopilot_ecc;
    std::vector<double> lag_mean;

    std::ofstream csv;
    if (const auto path = args.option("csv"); path.has_value()) {
        csv.open(*path);
        csv << navigation::TransferRecord::csv_header() << "\n";
    }

    for (int i = 0; i < epochs; ++i) {
        const auto epoch = t0 + time::Duration::days(spacing_days * i);
        const auto state = state_from(ctx, scenario, epoch);

        const auto pinned = pin_for(state, request);
        if (!pinned.has_value()) {
            std::cout << "  " << ctx.time->to_utc_string(epoch, 0)
                      << "  search found nothing to pin\n";
            ++planner_faults;
            continue;
        }
        request.pinned = *pinned;

        navigation::TransferRecord results[3];
        const navigation::ExecutionModel models[3] = {navigation::ExecutionModel::Impulsive,
                                                      navigation::ExecutionModel::FiniteBurn,
                                                      navigation::ExecutionModel::Autopilot};
        for (int m = 0; m < 3; ++m) {
            request.spacecraft.execution = models[m];
            results[m] = navigation::plan_mission(state, request).diagnostics;
            if (csv.is_open()) {
                csv << results[m].csv_row() << "\n";
            }
            std::cout << "  " << (m == 0 ? ctx.time->to_utc_string(epoch, 0)
                                         : std::string(19, ' '))
                      << "  " << std::left << std::setw(12)
                      << navigation::to_string(models[m]) << std::right << std::fixed
                      << std::setprecision(6) << std::setw(10)
                      << results[m].post_burn_eccentricity << std::setprecision(2)
                      << std::setw(9) << results[m].post_burn_periapsis_altitude / 1000.0
                      << std::setw(9) << results[m].post_burn_apoapsis_altitude / 1000.0
                      << std::setw(9) << results[m].burn_duration_s << std::setw(8)
                      << results[m].required_capture_delta_v << std::setprecision(3)
                      << std::setw(8)
                      << units::rad_to_deg(results[m].capture_pointing_error_mean_rad)
                      << std::setw(8)
                      << units::rad_to_deg(results[m].capture_pointing_error_peak_rad) << "   "
                      << (results[m].success ? "SUCCESS"
                                             : std::string{navigation::to_string(
                                                   results[m].failure)})
                      << "\n";
        }
        std::cout << "\n";

        if (results[0].success) {
            impulsive_ecc.push_back(results[0].post_burn_eccentricity);
        }
        if (results[1].success) {
            finite_ecc.push_back(results[1].post_burn_eccentricity);
        }
        if (results[2].success) {
            autopilot_ecc.push_back(results[2].post_burn_eccentricity);
        }
        if (results[2].capture_pointing_error_mean_rad > 0.0) {
            lag_mean.push_back(units::rad_to_deg(results[2].capture_pointing_error_mean_rad));
        }
        all_three += (results[0].success && results[1].success && results[2].success) ? 1 : 0;
        // The whole point (section 11): a case that fails IMPULSIVELY is a
        // planner fault, because there is no control error in that model at all.
        // A case that works impulsively and fails with the engine running is an
        // execution fault, and the model that first fails names which part.
        if (!results[0].success) {
            ++planner_faults;
        } else if (!results[1].success || !results[2].success) {
            ++execution_faults;
        }
    }

    std::cout << "  epochs                  : " << epochs
              << "\n  all three succeeded     : " << all_three
              << "\n  planner faults          : " << planner_faults
              << "   (fails with ideal impulses: the trajectory is wrong)"
              << "\n  execution faults        : " << execution_faults
              << "   (works with ideal impulses, fails with the engine running)\n";
    print_distribution("impulsive eccentricity", impulsive_ecc, "");
    print_distribution("finite-burn eccentricity", finite_ecc, "");
    print_distribution("autopilot eccentricity", autopilot_ecc, "");
    print_distribution("autopilot pointing lag", lag_mean, "deg");
    std::cout << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Section 9: the controller bandwidth, swept.
//
// One geometry per epoch, pinned by a FINITE_BURN search, then flown by the
// autopilot at each omega_n.  Pinning is what makes the sweep a sweep: without
// it every gain would also get its own trajectory and the eccentricity column
// would be measuring the search, not the controller.
//
// Section 10 is why the table is this wide.  A gain chosen on eccentricity alone
// buys it with propellant, with an actuator hard against its stop, and with a
// hull that is being thrown about; those columns are here so that the choice can
// be argued about rather than asserted.
// ---------------------------------------------------------------------------
int run_autopilot_sweep(const Args& args, const Context& ctx, orbitcli::Scenario scenario) {
    const int epochs = args.integer("epochs", 5);
    const double spacing_days = args.number("epoch-spacing-days", 7.0);
    auto request = request_from(args, scenario);
    const auto t0 = ctx.time->parse(scenario.epoch_text);

    std::vector<double> gains{0.05, 0.075, 0.10, 0.15, 0.20, 0.30};
    if (const auto given = args.option("omega-n"); given.has_value()) {
        gains = comma_separated(*given);
    }

    std::cout << "autopilot bandwidth sweep over " << epochs << " epoch(s), "
              << gains.size() << " gain(s)\n"
              << "every gain flies the geometry the FINITE_BURN search pinned for that epoch.\n\n";

    std::ofstream csv;
    if (const auto path = args.option("csv"); path.has_value()) {
        csv.open(*path);
        csv << "omega_n,zeta,epoch_tdb_s,result,failure,"
               "pointing_mean_deg,pointing_peak_deg,settling_s,"
               "rcs_propellant_kg,rcs_duty_cycle,torque_saturation,angular_rate_peak_rad_s,"
               "eccentricity,periapsis_km,apoapsis_km,capture_dv_ms,burn_duration_s\n";
        csv << std::setprecision(17);
    }

    struct Row {
        double omega_n{0.0};
        int flown{0};
        int captured{0};
        std::vector<double> mean_deg;
        std::vector<double> peak_deg;
        std::vector<double> settling_s;
        std::vector<double> rcs_kg;
        std::vector<double> duty;
        std::vector<double> saturation;
        std::vector<double> rate_peak;
        std::vector<double> eccentricity;
        std::vector<double> periapsis_km;
        std::vector<double> apoapsis_km;
    };
    std::vector<Row> rows;
    for (const double gain : gains) {
        Row row{};
        row.omega_n = gain;
        rows.push_back(std::move(row));
    }

    for (int i = 0; i < epochs; ++i) {
        const auto epoch = t0 + time::Duration::days(spacing_days * i);
        const auto state = state_from(ctx, scenario, epoch);

        request.spacecraft.pointing.natural_frequency = gains.front();
        const auto pinned = pin_for(state, request);
        if (!pinned.has_value()) {
            std::cout << "  " << ctx.time->to_utc_string(epoch, 0)
                      << "  no geometry to pin; skipped\n";
            continue;
        }
        request.pinned = *pinned;
        request.spacecraft.execution = navigation::ExecutionModel::Autopilot;

        std::cout << "  " << ctx.time->to_utc_string(epoch, 0) << "   pinned "
                  << pinned->time_of_flight_days << " d, coast "
                  << pinned->coast_s / 3600.0 << " h\n"
                  << "    omega_n    mean     peak   settling     RCS    duty    sat"
                     "    rate        e     r_p     r_a   result\n"
                  << "    [rad/s]   [deg]    [deg]        [s]     [g]                "
                     " [mrad/s]           [km]    [km]\n";

        for (std::size_t g = 0; g < gains.size(); ++g) {
            request.spacecraft.pointing.natural_frequency = gains[g];
            const auto record = navigation::plan_mission(state, request).diagnostics;
            auto& row = rows[g];
            ++row.flown;
            if (record.success) {
                ++row.captured;
            }
            // Recorded whether or not the capture met the specification: a gain
            // that points beautifully and misses the orbit is a fact about the
            // gain, and dropping its row would make the sweep look better than
            // the controller is.
            row.mean_deg.push_back(units::rad_to_deg(record.capture_pointing_error_mean_rad));
            row.peak_deg.push_back(units::rad_to_deg(record.capture_pointing_error_peak_rad));
            row.settling_s.push_back(record.capture_settling_s);
            row.rcs_kg.push_back(record.capture_rcs_propellant);
            row.duty.push_back(record.capture_rcs_duty_cycle);
            row.saturation.push_back(record.capture_torque_saturation);
            row.rate_peak.push_back(record.capture_angular_rate_peak);
            if (record.success) {
                row.eccentricity.push_back(record.post_burn_eccentricity);
                row.periapsis_km.push_back(record.post_burn_periapsis_altitude / 1000.0);
                row.apoapsis_km.push_back(record.post_burn_apoapsis_altitude / 1000.0);
            }

            if (csv.is_open()) {
                csv << gains[g] << "," << request.spacecraft.pointing.damping_ratio << ","
                    << epoch.seconds_since_j2000() << ","
                    << (record.success ? "SUCCESS" : "FAILURE") << ","
                    << navigation::to_string(record.failure) << ","
                    << units::rad_to_deg(record.capture_pointing_error_mean_rad) << ","
                    << units::rad_to_deg(record.capture_pointing_error_peak_rad) << ","
                    << record.capture_settling_s << "," << record.capture_rcs_propellant << ","
                    << record.capture_rcs_duty_cycle << "," << record.capture_torque_saturation
                    << "," << record.capture_angular_rate_peak << ","
                    << record.post_burn_eccentricity << ","
                    << record.post_burn_periapsis_altitude / 1000.0 << ","
                    << record.post_burn_apoapsis_altitude / 1000.0 << ","
                    << record.required_capture_delta_v << "," << record.burn_duration_s << "\n";
            }

            std::cout << std::fixed << std::setw(11) << std::setprecision(3) << gains[g]
                      << std::setw(8) << units::rad_to_deg(record.capture_pointing_error_mean_rad)
                      << std::setw(9) << units::rad_to_deg(record.capture_pointing_error_peak_rad)
                      << std::setw(11) << std::setprecision(1) << record.capture_settling_s
                      << std::setw(8) << std::setprecision(3)
                      << record.capture_rcs_propellant * 1000.0
                      << std::setw(8) << std::setprecision(3) << record.capture_rcs_duty_cycle
                      << std::setw(7) << record.capture_torque_saturation
                      << std::setw(9) << std::setprecision(3)
                      << record.capture_angular_rate_peak * 1000.0
                      << std::setw(9) << std::setprecision(6) << record.post_burn_eccentricity
                      << std::setw(8) << std::setprecision(2)
                      << record.post_burn_periapsis_altitude / 1000.0 << std::setw(8)
                      << record.post_burn_apoapsis_altitude / 1000.0 << "   "
                      << (record.success ? "SUCCESS"
                                         : std::string{navigation::to_string(record.failure)})
                      << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "================ sweep summary (median over epochs) ================\n"
              << "  omega_n  captured     mean     peak  settling      RCS    duty     sat"
                 "      rate         e\n"
              << "  [rad/s]              [deg]    [deg]       [s]      [g]"
                 "                   [mrad/s]\n";
    for (const auto& row : rows) {
        std::cout << std::fixed << std::setw(9) << std::setprecision(3) << row.omega_n
                  << std::setw(7) << row.captured << "/" << row.flown
                  << std::setw(9) << std::setprecision(4) << percentile(row.mean_deg, 0.5)
                  << std::setw(9) << percentile(row.peak_deg, 0.5)
                  << std::setw(10) << std::setprecision(1) << percentile(row.settling_s, 0.5)
                  << std::setw(9) << std::setprecision(3)
                  << percentile(row.rcs_kg, 0.5) * 1000.0
                  << std::setw(8) << std::setprecision(3) << percentile(row.duty, 0.5)
                  << std::setw(8) << percentile(row.saturation, 0.5)
                  << std::setw(11) << std::setprecision(3)
                  << percentile(row.rate_peak, 0.5) * 1000.0
                  << std::setw(10) << std::setprecision(6)
                  << percentile(row.eccentricity, 0.5) << "\n";
    }
    std::cout << "\nthe worst case over the epochs, which is what a specification is about\n"
              << "  omega_n     peak lag   peak sat   worst e\n";
    for (const auto& row : rows) {
        std::cout << std::fixed << std::setw(9) << std::setprecision(3) << row.omega_n
                  << std::setw(13) << std::setprecision(4) << percentile(row.peak_deg, 1.0)
                  << std::setw(11) << std::setprecision(3) << percentile(row.saturation, 1.0)
                  << std::setw(11) << std::setprecision(6)
                  << percentile(row.eccentricity, 1.0) << "\n";
    }
    std::cout << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Section 19: a campaign with the engine and the controller in the loop.
//
// Three questions, scored separately, because collapsing them is what made
// Milestone 6 unable to say anything:
//
//   planning success     does a trajectory exist and does the search find it
//   execution success    does the flight of it reach the target and capture
//   final orbit success  is the orbit it captured into the one that was asked for
//
// Sections 20 and 21 are the two axes this is run over: the parking orbit
// (altitude x inclination) and the tank (abundant, limited, marginal).
// ---------------------------------------------------------------------------
int run_execution_campaign(const Args& args, const Context& ctx, orbitcli::Scenario scenario) {
    const int epochs = args.integer("epochs", 30);
    const double spacing_days = args.number("epoch-spacing-days", 1.0);
    const auto t0 = ctx.time->parse(scenario.epoch_text);

    // Empty means "the scenario's own orbit, untouched".
    //
    // This used to default to {400.0} x {the scenario's inclination} and rebuild
    // the state from elements every time, which looked like a no-op and was not:
    // 400 km above the MEAN radius is 6 771 008 m against the scenario's
    // 6 778 000 m, so every run of the default configuration silently flew an
    // orbit 7 km below the one it named.  A campaign that does not fly the
    // scenario it was handed is not measuring the scenario.
    std::vector<double> altitudes_km{};
    if (const auto given = args.option("parking-altitudes-km"); given.has_value()) {
        altitudes_km = comma_separated(*given);
    }
    std::vector<double> inclinations_deg{};
    if (const auto given = args.option("parking-inclinations-deg"); given.has_value()) {
        inclinations_deg = comma_separated(*given);
    }
    const bool rebuild_parking = !altitudes_km.empty() || !inclinations_deg.empty();

    std::vector<FuelScenario> fuels{FuelScenario::Abundant};
    if (const auto given = args.option("fuel"); given.has_value()) {
        fuels.clear();
        std::istringstream stream{*given};
        std::string token;
        while (std::getline(stream, token, ',')) {
            const auto parsed = fuel_from_string(token);
            if (!parsed.has_value()) {
                std::cerr << "unknown fuel scenario \"" << token
                          << "\" (abundant | limited | marginal | insufficient)\n";
                return 2;
            }
            fuels.push_back(*parsed);
        }
    }

    const double gm_earth = ctx.provider->gravitational_parameter(scenario.relative_to);
    const double radius_earth = ctx.provider->mean_radius(scenario.relative_to);
    const coordinates::StateVector scenario_state{scenario.position, scenario.velocity};
    const double scenario_inclination =
        trajectory::elements_from_state(scenario_state, gm_earth).inclination.degrees();
    const double scenario_altitude =
        trajectory::elements_from_state(scenario_state, gm_earth).semi_major_axis - radius_earth;
    if (inclinations_deg.empty()) {
        inclinations_deg.push_back(scenario_inclination);
    }
    if (altitudes_km.empty()) {
        altitudes_km.push_back(scenario_altitude / 1000.0);
    }

    std::ofstream csv;
    if (const auto path = args.option("csv"); path.has_value()) {
        csv.open(*path);
        if (!csv) {
            std::cerr << "cannot write " << *path << "\n";
            return 2;
        }
        csv << "parking_altitude_km,parking_inclination_deg,fuel_scenario,delta_v_budget_ms,"
               "epoch_tdb_s,epoch_utc,"
               "planning,planning_failure,execution,execution_failure,"
               "final_orbit,final_orbit_failure,verdict,"
               "injection_dv_ms,capture_dv_ms,total_dv_ms,propellant_used_kg,"
               "propellant_left_kg,time_of_flight_d,"
               "predicted_periapsis_km,predicted_apoapsis_km,predicted_ecc,"
               "predicted_inclination_deg,predicted_raan_deg,"
               "actual_periapsis_km,actual_apoapsis_km,actual_ecc,actual_inclination_deg,"
               "actual_raan_deg,actual_propellant_used_kg,actual_arrival_tdb_s,"
               "actual_capture_dv_ms,"
               "pointing_mean_deg,pointing_peak_deg,settling_s,rcs_propellant_kg,"
               "rcs_duty_cycle,torque_saturation\n";
        csv << std::setprecision(17);
    }

    struct Tally {
        int cases{0};
        int planning{0};
        int execution{0};
        int final_orbit{0};
        std::map<std::string, int> planner_failures;
        std::map<std::string, int> execution_failures;
    };
    std::map<std::string, Tally> tallies;
    Tally overall{};

    for (const double altitude_km : altitudes_km) {
        for (const double inclination_deg : inclinations_deg) {
            for (const FuelScenario fuel : fuels) {
                orbitcli::Scenario variant = scenario;
                if (rebuild_parking) {
                    const auto parking = parking_orbit(scenario, gm_earth,
                                                       radius_earth + altitude_km * 1000.0,
                                                       inclination_deg);
                    variant.position = parking.position;
                    variant.velocity = parking.velocity;
                }
                variant.craft = with_fuel(*scenario.craft, fuel);

                auto request = request_from(args, variant);
                const double budget =
                    variant.craft->delta_v_budget(variant.craft->initial_mass());

                std::ostringstream label;
                label << std::fixed << std::setprecision(0) << altitude_km << " km / "
                      << std::setprecision(1) << inclination_deg << " deg / "
                      << to_string(fuel) << (rebuild_parking ? "" : " (scenario orbit)");
                std::cout << "\n=== " << label.str() << "   budget " << std::fixed
                          << std::setprecision(0) << budget << " m/s ===\n"
                          << "  epoch                planning            execution"
                             "           orbit      verdict\n";

                auto& tally = tallies[label.str()];

                for (int i = 0; i < epochs; ++i) {
                    const auto epoch = t0 + time::Duration::days(spacing_days * i);
                    const auto state = state_from(ctx, variant, epoch);

                    // ---- planning: ideal impulses, no control error at all ---
                    request.pinned = {};
                    request.spacecraft.execution = navigation::ExecutionModel::Impulsive;
                    const auto planned = navigation::plan_mission(state, request);

                    navigation::MissionPlanResult flown{};
                    if (planned.ok()) {
                        // Same geometry, engine running, controller in the loop.
                        request.pinned.active = true;
                        request.pinned.coast_s = planned.diagnostics.departure_coast_s;
                        request.pinned.time_of_flight_days =
                            planned.diagnostics.time_of_flight_s / 86400.0;
                        request.pinned.direction = planned.diagnostics.lambert_direction;
                        request.spacecraft.execution = navigation::ExecutionModel::Autopilot;
                        flown = navigation::plan_mission(state, request);
                    }

                    // The three questions.  "Execution" asks whether the flight
                    // reached the target and closed an orbit about it; "final
                    // orbit" asks whether that orbit is the one requested.  A
                    // capture into 40 x 900 km passes the second and fails the
                    // third, and saying so is the whole point of splitting them.
                    //
                    // "Closed an orbit" is read off the SPECIFIC ENERGY and not
                    // off the failure label, because two different mechanisms
                    // share the label PERIAPSIS_TOO_LOW -- a flyby that came in
                    // too low, which never captured at all, and a capture that
                    // succeeded into an orbit whose periapsis is below the band.
                    // Energy tells them apart; the enum cannot.
                    const bool planning_ok = planned.ok();
                    const bool captured = flown.diagnostics.post_burn_specific_energy < 0.0;
                    const bool execution_ok = planning_ok && captured;
                    const bool orbit_ok = flown.ok();

                    ++tally.cases;
                    ++overall.cases;
                    tally.planning += planning_ok ? 1 : 0;
                    overall.planning += planning_ok ? 1 : 0;
                    tally.execution += execution_ok ? 1 : 0;
                    overall.execution += execution_ok ? 1 : 0;
                    tally.final_orbit += orbit_ok ? 1 : 0;
                    overall.final_orbit += orbit_ok ? 1 : 0;

                    std::string verdict = "COMPLETE";
                    if (!planning_ok) {
                        verdict = planned.status ==
                                          navigation::MissionPlanStatus::InsufficientPropellant
                                      ? "PROPULSION FAILURE"
                                      : "PLANNER FAILURE";
                        ++tally.planner_failures[std::string{
                            navigation::to_string(planned.diagnostics.failure)}];
                    } else if (!execution_ok) {
                        verdict = flown.status ==
                                          navigation::MissionPlanStatus::InsufficientPropellant
                                      ? "PROPULSION FAILURE"
                                      : "AUTOPILOT FAILURE";
                        ++tally.execution_failures[std::string{
                            navigation::to_string(flown.diagnostics.failure)}];
                    } else if (!orbit_ok) {
                        verdict = "ORBIT OUT OF SPEC";
                        ++tally.execution_failures[std::string{
                            navigation::to_string(flown.diagnostics.failure)}];
                    }

                    const auto& p = planned.diagnostics;
                    const auto& a = flown.diagnostics;
                    // setw(20), not setw(16): the longest classification is
                    // DEPARTURE_CONIC_HITS_CENTRAL_BODY, and a column narrower
                    // than its contents runs two fields together -- which is how
                    // a table stops being readable exactly when it has something
                    // to say.
                    std::cout << "  " << ctx.time->to_utc_string(epoch, 0) << "  "
                              << std::left << std::setw(20)
                              << (planning_ok ? std::string{"ok"}
                                              : std::string{navigation::to_string(p.failure)})
                              << std::setw(20)
                              << (execution_ok ? std::string{"ok"}
                                               : std::string{navigation::to_string(a.failure)})
                              << std::setw(11) << (orbit_ok ? "ok" : "-") << verdict
                              << std::right << "\n";

                    if (csv.is_open()) {
                        csv << altitude_km << "," << inclination_deg << "," << to_string(fuel)
                            << "," << budget << "," << epoch.seconds_since_j2000() << ",\""
                            << ctx.time->to_utc_string(epoch, 0) << "\","
                            << (planning_ok ? "PASS" : "FAIL") << ","
                            << navigation::to_string(p.failure) << ","
                            << (execution_ok ? "PASS" : "FAIL") << ","
                            << navigation::to_string(a.failure) << ","
                            << (orbit_ok ? "PASS" : "FAIL") << ","
                            << navigation::to_string(a.failure) << ",\"" << verdict << "\","
                            << p.departure_delta_v << "," << p.required_capture_delta_v << ","
                            << (p.departure_delta_v + p.required_capture_delta_v) << ","
                            << p.propellant_used << "," << p.propellant_left << ","
                            << p.time_of_flight_s / 86400.0 << ","
                            << p.post_burn_periapsis_altitude / 1000.0 << ","
                            << p.post_burn_apoapsis_altitude / 1000.0 << ","
                            << p.post_burn_eccentricity << ","
                            << units::rad_to_deg(p.post_burn_inclination_rad) << ","
                            << units::rad_to_deg(p.post_burn_raan_rad) << ","
                            << a.post_burn_periapsis_altitude / 1000.0 << ","
                            << a.post_burn_apoapsis_altitude / 1000.0 << ","
                            << a.post_burn_eccentricity << ","
                            << units::rad_to_deg(a.post_burn_inclination_rad) << ","
                            << units::rad_to_deg(a.post_burn_raan_rad) << ","
                            << a.propellant_used << ","
                            << a.closest_approach_epoch.seconds_since_j2000() << ","
                            << a.required_capture_delta_v << ","
                            << units::rad_to_deg(a.capture_pointing_error_mean_rad) << ","
                            << units::rad_to_deg(a.capture_pointing_error_peak_rad) << ","
                            << a.capture_settling_s << "," << a.capture_rcs_propellant << ","
                            << a.capture_rcs_duty_cycle << "," << a.capture_torque_saturation
                            << "\n";
                    }
                }
            }
        }
    }

    std::cout << "\n================ execution campaign ================\n"
              << "  " << std::left << std::setw(34) << "configuration" << std::right
              << std::setw(10) << "cases" << std::setw(12) << "planning" << std::setw(12)
              << "execution" << std::setw(12) << "orbit" << "\n";
    for (const auto& [label, tally] : tallies) {
        std::cout << "  " << std::left << std::setw(34) << label << std::right << std::setw(10)
                  << tally.cases << std::setw(12) << tally.planning << std::setw(12)
                  << tally.execution << std::setw(12) << tally.final_orbit << "\n";
        for (const auto& [reason, count] : tally.planner_failures) {
            std::cout << "      planner   " << std::left << std::setw(34) << reason
                      << std::right << count << "\n";
        }
        for (const auto& [reason, count] : tally.execution_failures) {
            std::cout << "      execution " << std::left << std::setw(34) << reason
                      << std::right << count << "\n";
        }
    }
    std::cout << "  " << std::left << std::setw(34) << "TOTAL" << std::right << std::setw(10)
              << overall.cases << std::setw(12) << overall.planning << std::setw(12)
              << overall.execution << std::setw(12) << overall.final_orbit << "\n\n";

    return overall.final_orbit == overall.cases ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse(argc, argv);
        if (args.positional.empty() || args.has("help")) {
            std::cout
                << "usage: lunar-campaign <scenario.json> [--epochs N] [--csv <file>]\n"
                   "       lunar-campaign <scenario.json> --map [--csv <file>]\n"
                   "       lunar-campaign <scenario.json> --oberth-sweep [--offsets a,b,c]\n"
                   "       lunar-campaign <scenario.json> --compare [--epochs N]\n"
                   "       lunar-campaign <scenario.json> --autopilot-sweep [--omega-n a,b,c]\n"
                   "       lunar-campaign <scenario.json> --execution-campaign [--fuel ...]\n\n"
                   "options:\n"
                   "  --epochs N                 departure epochs, one per day (default 100)\n"
                   "  --epoch-spacing-days D     days between epochs (default 1)\n"
                   "  --execution MODEL          impulsive | finite | autopilot\n"
                   "  --objective NAME           balanced | min-total-dv | min-injection-dv |\n"
                   "                             shortest-tof\n"
                   "  --tof-days a,b,c           the time-of-flight grid\n"
                   "  --departure-window-hours H how far to coast the parking orbit\n"
                   "  --departure-samples N      samples inside that window\n"
                   "  --flyby-altitude-km A      the target orbit's periapsis\n"
                   "  --target-apoapsis-km A     the target orbit's apoapsis\n"
                   "  --target-inclination-deg I reported against, never steered to (s. 13)\n"
                   "  --periapsis-tolerance-km T how close the flyby has to land\n"
                   "  --min-departure-perigee-km P  the floor on the departure conic's perigee\n"
                   "  --refuse-departure-conic   refuse below that floor instead of pricing it\n"
                   "  --screened N / --flown N   how many candidates are ranked and flown\n"
                   "  --step-budget N            integrator steps before TIMEOUT\n"
                   "  --burn-offset-s S          capture burn offset from periapsis\n"
                   "  --autopilot-omega-n W      controller bandwidth [rad/s]\n"
                   "  --autopilot-zeta Z         damping ratio\n"
                   "  --settling-threshold-deg D where a slew counts as finished\n"
                   "  --omega-n a,b,c            the bandwidths --autopilot-sweep walks\n"
                   "  --parking-altitudes-km a,b      section 20's altitude axis\n"
                   "  --parking-inclinations-deg a,b  section 20's inclination axis\n"
                   "  --fuel abundant,limited,marginal,insufficient   section 21's tanks\n"
                   "  --jobs N                   worker threads\n";
            return args.has("help") ? 0 : 1;
        }

        const Context ctx = make_context(args);
        orbitcli::Scenario scenario = orbitcli::Scenario::load(args.positional.front());
        if (const auto epoch = args.option("date"); epoch.has_value()) {
            scenario.epoch_text = *epoch;
        }
        if (!scenario.craft.has_value()) {
            std::cerr << "the campaign needs a scenario with spacecraft.engine configured\n";
            return 2;
        }
        if (scenario.bodies.empty()) {
            scenario.bodies = celestial::BodyCatalog::default_solar_system_ids();
        }

        if (args.has("map")) {
            return run_map(args, ctx, std::move(scenario));
        }
        if (args.has("oberth-sweep")) {
            return run_oberth_sweep(args, ctx, std::move(scenario));
        }
        if (args.has("compare")) {
            return run_compare(args, ctx, std::move(scenario));
        }
        if (args.has("autopilot-sweep")) {
            return run_autopilot_sweep(args, ctx, std::move(scenario));
        }
        if (args.has("execution-campaign")) {
            return run_execution_campaign(args, ctx, std::move(scenario));
        }
        return run_campaign(args, ctx, std::move(scenario));
    } catch (const std::exception& e) {
        std::cerr << "lunar-campaign: " << e.what() << "\n";
        return 2;
    }
}
