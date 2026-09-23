#include "app/presentation/headless_driver.hpp"

#include "app/presentation/flight_app.hpp"
#include "app/presentation/format.hpp"
#include "app/presentation/ui/debug_hud.hpp"

#include <cstdio>
#include <cstdlib>

namespace sf::app {

HeadlessDriver::HeadlessDriver(FlightApp& flight, std::string destination) : flight_(flight) {
    const char* mission = std::getenv("SPACEFLIGHT_HEADLESS_MISSION");
    mission_mode_ = mission != nullptr && std::string{mission} == "1";
    const char* wanted = std::getenv("SPACEFLIGHT_HEADLESS_DESTINATION");
    if (destination.empty() && wanted != nullptr) {
        destination = wanted;
    }
    if (!destination.empty()) {
        destination_ = destination;
        mission_mode_ = true;
    }
}

void HeadlessDriver::print_readout(const std::vector<std::string>& readout) const {
    std::printf("\n");
    for (const auto& line : readout) {
        std::printf("%s\n", line.c_str());
    }
    std::fflush(stdout);
}

void HeadlessDriver::drive(const std::vector<std::string>& readout) {
    auto& session = flight_.session();
    const auto s = session.snapshot();
    if (!s.valid) {
        return;
    }
    if (mission_mode_) {
        fly_the_mission(s.elapsed_s, readout);
        return;
    }

    // A slew in passing: the printed pointing error then exercises the whole
    // attitude chain -- controller, RCS, torque, Euler's equations -- end to end.
    if (!slew_commanded_ && s.elapsed_s > 2.0) {
        slew_commanded_ = true;
        session.set_pointing_mode("prograde");
        std::printf("\n[headless] commanded PROGRADE\n");
    }
    // Once pointed, burn: the apoapsis has to rise while the propellant falls.
    //
    // 3 degrees and not 1: a PD controller settles at the tracking lag
    // 2 zeta n / omega_n = 2.593 degrees and never gets closer
    // (docs/physics/attitude.md section 7.1). A threshold below that waits
    // forever -- which is what the first version did.
    //
    // Note the first condition: the pointing error is 0 while the mode is HOLD
    // (no target, no error), and without it the burn fires immediately -- 90
    // degrees from prograde.
    if (slew_commanded_ && !burn_commanded_ && s.pointing_mode == "PROGRADE" && s.pointing_error_deg < 3.0) {
        burn_commanded_ = true;
        flight_.controls().set_throttle(1.0);
        std::printf("\n[headless] throttle 100%% at %.3f deg of pointing error\n", s.pointing_error_deg);
    }
    warp_schedule(s.elapsed_s);

    frames_ += 1;
    if (frames_ % PRINT_EVERY_FRAMES == 0) {
        print_readout(readout);
    }
    // Once, early: the milestone's headline numbers, produced by the code that
    // runs and not quoted from the document.
    if (frames_ == PRINT_EVERY_FRAMES) {
        for (const double beta : {0.0896, 0.9048, 0.99}) {
            std::printf("\n");
            for (const auto& line : debug_hud::sky_projection_lines(flight_, beta)) {
                std::printf("%s\n", line.c_str());
            }
        }
        std::fflush(stdout);
    }
}

void HeadlessDriver::warp_schedule(double elapsed) {
    auto& controls = flight_.controls();
    int wanted = controls.warp_index();
    if (!burn_commanded_) {
        // The slew takes a couple of minutes of simulation time, which at warp 1
        // is more frames than any verification run has. Warping through it
        // changes nothing physical: the warp decides how much coordinate time a
        // frame asks for, never how the propagator gets there.
        //
        // 10x and not 100x: while the RCS fires, the propagator's own error
        // control keeps the steps short, so a frame at 100x costs ten times the
        // work and buys nothing.
        wanted = 1;
    } else if (elapsed > 2.0e6) {
        wanted = static_cast<int>(FlightControls::WARP_LEVELS.size()) - 1;   // 1e8
    } else if (elapsed > 1.0e4) {
        wanted = 7;   // 1e7
    } else if (elapsed > 6.0e2) {
        wanted = 5;   // 1e5
    } else {
        wanted = 2;   // 100
    }
    if (wanted != controls.warp_index()) {
        controls.set_warp_index(wanted);
    }
    // CRUISE trades thrust for exhaust velocity: a budget of 0.0899 c becomes
    // 0.9048 c. Switched as soon as the impulse burn has done its part.
    if (!cruise_commanded_ && elapsed > 1.0e4) {
        cruise_commanded_ = true;
        flight_.session().set_engine_mode("CRUISE");
        std::printf("\n[headless] CRUISE -- exhaust 0.5 c, budget 0.9048 c\n");
        std::fflush(stdout);
    }
}

void HeadlessDriver::fly_the_mission(double elapsed, const std::vector<std::string>& readout) {
    // The whole mission, without a keyboard: choose the destination, SEARCH,
    // arm, and let the warp carry the days -- six and three quarters to the Moon,
    // two hundred and four to Mars.
    //
    // run_mission splits every frame at the ignition and cut-off epochs, so a
    // frame that spans a whole burn still integrates it correctly -- which is why
    // warping through one is safe here.
    //
    // The search is asynchronous, so this is a state machine and not two calls in
    // a row: plan_mission() returns "I started", not "I found".
    auto& session = flight_.session();
    if (!mission_commanded_ && elapsed > 1.0) {
        mission_commanded_ = true;
        flight_.set_target(destination_);
        const double altitude = destination_ == "Moon" ? 100.0 : 500.0;
        std::printf("\n[headless] searching for a transfer to %s (%.0f km circular)\n", destination_.c_str(), altitude);
        std::fflush(stdout);
        flight_.plan_mission(destination_, altitude, altitude);
    }

    // While the search runs, the frame keeps going -- which is its point.
    if (session.is_planning()) {
        frames_ += 1;
        if (frames_ % PRINT_EVERY_FRAMES == 0) {
            const auto progress = session.planning_progress();
            std::printf("[headless] searching: %s, %lld of %lld screened, %lld flown\n",
                        progress.stage.empty() ? "?" : progress.stage.c_str(), progress.candidates_screened,
                        progress.candidates_considered, progress.candidates_flown);
            std::fflush(stdout);
        }
        return;
    }

    if (mission_commanded_ && !mission_armed_ && session.has_planned_transfer()) {
        mission_armed_ = true;
        const auto plan = session.plan();
        std::printf("\n[headless] plan: %s -> %s, %s, %.0f m/s total\n", plan.origin.c_str(), plan.target.c_str(),
                    fmt::duration(plan.time_of_flight_days * 86400.0).c_str(), plan.total_delta_v);
        if (flight_.arm_mission()) {
            // The warp that makes the journey fit a headless run. A 6.75-day lunar
            // flight at 1e5x is about 1200 frames; a 204-day Martian one needs 1e7
            // to fit in the same budget.
            flight_.controls().set_warp_index(destination_ == "Moon" ? 5 : 7);
            std::printf("[headless] armed, warp %s\n", fmt::warp(flight_.controls().warp()).c_str());
        }
        std::fflush(stdout);
    }

    frames_ += 1;
    if (frames_ % PRINT_EVERY_FRAMES == 0) {
        print_readout(readout);
    }

    if (session.has_plan()) {
        const auto p = session.plan();
        if (p.done && !mission_reported_) {
            mission_reported_ = true;
            std::printf("\n===== ARRIVED =====\n");
            for (const auto& line : debug_hud::hud_lines(flight_, false)) {
                std::printf("%s\n", line.c_str());
            }
            const auto outcome = session.mission_outcome();
            if (outcome.recorded) {
                std::printf("predicted vs actual: periapsis %.1f/%.1f km, apoapsis %.1f/%.1f km, e %.5f/%.5f, "
                            "i %.3f/%.3f deg\n",
                            outcome.periapsis_m.predicted / 1000.0, outcome.periapsis_m.actual / 1000.0,
                            outcome.apoapsis_m.predicted / 1000.0, outcome.apoapsis_m.actual / 1000.0,
                            outcome.eccentricity.predicted, outcome.eccentricity.actual,
                            outcome.inclination_deg.predicted, outcome.inclination_deg.actual);
            }
            std::printf("===== end =====\n");
            std::fflush(stdout);
            // And STOP warping. Without this the run carries on at 1e7x until the
            // frame count runs out -- 79 YEARS of simulated time, past the end of
            // de440s's coverage.
            flight_.controls().set_warp_index(0);
        }
    }
}

}  // namespace sf::app
