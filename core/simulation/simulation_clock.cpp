#include "core/simulation/simulation_clock.hpp"

#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace sf::simulation {
namespace {
constexpr std::array<double, 6> kWarpLevels{1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0};
}  // namespace

SimulationClock::SimulationClock(time::CoordinateTime epoch)
    : epoch_(epoch), coordinate_(epoch) {}

void SimulationClock::set_time_warp(double factor) {
    if (!std::isfinite(factor) || factor <= 0.0) {
        throw std::invalid_argument("SimulationClock: time warp must be finite and > 0");
    }
    time_warp_ = factor;
}

time::Duration SimulationClock::coordinate_advance_for(time::Duration wall_dt) const {
    return wall_dt * time_warp_;
}

time::CoordinateTime SimulationClock::target_for(time::Duration wall_dt) const {
    return coordinate_ + coordinate_advance_for(wall_dt);
}

void SimulationClock::commit(time::CoordinateTime new_time, time::Duration proper_elapsed,
                             time::Duration wall_dt) {
    coordinate_ = new_time;
    proper_ += proper_elapsed;
    wall_ += wall_dt;
}

void SimulationClock::reset_to(time::CoordinateTime t) {
    epoch_ = t;
    coordinate_ = t;
    proper_ = time::Duration::zero();
}

std::string SimulationClock::to_string() const {
    std::ostringstream os;
    os << "coordinate " << coordinate_.to_string()
       << " | elapsed " << elapsed_coordinate().seconds() << " s"
       << " | proper " << proper_.seconds() << " s"
       << " | difference " << clock_difference().seconds() << " s"
       << " | warp " << time_warp_ << "x";
    return os.str();
}

std::span<const double> SimulationClock::warp_levels() {
    return std::span<const double>{kWarpLevels};
}

}  // namespace sf::simulation
