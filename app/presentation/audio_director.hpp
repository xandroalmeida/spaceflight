#pragma once

// The ship's sound (rules 36, 37).
//
// ## The rule this file exists to respect
//
// In vacuum there is no sound. What there is is STRUCTURE: the engine and the
// thrusters are bolted to the same hull the pilot sits in, and the hull
// conducts. So the engine is heard from inside and NOT from outside -- and that
// is why `set_interior(false)` silences everything that belongs to the ship and
// leaves only the interface.
//
// No sound crosses space here. No explosion, no fly-by, no other ship's engine.
//
// ## The engine's volume follows the THRUST
//
// For the same reason as the plume: with the tank empty the key keeps working
// and the thrust is zero. A sound that followed the throttle would keep roaring
// over an engine that is out.

#include <string>

namespace sf::app {

// Where the sound goes. The application implements it over SDL audio; the tests
// and the headless run use a sink that plays nothing.
class AudioSink {
public:
    virtual ~AudioSink() = default;
    // A looping clip: its level (linear, 0..1) and pitch are updated every frame.
    virtual void set_loop(const std::string& clip, double level, double pitch) = 0;
    // A one-shot clip.
    virtual void play(const std::string& clip, double level, double pitch) = 0;
};

class SilentAudio final : public AudioSink {
public:
    void set_loop(const std::string&, double, double) override {}
    void play(const std::string&, double, double) override {}
};

class AudioDirector {
public:
    static constexpr double ENGINE_REFERENCE_N = 200000.0;

    explicit AudioDirector(AudioSink& sink) : sink_(&sink) {}

    void advance(double delta);
    // `thrust_n` is the core's real thrust.
    void set_engine_thrust(double thrust_n);
    void rcs_fired(int count);
    void switch_flipped() { play("switch", 0.5, 1.0); }
    void button_pressed() { play("button", 0.45, 1.0); }
    void warning() { play("warning", 0.55, 1.0); }
    void notify() { play("notify", 0.5, 1.0); }
    // Rule 36: in the external camera, by default, there is no sound carried
    // through space. The interface still sounds, because it is not in space -- it
    // is on the monitor of whoever is playing.
    void set_interior(bool inside);
    void set_volumes(double master, double effects);

    [[nodiscard]] double master_volume() const { return master_volume_; }
    [[nodiscard]] double effects_volume() const { return effects_volume_; }

private:
    void play(const std::string& clip, double level, double pitch);
    void apply_volumes();

    AudioSink* sink_;
    double master_volume_{0.8};
    double effects_volume_{0.8};
    bool interior_{true};
    double engine_level_{0.0};
    double rcs_cooldown_{0.0};
};

}  // namespace sf::app
