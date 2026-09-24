#include "app/presentation/flight_controls.hpp"

#include "app/presentation/format.hpp"

#include <algorithm>
#include <cmath>

namespace sf::app {

FlightControls::FlightControls(FlightSession& session) : session_(session) {}

void FlightControls::apply(double delta, const KeyboardState& keyboard) {
    math::Vec3 torque{};
    math::Vec3 force{};

    if (rcs_enabled_) {
        // Pitch about body +y, yaw about +z, roll about +x. The nose is +x, so it
        // is this convention and no other; a sign swapped here makes the pilot fly
        // with the stick reversed and nothing reports it except flying.
        torque.y += input::strength(keyboard, "pitch_up") * MANUAL_TORQUE;
        torque.y -= input::strength(keyboard, "pitch_down") * MANUAL_TORQUE;
        torque.z += input::strength(keyboard, "yaw_left") * MANUAL_TORQUE;
        torque.z -= input::strength(keyboard, "yaw_right") * MANUAL_TORQUE;
        torque.x += input::strength(keyboard, "roll_right") * MANUAL_TORQUE;
        torque.x -= input::strength(keyboard, "roll_left") * MANUAL_TORQUE;

        force.x += input::strength(keyboard, "translate_forward") * MANUAL_FORCE;
        force.x -= input::strength(keyboard, "translate_back") * MANUAL_FORCE;
        force.y += input::strength(keyboard, "translate_left") * MANUAL_FORCE;
        force.y -= input::strength(keyboard, "translate_right") * MANUAL_FORCE;
        force.z += input::strength(keyboard, "translate_up") * MANUAL_FORCE;
        force.z -= input::strength(keyboard, "translate_down") * MANUAL_FORCE;
    }

    session_.set_manual_torque(torque);
    session_.set_manual_translation(force);

    const bool was_firing = last_torque_.norm() > 0.0 || last_force_.norm() > 0.0;
    const bool firing = torque.norm() > 0.0 || force.norm() > 0.0;
    if (firing && !was_firing && on_rcs_fired) {
        on_rcs_fired();
    }
    last_torque_ = torque;
    last_force_ = force;

    // Shift and Ctrl held trim the throttle continuously. The trim is per
    // SECOND and not per frame: at 30 fps and at 120 fps the throttle has to
    // rise at the same rate, or the frame rate becomes a flight control.
    double trim = 0.0;
    if (input::held(keyboard, "throttle_up")) {
        trim += THROTTLE_TRIM;
    }
    if (input::held(keyboard, "throttle_down")) {
        trim -= THROTTLE_TRIM;
    }

    if (trim == 0.0) {
        // The modifier was released: the block ends here and the counter resets.
        trim_hold_ = 0.0;
        trim_blocked_ = false;
        return;
    }
    trim_hold_ += delta;
    if (trim_blocked_ || trim_hold_ < TRIM_ARM_SECONDS) {
        return;
    }
    set_throttle(throttle_ + trim * delta);
}

std::string FlightControls::rcs_activity(int firing) const {
    if (!rcs_enabled_) {
        return "OFF";
    }
    const bool rotating = last_torque_.norm() > 0.0;
    const bool translating = last_force_.norm() > 0.0;
    if (rotating && translating) {
        return "ROT+TRANS";
    }
    if (rotating) {
        return "ROTATION";
    }
    if (translating) {
        return "TRANSLATION";
    }
    if (firing > 0) {
        // Nobody pressed a key and thrusters are open: the attitude controller,
        // inside the core, is flying.
        return "AUTOPILOT";
    }
    return "IDLE";
}

void FlightControls::set_throttle(double value) {
    const double wanted = std::clamp(value, 0.0, 1.0);
    // Godot's is_equal_approx: the same number to within one part in 1e5.
    if (std::abs(wanted - throttle_) <= std::max(1.0e-5 * std::abs(throttle_), 1.0e-5)) {
        return;
    }
    const bool was_running = throttle_ > 0.0;
    throttle_ = wanted;
    session_.set_throttle(throttle_);
    if ((throttle_ > 0.0) != was_running && on_engine_changed) {
        on_engine_changed(throttle_ > 0.0);
    }
}

void FlightControls::toggle_rcs() {
    rcs_enabled_ = !rcs_enabled_;
    if (!rcs_enabled_) {
        session_.set_manual_torque(math::Vec3{});
        session_.set_manual_translation(math::Vec3{});
        // Switching the RCS off has to switch the pointing off too. The attitude
        // controller lives INSIDE the core and would keep asking for torque; a
        // switch that put out the drawn flames and left propellant flowing would
        // be exactly the lie rule 15 forbids, the other way round.
        session_.set_pointing_mode("");
    }
    message(std::string{"RCS "} + (rcs_enabled_ ? "ENABLED" : "DISABLED"), MessageLevel::Info);
}

void FlightControls::point(const std::string& mode) {
    if (!rcs_enabled_ && !mode.empty()) {
        message("RCS DISABLED -- pointing unavailable", MessageLevel::Warning);
        return;
    }
    if (!session_.set_pointing_mode(mode)) {
        // An armed mission flies the ship, and the session REFUSES the command
        // rather than accepting it and overwriting it on the next frame. Saying
        // so out loud is the difference between a cockpit that ignores the
        // pilot and one that says who has the controls.
        message(session_.last_error(), MessageLevel::Warning);
        return;
    }
    message("ATTITUDE " + (mode.empty() ? std::string{"HOLD"} : fmt::upper(mode)), MessageLevel::Info);
}

void FlightControls::step_warp(int direction) {
    const int wanted = std::clamp(warp_index_ + direction, 0, static_cast<int>(WARP_LEVELS.size()) - 1);
    if (wanted == warp_index_) {
        return;
    }
    warp_index_ = wanted;
    session_.set_time_warp(warp());
    // Never silently (rule 28).
    message("TIME WARP " + fmt::warp(warp()), MessageLevel::Info);
}

void FlightControls::set_warp_index(int index) {
    warp_index_ = std::clamp(index, 0, static_cast<int>(WARP_LEVELS.size()) - 1);
    session_.set_time_warp(warp());
}

void FlightControls::cycle_engine_mode() {
    if (session_.has_plan()) {
        message("PLAN ARMED -- engine mode locked to " + session_.engine_mode(), MessageLevel::Warning);
        return;
    }
    session_.cycle_engine_mode();
    message("ENGINE " + session_.engine_mode(), MessageLevel::Info);
}

void FlightControls::set_engine_mode(const std::string& mode) {
    if (mode == session_.engine_mode()) {
        return;
    }
    if (session_.has_plan()) {
        message("PLAN ARMED -- engine mode locked to " + session_.engine_mode(), MessageLevel::Warning);
        return;
    }
    if (session_.set_engine_mode(mode)) {
        message("ENGINE " + session_.engine_mode(), MessageLevel::Info);
    }
}

}  // namespace sf::app
