#include "app/presentation/audio_director.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sf::app {
namespace {

// A time constant of about 120 ms up and down at 60 fps. Without it the cut-off
// is a step, and a step in a low sound clicks.
constexpr double kEngineSmoothing = 0.12;
// The gas answers faster than the engine: a valve opens in milliseconds.
constexpr double kRcsSmoothing = 0.35;

// How loud each mode is at its own full thrust. CRUISE is quieter because it is
// eighteen times less thrust through the same structure; it is not scaled by
// that ratio because then it would not be heard at all.
constexpr double kImpulseGain = 1.0;
constexpr double kCruiseGain = 0.6;

constexpr std::array<const char*, 4> kBeeps = {"beep_1", "beep_2", "beep_3", "beep_4"};

double approach(double current, double wanted, double rate) { return current + (wanted - current) * rate; }

}  // namespace

void AudioDirector::advance(double delta) {
    rcs_cooldown_ = std::max(rcs_cooldown_ - delta, 0.0);
    beep_countdown_ -= delta;
    if (beep_countdown_ <= 0.0) {
        schedule_beep();
    }
}

void AudioDirector::schedule_beep() {
    std::uniform_real_distribution<double> interval(BEEP_INTERVAL_MIN_S, BEEP_INTERVAL_MAX_S);
    std::uniform_int_distribution<std::size_t> which(0, kBeeps.size() - 1);
    std::uniform_real_distribution<double> spread(0.0, 1.0);
    beep_countdown_ = interval(beep_random_);
    const auto clip = kBeeps[which(beep_random_)];
    const double level = 0.10 + 0.08 * spread(beep_random_);
    const double pitch = 0.94 + 0.12 * spread(beep_random_);
    // The generator is drawn from whether or not the beep is heard, so leaving
    // the cockpit and coming back does not change the sequence.
    if (interior_) {
        play(clip, level, pitch);
    }
}

void AudioDirector::set_engine(const std::string& mode, double thrust_n, double full_thrust_n) {
    // RELATIVISTIC has no recording of its own yet: it plays the IMPULSE rumble
    // pitched a fifth down, a deeper and heavier sound than either fusion mode,
    // and crossfades from CRUISE like any other change of mode.
    const bool cruise = mode == "CRUISE";
    const bool relativistic = mode == "RELATIVISTIC";
    const double fraction = full_thrust_n > 0.0 ? std::clamp(thrust_n / full_thrust_n, 0.0, 1.0) : 0.0;
    // The mode not in use fades out on the same time constant, so switching
    // mode with the engine lit is a crossfade and not a cut.
    impulse_level_ = approach(impulse_level_, cruise ? 0.0 : fraction, kEngineSmoothing);
    cruise_level_ = approach(cruise_level_, cruise ? fraction : 0.0, kEngineSmoothing);
    const double gain = (interior_ ? 1.0 : 0.0) * master_volume_ * effects_volume_;
    // The pitch rises a little with thrust. A fuller sound, and the only thing
    // here that corresponds to nothing physical -- said out loud rather than
    // buried.
    const double impulse_pitch = relativistic ? 0.62 + 0.10 * impulse_level_ : 0.92 + 0.16 * impulse_level_;
    sink_->set_loop("engine_impulse", kImpulseGain * impulse_level_ * gain, impulse_pitch);
    sink_->set_loop("engine_cruise", kCruiseGain * cruise_level_ * gain, 0.96 + 0.08 * cruise_level_);
}

void AudioDirector::rcs_fired(int count) {
    if (!interior_ || count <= 0 || rcs_cooldown_ > 0.0) {
        return;
    }
    // One thump per firing, not per nozzle: twelve identical clicks in the same
    // millisecond add up to a crack and not to twelve thumps.
    rcs_cooldown_ = 0.09;
    play("rcs", 0.55 + 0.05 * std::min(count, 6), 0.92 + 0.03 * std::min(count, 4));
}

void AudioDirector::set_rcs_activity(int firing) {
    // Louder with more nozzles, but by the square root: six nozzles are not six
    // times the hiss of one, they are more gas through the same plumbing.
    const double wanted = firing > 0 ? 0.20 * std::sqrt(static_cast<double>(std::min(firing, 8))) : 0.0;
    rcs_level_ = approach(rcs_level_, wanted, kRcsSmoothing);
    const double gain = (interior_ ? 1.0 : 0.0) * master_volume_ * effects_volume_;
    sink_->set_loop("rcs_hiss", rcs_level_ * gain, 1.0);
}

void AudioDirector::set_interior(bool inside) {
    interior_ = inside;
    apply_volumes();
}

void AudioDirector::set_volumes(double master, double effects) {
    master_volume_ = std::clamp(master, 0.0, 1.0);
    effects_volume_ = std::clamp(effects, 0.0, 1.0);
    apply_volumes();
}

void AudioDirector::apply_volumes() {
    const double ambient = (interior_ ? 1.0 : 0.0) * master_volume_ * effects_volume_;
    // Two tonal layers, a fan and the hum of the ship's machinery, at about the
    // same weight: together they beat slowly against each other, which is what
    // keeps a steady hum from vanishing into the background.
    sink_->set_loop("ventilation", 0.22 * ambient, 1.0);
    sink_->set_loop("equipment", 0.18 * ambient, 1.0);
}

void AudioDirector::play(const std::string& clip, double level, double pitch) {
    sink_->play(clip, level * master_volume_ * effects_volume_, pitch);
}

}  // namespace sf::app
