#pragma once

// The demonstration, photographed (rule 60).
//
// Flies the whole demonstration by itself -- Earth orbit, external view, engine,
// RCS, target selection, plan, execution, arrival, orbit -- and saves an image
// at each point. It is the milestone's evidence, and it is REPRODUCIBLE: the same
// command produces the same sequence, because every step is a condition on the
// simulation's state and not a number of frames chosen by hand.
//
// ⚠️ The images are EVIDENCE, not an oracle. They prove that the scene puts
// something on the screen at the right moments; whether it looks good is human
// judgement and this file has no opinion (rule 60).

#include "app/presentation/geometry.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sf::app {

class FlightApp;

class ShotDirector {
public:
    enum class Script { M7, M8 };

    // How many frames to wait AFTER the condition holds, before photographing:
    // between changing a command and it being drawn there is the simulation
    // itself, the instruments' update and the drawing.
    static constexpr int SETTLE_FRAMES = 3;

    // `stop_after` > 0 truncates the script: iterating on a picture must not
    // require flying to the Moon first.
    ShotDirector(FlightApp& flight, std::string directory, Script script, int stop_after = 0);

    void step(double delta);
    [[nodiscard]] bool finished() const { return step_ >= steps_.size(); }

private:
    struct Step {
        std::string name;
        std::function<void()> setup;
        std::optional<int> frames;
        std::function<bool()> until;
        int limit{1200};
        std::string shot;
        std::function<std::map<std::string, Vec3>()> anchors;
    };

    [[nodiscard]] std::vector<Step> m7_script();
    [[nodiscard]] std::vector<Step> m8_script();
    void shoot(const Step& step);
    void write_anchors(const std::string& name, const std::map<std::string, Vec3>& anchors) const;
    void frame_sunlit(double elevation_bias);
    void set_warp(int index);

    [[nodiscard]] bool pointed() const;
    [[nodiscard]] bool engine_running() const;
    [[nodiscard]] bool rcs_firing() const;
    [[nodiscard]] bool plan_ready() const;
    [[nodiscard]] bool within_approach() const;
    [[nodiscard]] bool close_to_target() const;
    [[nodiscard]] bool settled_in_orbit() const;
    [[nodiscard]] bool over_sunlit_target() const;
    [[nodiscard]] bool mission_complete() const;
    [[nodiscard]] bool injection_burning() const;
    [[nodiscard]] bool heliocentric() const;
    [[nodiscard]] bool closer_than(double metres) const;
    [[nodiscard]] std::map<std::string, Vec3> ship_anchors() const;
    [[nodiscard]] std::map<std::string, Vec3> lit_thruster_anchor() const;

    FlightApp& flight_;
    std::string directory_;
    std::vector<Step> steps_;
    std::size_t step_{0};
    int settle_{0};
    int hold_{0};
    bool armed_{false};
};

}  // namespace sf::app
