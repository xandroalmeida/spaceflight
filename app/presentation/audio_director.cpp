#include "app/presentation/audio_director.hpp"

#include <algorithm>

namespace sf::app {

void AudioDirector::advance(double delta) { rcs_cooldown_ = std::max(rcs_cooldown_ - delta, 0.0); }

void AudioDirector::set_engine_thrust(double thrust_n) {
    const double wanted = std::clamp(thrust_n / ENGINE_REFERENCE_N, 0.0, 1.0);
    // A time constant of about 120 ms up and down. Without it the cut-off is a
    // step, and a step in a low sound clicks.
    engine_level_ += (wanted - engine_level_) * 0.12;
    const double level = interior_ ? engine_level_ : 0.0;
    // The pitch rises a little with thrust: 0.92x idle, 1.08x at full. A fuller
    // sound, and the only thing here that corresponds to nothing physical --
    // said out loud rather than buried.
    sink_->set_loop("engine", level * master_volume_ * effects_volume_, 0.92 + 0.16 * engine_level_);
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
    const double ambient = (interior_ ? 0.30 : 0.0) * master_volume_ * effects_volume_;
    sink_->set_loop("ventilation", ambient, 1.0);
}

void AudioDirector::play(const std::string& clip, double level, double pitch) {
    sink_->play(clip, level * master_volume_ * effects_volume_, pitch);
}

}  // namespace sf::app
