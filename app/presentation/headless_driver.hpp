#pragma once

// The headless verification, preserved from Milestone 5 (rule 62).
//
// Without a display nobody can press a key, so the headless run flies itself:
// points, burns, raises the warp, prints. That is how the project is checked on a
// machine with no screen, and it is deliberately driven by the COMPLETE
// read-out, whatever the HUD mode -- the verification cannot depend on which way
// an interface button is pointing.
//
// The printing cadence is in FRAMES and not seconds. The Milestone 2 version
// printed every wall-clock second, which made the verification depend on the
// speed of the machine.

#include <string>
#include <vector>

namespace sf::app {

class FlightApp;

class HeadlessDriver {
public:
    static constexpr int PRINT_EVERY_FRAMES = 150;

    // `destination` empty: the slew-and-burn check. Otherwise the whole mission
    // to that body -- the Moon by default, because it is the qualified case;
    // "Mars" is how Milestone 8 is checked end to end without a screen.
    HeadlessDriver(FlightApp& flight, std::string destination);

    void drive(const std::vector<std::string>& readout);

    [[nodiscard]] bool arrived() const { return mission_reported_; }

private:
    void warp_schedule(double elapsed);
    void fly_the_mission(double elapsed, const std::vector<std::string>& readout);
    void print_readout(const std::vector<std::string>& readout) const;

    FlightApp& flight_;
    bool mission_mode_{false};
    std::string destination_{"Moon"};
    int frames_{0};
    bool slew_commanded_{false};
    bool burn_commanded_{false};
    bool cruise_commanded_{false};
    bool mission_commanded_{false};
    bool mission_armed_{false};
    bool mission_reported_{false};
};

}  // namespace sf::app
