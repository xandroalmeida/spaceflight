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
// Here the campaign calls core/navigation/lunar_transfer.hpp directly, and the
// CSV header lives in TransferRecord so the tool cannot disagree with the data.
//
//   lunar-campaign <scenario.json> --epochs 100 [options]
//
// Modes:
//   (default)          one transfer per epoch, one CSV row each
//   --map              the departure x time-of-flight grid for ONE epoch
//   --oberth-sweep     one epoch, the capture burn walked around periapsis
//   --compare          the same epochs under IMPULSIVE and FINITE_BURN
//
// See docs/validation/lunar-navigation-hardening.md.

#include "core/celestial/body_catalog.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_kernel_set.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "core/navigation/lunar_transfer.hpp"
#include "core/units/conversions.hpp"
#include "tools/orbit-cli/scenario.hpp"

#include <algorithm>
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

navigation::LunarTransferConfig config_from(const Args& args) {
    navigation::LunarTransferConfig config{};
    config.departure_window =
        time::Duration::hours(args.number("departure-window-hours", 2.0));
    config.departure_samples = args.integer("departure-samples", 16);
    config.flyby_altitude = args.number("flyby-altitude-km", 100.0) * 1000.0;
    config.periapsis_tolerance = args.number("periapsis-tolerance-km", 2.0) * 1000.0;
    config.b_plane_angle = units::Angle::degrees(args.number("b-plane-angle", 0.0));
    config.minimum_departure_perigee_altitude =
        args.number("min-departure-perigee-km", 120.0) * 1000.0;
    // The stricter reading of section 8's hard constraint: refuse a departure
    // conic below the floor instead of pricing it.  Off by default -- the screen
    // reads the UNCORRECTED conic and the corrector routinely lifts a perigee
    // that started below the surface, so refusing refuses flyable transfers.
    config.refuse_departure_conic_below_floor = args.has("refuse-departure-conic");
    config.screened_candidates = args.integer("screened", 24);
    config.flown_candidates = args.integer("flown", 6);
    config.b_plane_passes = args.integer("bplane-passes", 4);
    config.step_budget = static_cast<std::size_t>(args.number("step-budget", 2.0e6));
    config.capture_burn_offset_seconds = args.number("burn-offset-s", 0.0);

    if (const auto tofs = args.option("tof-days"); tofs.has_value()) {
        config.time_of_flight_days.clear();
        std::istringstream stream{*tofs};
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (!token.empty()) {
                config.time_of_flight_days.push_back(std::stod(token));
            }
        }
    }

    const std::string execution = args.option_or("execution", "finite");
    if (execution == "impulsive") {
        config.execution = navigation::ExecutionModel::Impulsive;
    } else if (execution == "autopilot") {
        config.execution = navigation::ExecutionModel::Autopilot;
    } else {
        config.execution = navigation::ExecutionModel::FiniteBurn;
    }

    // The autopilot's controller bandwidth, exposed because it is the quantity
    // the whole IMPULSIVE/FINITE/AUTOPILOT comparison turns out to be about: a
    // critically damped PD tracking a target that rotates at omega settles at a
    // lag of 2 zeta omega / omega_n, and at periapsis of a 100 km lunar orbit
    // omega is 8.9e-4 rad/s.
    config.autopilot_gains.natural_frequency = args.number("autopilot-omega-n", 0.05);
    config.autopilot_gains.damping_ratio = args.number("autopilot-zeta", 1.0);

    config.target_orbit.mean_altitude = args.number("target-altitude-km", 100.0) * 1000.0;
    config.target_orbit.max_eccentricity = args.number("max-ecc", 0.01);
    config.target_orbit.min_periapsis_altitude =
        args.number("min-periapsis-km", 80.0) * 1000.0;
    config.target_orbit.max_apoapsis_altitude =
        args.number("max-apoapsis-km", 120.0) * 1000.0;
    return config;
}

navigation::TransferInputs inputs_from(const Context& ctx, const orbitcli::Scenario& scenario,
                                       time::CoordinateTime epoch) {
    navigation::TransferInputs inputs{};
    inputs.provider = ctx.provider.get();
    inputs.orientation = ctx.provider.get();
    inputs.craft = &*scenario.craft;
    inputs.catalog = celestial::BodyCatalog::resolve(*ctx.provider, scenario.bodies);
    inputs.j2_bodies = scenario.j2_bodies;
    inputs.center = scenario.relative_to;
    inputs.target = celestial::bodies::moon;
    inputs.parking.position = scenario.position;
    inputs.parking.velocity = scenario.velocity;
    inputs.epoch = epoch;
    inputs.integrator = scenario.integrator;
    return inputs;
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
    const auto config = config_from(args);
    const auto t0 = ctx.time->parse(scenario.epoch_text);

    std::cout << "Earth-Moon campaign\n"
              << "  scenario        : " << scenario.name << "\n"
              << "  first epoch     : " << ctx.time->to_utc_string(t0, 0) << "\n"
              << "  epochs          : " << epochs << ", one every " << spacing_days << " day(s)\n"
              << "  execution       : " << navigation::to_string(config.execution) << "\n"
              << "  departure window: " << config.departure_window.hours() << " h, "
              << config.departure_samples << " samples\n"
              << "  time of flight  : ";
    for (std::size_t i = 0; i < config.time_of_flight_days.size(); ++i) {
        std::cout << (i > 0 ? ", " : "") << config.time_of_flight_days[i];
    }
    std::cout << " d\n"
              << "  flyby altitude  : " << config.flyby_altitude / 1000.0 << " km\n"
              << "  target orbit    : periapsis >= "
              << config.target_orbit.min_periapsis_altitude / 1000.0 << " km, apoapsis <= "
              << config.target_orbit.max_apoapsis_altitude / 1000.0 << " km, e <= "
              << config.target_orbit.max_eccentricity << "\n\n";

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
            auto inputs = inputs_from(ctx, scenario, epoch);
            navigation::TransferRecord record{};
            try {
                record = navigation::plan_and_fly(inputs, config);
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
    print_distribution("propellant used", fuel_kg, "kg");
    print_distribution("time of flight", tof_days, "d");
    print_distribution("transfer angle", transfer_angle_deg, "deg");
    print_distribution("runtime per epoch", runtime_s, "s");

    return successes == epochs ? 0 : 1;
}

// ---------------------------------------------------------------------------

int run_map(const Args& args, const Context& ctx, orbitcli::Scenario scenario) {
    const auto config = config_from(args);
    const auto t0 = ctx.time->parse(scenario.epoch_text);
    const auto inputs = inputs_from(ctx, scenario, t0);
    const auto grid = navigation::map_transfer_grid(inputs, config);

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
        const bool wanted = direction == trajectory::TransferDirection::Prograde
                                ? config.try_prograde
                                : config.try_retrograde;
        if (!wanted) {
            continue;
        }
        std::cout << "\n"
                  << (direction == trajectory::TransferDirection::Prograde ? "prograde"
                                                                          : "retrograde")
                  << " branch   ( . no solution   x degenerate   E departure conic hits Earth"
                     "   $ unaffordable   # feasible )\n";
        for (const double tof : config.time_of_flight_days) {
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
    auto config = config_from(args);
    const auto t0 = ctx.time->parse(scenario.epoch_text);
    const auto inputs = inputs_from(ctx, scenario, t0);

    std::vector<double> offsets{-120.0, -60.0, -30.0, 0.0, 30.0, 60.0, 120.0};
    if (const auto given = args.option("offsets"); given.has_value()) {
        offsets.clear();
        std::istringstream stream{*given};
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (!token.empty()) {
                offsets.push_back(std::stod(token));
            }
        }
    }

    std::cout << "Oberth sensitivity at " << ctx.time->to_utc_string(t0, 0) << ", execution "
              << navigation::to_string(config.execution) << "\n\n"
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
        config.capture_burn_offset_seconds = offset;
        const auto record = navigation::plan_and_fly(inputs, config);
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

int run_compare(const Args& args, const Context& ctx, orbitcli::Scenario scenario) {
    const int epochs = args.integer("epochs", 10);
    const double spacing_days = args.number("epoch-spacing-days", 1.0);
    auto config = config_from(args);
    const auto t0 = ctx.time->parse(scenario.epoch_text);

    // Section 11, and the reason it is done this way.
    //
    // The three models are supposed to differ in ONE thing: how the burns are
    // delivered.  Letting each run its own search makes them differ in
    // everything, and the comparison stops being a comparison.  So the FINITE
    // model searches, and its chosen departure is PINNED for all three -- each
    // then corrects and flies that same geometry in its own model.
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
        const auto inputs = inputs_from(ctx, scenario, epoch);

        config.pinned = {};
        config.execution = navigation::ExecutionModel::FiniteBurn;
        const auto search = navigation::plan_and_fly(inputs, config);
        if (!search.success) {
            std::cout << "  " << ctx.time->to_utc_string(epoch, 0)
                      << "  search found nothing to pin: "
                      << navigation::to_string(search.failure) << "\n";
            ++planner_faults;
            continue;
        }

        config.pinned.active = true;
        config.pinned.coast_s = search.departure_coast_s;
        config.pinned.time_of_flight_days = search.time_of_flight_s / 86400.0;
        config.pinned.direction = search.lambert_direction;

        navigation::TransferRecord results[3];
        const navigation::ExecutionModel models[3] = {navigation::ExecutionModel::Impulsive,
                                                      navigation::ExecutionModel::FiniteBurn,
                                                      navigation::ExecutionModel::Autopilot};
        for (int m = 0; m < 3; ++m) {
            config.execution = models[m];
            results[m] = navigation::plan_and_fly(inputs, config);
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

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse(argc, argv);
        if (args.positional.empty() || args.has("help")) {
            std::cout
                << "usage: lunar-campaign <scenario.json> [--epochs N] [--csv <file>]\n"
                   "       lunar-campaign <scenario.json> --map [--csv <file>]\n"
                   "       lunar-campaign <scenario.json> --oberth-sweep [--offsets a,b,c]\n"
                   "       lunar-campaign <scenario.json> --compare [--epochs N]\n\n"
                   "options:\n"
                   "  --epochs N                 departure epochs, one per day (default 100)\n"
                   "  --epoch-spacing-days D     days between epochs (default 1)\n"
                   "  --execution MODEL          impulsive | finite (default finite)\n"
                   "  --tof-days a,b,c           the time-of-flight grid\n"
                   "  --departure-window-hours H how far to coast the parking orbit\n"
                   "  --departure-samples N      samples inside that window\n"
                   "  --flyby-altitude-km A      where the flyby is aimed\n"
                   "  --periapsis-tolerance-km T how close it has to land\n"
                   "  --min-departure-perigee-km P  the floor on the departure conic's perigee\n"
                   "  --refuse-departure-conic   refuse below that floor instead of pricing it\n"
                   "  --screened N / --flown N   how many candidates are ranked and flown\n"
                   "  --step-budget N            integrator steps before TIMEOUT\n"
                   "  --burn-offset-s S          capture burn offset from periapsis\n"
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
        return run_campaign(args, ctx, std::move(scenario));
    } catch (const std::exception& e) {
        std::cerr << "lunar-campaign: " << e.what() << "\n";
        return 2;
    }
}
