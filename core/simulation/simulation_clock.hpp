#pragma once

// Keeps the four clocks of the simulation apart (rule section 21):
//
//   wall time        real seconds outside the simulation
//   coordinate time  TDB; the integration variable
//   proper time      the spacecraft's own clock
//   render time      real seconds, for animation and interpolation
//
// Time warp scales wall time into coordinate time.  It does NOT touch the
// integrator's step size: the propagator is asked to reach a later instant and
// decides on its own how many steps that takes.
//
// The clock never reads a real-time source itself: wall deltas are passed in.
// That keeps it deterministic and testable, and keeps std::chrono out of the
// scientific core's behaviour.

#include "core/time/coordinate_time.hpp"
#include "core/time/duration.hpp"

#include <span>
#include <string>

namespace sf::simulation {

class SimulationClock {
public:
    explicit SimulationClock(time::CoordinateTime epoch);

    [[nodiscard]] time::CoordinateTime epoch() const noexcept { return epoch_; }
    [[nodiscard]] time::CoordinateTime coordinate_time() const noexcept { return coordinate_; }
    [[nodiscard]] time::Duration proper_time() const noexcept { return proper_; }
    [[nodiscard]] time::Duration wall_time() const noexcept { return wall_; }
    [[nodiscard]] time::Duration render_time() const noexcept { return wall_; }

    [[nodiscard]] time::Duration elapsed_coordinate() const { return coordinate_ - epoch_; }

    // Coordinate time minus proper time: the number the cockpit shows as "TIME
    // DIFFERENCE".  Zero while the dynamics are Newtonian.
    [[nodiscard]] time::Duration clock_difference() const {
        return elapsed_coordinate() - proper_;
    }

    [[nodiscard]] double time_warp() const noexcept { return time_warp_; }
    void set_time_warp(double factor);

    // Coordinate time the propagator should be asked to reach after `wall_dt` of
    // real time.  Pure function of the current state: it does not advance
    // anything.
    [[nodiscard]] time::Duration coordinate_advance_for(time::Duration wall_dt) const;
    [[nodiscard]] time::CoordinateTime target_for(time::Duration wall_dt) const;

    // Records the outcome of a propagation.  `new_time` is where the propagator
    // actually arrived (which may differ from the target if it stopped early),
    // and `proper_elapsed` the proper time it accumulated getting there.
    void commit(time::CoordinateTime new_time, time::Duration proper_elapsed, time::Duration wall_dt);

    // Jumps the coordinate clock without any propagation: scenario setup only.
    void reset_to(time::CoordinateTime t);

    [[nodiscard]] std::string to_string() const;

    // The warp ladder offered by the UI.  Nothing in the core depends on these
    // being the only values; the warp factor is a continuous double.
    [[nodiscard]] static std::span<const double> warp_levels();

private:
    time::CoordinateTime epoch_{};
    time::CoordinateTime coordinate_{};
    time::Duration proper_{};
    time::Duration wall_{};
    double time_warp_{1.0};
};

}  // namespace sf::simulation
