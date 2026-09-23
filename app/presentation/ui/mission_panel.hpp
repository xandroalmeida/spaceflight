#pragma once

// The mission computer (rules 25, 26, 27).
//
//     NAV -> choose target -> SEARCH -> summary -> EXECUTE | CANCEL
//
// Every number in the summary comes from plan(), which comes from the core's
// MissionMetrics. This panel renames fields and formats units; it does not
// compute delta-v, does not estimate flight time and has no opinion about the
// predicted orbit (rule 77).
//
// This is the panel's STATE; the application draws it (app/ui/panels.cpp) and
// the tests drive it without a window.

#include "app/presentation/palette.hpp"
#include "app/session/flight_session.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace sf::app {

struct TextSpan {
    std::string text;
    Colour colour{palette::PRIMARY};
};
using TextLine = std::vector<TextSpan>;

class MissionPanel {
public:
    // The altitudes the pilot may ask for, in km (rules 9 and 57). A list and not
    // a text field: the number has to be plausible for the chosen body, and a
    // free box invites asking for a 5 m orbit.
    static constexpr std::array<double, 7> ALTITUDES_KM = {50.0, 100.0, 200.0, 300.0, 500.0, 1000.0, 2000.0};

    struct Choice {
        std::string label;
        int source_index{0};
    };

    // Signals.
    std::function<void(const std::string& target, double periapsis_km, double apoapsis_km)> on_plan_requested;
    std::function<void()> on_execute;
    std::function<void()> on_cancel;
    std::function<void()> on_search_cancelled;
    std::function<void(int)> on_alternative_chosen;
    std::function<void(const std::string&)> on_target_changed;
    std::function<void()> on_closed;

    void set_targets(const std::vector<std::string>& names, const std::string& current);
    [[nodiscard]] std::string current_target() const;
    void step_target(int direction);
    void step_altitude(int direction);
    [[nodiscard]] double current_altitude_km() const {
        return ALTITUDES_KM[static_cast<std::size_t>(altitude_index_)];
    }
    [[nodiscard]] std::string orbit_label() const;

    // The SEARCH button. While searching it is CANCEL SEARCH.
    void request_plan();
    // Called every frame: a plan asked for last frame is emitted now, so the
    // screen shows "starting the search" before anything else happens.
    void advance();

    // Rule 48: the interface says what is happening while it happens. Counts,
    // not trajectories: a progress report that carried a plan would be a second
    // place plans come from.
    void show_progress(const PlanningProgress& progress);
    // Called after planning (or failing).
    void show_plan(const PlanSummary& plan, const std::string& error);
    // The geometries the search actually flew (rule 47), side by side, sorted by
    // flight time, three columns at most.
    void show_alternatives(const std::vector<PlanAlternative>& alternatives);

    [[nodiscard]] const std::vector<TextLine>& summary() const { return summary_; }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] Colour status_colour() const { return status_colour_; }
    [[nodiscard]] const std::string& plan_button_text() const { return plan_button_text_; }
    [[nodiscard]] bool execute_enabled() const { return execute_enabled_; }
    [[nodiscard]] bool cancel_enabled() const { return cancel_enabled_; }
    [[nodiscard]] const std::vector<Choice>& choices() const { return choices_; }
    [[nodiscard]] bool searching() const { return searching_; }
    [[nodiscard]] const std::vector<std::string>& targets() const { return targets_; }

    bool visible{false};

    // FAST / BALANCED / LOW DV, and only when there are three to name.
    [[nodiscard]] static std::string alternative_name(int index, int total);

private:
    [[nodiscard]] int default_altitude_for(const std::string& target) const;
    void format(const PlanSummary& plan);

    std::vector<std::string> targets_;
    int target_index_{0};
    int altitude_index_{1};
    std::vector<TextLine> summary_;
    std::string status_;
    Colour status_colour_{palette::WARNING};
    std::string plan_button_text_{"SEARCH"};
    bool execute_enabled_{false};
    bool cancel_enabled_{false};
    bool pending_plan_{false};
    bool searching_{false};
    std::vector<Choice> choices_;
};

}  // namespace sf::app
