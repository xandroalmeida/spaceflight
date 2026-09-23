#pragma once

// What the pilot commands, and nothing else (rules 14, 15, 28).
//
// Every key becomes a REQUEST to the core: a torque, a force, a throttle, a
// pointing mode. There is no path from here to the orientation or to the
// velocity -- the RCS burns propellant to turn the ship, the engine burns
// propellant to accelerate it, and both happen inside the integrator.
//
// The throttle changes BETWEEN frames, never inside a propagation step: opening
// it in the middle of a step would be a discontinuity in the derivative, which
// is the same reason planned burns are split at their ignition epochs
// (docs/architecture/navigation.md section 4).

#include "app/presentation/input_actions.hpp"
#include "app/presentation/message_log.hpp"
#include "app/session/flight_session.hpp"

#include <array>
#include <functional>
#include <string>

namespace sf::app {

class FlightControls {
public:
    // Manual torque command, N m. Roughly what the modelled RCS delivers; asking
    // for more only saturates the allocator, which the instrument shows.
    static constexpr double MANUAL_TORQUE = 400.0;
    // Translation force command, N. Two 180 N thrusters per axis.
    static constexpr double MANUAL_FORCE = 360.0;

    static constexpr std::array<double, 9> WARP_LEVELS = {1.0,      10.0,  100.0, 1000.0, 10000.0,
                                                          100000.0, 1.0e6, 1.0e7, 1.0e8};
    static constexpr double THROTTLE_TRIM = 0.5;   // per second, with Shift/Ctrl held

    // How long `Shift` (or `Ctrl`) has to be held on its own before it starts
    // moving the throttle.
    //
    // ⚠️ This exists because of a real collision in the rule-13 key map, which
    // the rule itself proposes: `Shift` opens the throttle AND `Shift+P` is
    // retrograde. Without this, asking for retrograde opened the throttle in
    // passing -- about 5 % per tap, enough for a ship on station to start
    // accelerating without anyone having asked.
    //
    // 0.2 s is longer than any `Shift+key` chord takes and shorter than anyone
    // waits before deciding the throttle is broken. And if a modified command
    // fires during the hold, the trim stays blocked until the modifier is
    // released -- see `block_throttle_trim`.
    static constexpr double TRIM_ARM_SECONDS = 0.20;

    explicit FlightControls(FlightSession& session);

    // Called every frame, before `advance`.
    void apply(double delta, const KeyboardState& keyboard);

    // Called when a command with a modifier fires: that `Shift` was part of a
    // chord and not a throttle request.
    void block_throttle_trim() { trim_blocked_ = true; }

    // What the RCS is doing NOW (rule 15). `firing` is how many thrusters the
    // ALLOCATOR opened, and it is an argument on purpose: the first version
    // derived the label from the manual commands alone and wrote "IDLE" while
    // four thrusters were burning for the autopilot -- exactly the lie rule 15
    // forbids, committed in the text instead of the drawing.
    [[nodiscard]] std::string rcs_activity(int firing = 0) const;

    void set_throttle(double value);
    void toggle_rcs();
    void point(const std::string& mode);
    void step_warp(int direction);
    // Pausing stops SIMULATION TIME and only that: the camera keeps moving and
    // the interface keeps answering (rule 69). The orchestrator does it by not
    // calling `advance()`; it is NOT done by setting the warp to zero, because
    // SimulationClock refuses a warp that is not strictly positive -- a stopped
    // clock is not a slow clock.
    void set_paused(bool value) { paused_ = value; }
    void cycle_engine_mode();
    void set_warp_index(int index);

    [[nodiscard]] double warp() const { return WARP_LEVELS[static_cast<std::size_t>(warp_index_)]; }
    [[nodiscard]] int warp_index() const { return warp_index_; }
    [[nodiscard]] double throttle() const { return throttle_; }
    [[nodiscard]] bool rcs_enabled() const { return rcs_enabled_; }
    [[nodiscard]] bool paused() const { return paused_; }

    // The signals the orchestrator listens to.
    std::function<void(const std::string&, MessageLevel)> on_message;
    std::function<void()> on_rcs_fired;
    std::function<void(bool)> on_engine_changed;

private:
    void message(const std::string& text, MessageLevel level) const {
        if (on_message) {
            on_message(text, level);
        }
    }

    FlightSession& session_;
    double throttle_{0.0};
    bool rcs_enabled_{true};
    int warp_index_{0};
    bool paused_{false};

    math::Vec3 last_torque_{};
    math::Vec3 last_force_{};
    double trim_hold_{0.0};
    bool trim_blocked_{false};
};

}  // namespace sf::app
