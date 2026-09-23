#include "app/presentation/ui/mission_panel.hpp"

#include "app/presentation/format.hpp"

#include <algorithm>

namespace sf::app {
namespace {

constexpr Colour kLabel{0.541F, 0.576F, 0.620F};    // #8a939e
constexpr Colour kTitle{0.898F, 0.925F, 0.949F};    // #e5ecf2
constexpr Colour kTotal{0.949F, 0.749F, 0.404F};    // #f2bf67

TextLine row(const std::string& label, const std::string& value) {
    return TextLine{{fmt::rpad(label, 16), kLabel}, {"  " + value, palette::PRIMARY}};
}

TextLine plain(const std::string& text, Colour colour = palette::PRIMARY) { return TextLine{{text, colour}}; }

std::string short_label(const std::string& label) {
    const auto slash = label.find('/');
    if (slash == std::string::npos) {
        return label;
    }
    const auto next = label.find('/', slash + 1);
    return label.substr(slash + 1, next == std::string::npos ? std::string::npos : next - slash - 1);
}

}  // namespace

void MissionPanel::set_targets(const std::vector<std::string>& names, const std::string& current) {
    targets_ = names;
    const auto it = std::find(targets_.begin(), targets_.end(), current);
    target_index_ = it != targets_.end() ? static_cast<int>(it - targets_.begin()) : 0;
    altitude_index_ = default_altitude_for(current_target());
}

std::string MissionPanel::current_target() const {
    if (targets_.empty()) {
        return {};
    }
    return targets_[static_cast<std::size_t>(std::clamp(target_index_, 0, static_cast<int>(targets_.size()) - 1))];
}

void MissionPanel::step_target(int direction) {
    if (targets_.empty()) {
        return;
    }
    const int n = static_cast<int>(targets_.size());
    target_index_ = ((target_index_ + direction) % n + n) % n;
    altitude_index_ = default_altitude_for(current_target());
    if (on_target_changed) {
        on_target_changed(current_target());
    }
}

void MissionPanel::step_altitude(int direction) {
    const int n = static_cast<int>(ALTITUDES_KM.size());
    altitude_index_ = ((altitude_index_ + direction) % n + n) % n;
}

std::string MissionPanel::orbit_label() const { return fmt::format("%.0f km circular", current_altitude_km()); }

int MissionPanel::default_altitude_for(const std::string& target) const {
    // 100 km for the Moon, 500 km for everything else. Not a magic number per
    // body: the milestone's default (rule 57) with the exception the lunar
    // campaign qualified.
    const double wanted = target == "Moon" ? 100.0 : 500.0;
    const auto it = std::find(ALTITUDES_KM.begin(), ALTITUDES_KM.end(), wanted);
    return it != ALTITUDES_KM.end() ? static_cast<int>(it - ALTITUDES_KM.begin()) : 0;
}

void MissionPanel::request_plan() {
    if (searching_) {
        if (on_search_cancelled) {
            on_search_cancelled();
        }
        return;
    }
    plan_button_text_ = "CANCEL SEARCH";
    status_ = "starting the search…";
    status_colour_ = palette::SECONDARY;
    pending_plan_ = true;
    searching_ = true;
}

void MissionPanel::advance() {
    if (!pending_plan_) {
        return;
    }
    // The request was announced last frame and the screen already says so. Only
    // now does it go out. One frame is what separates "the game warned" from
    // "the game froze".
    pending_plan_ = false;
    if (on_plan_requested) {
        on_plan_requested(current_target(), current_altitude_km(), current_altitude_km());
    }
}

void MissionPanel::show_progress(const PlanningProgress& progress) {
    if (!progress.present) {
        return;
    }
    searching_ = true;
    plan_button_text_ = "CANCEL SEARCH";
    execute_enabled_ = false;
    const std::string stage = fmt::upper(progress.stage.empty() ? std::string{"searching"} : progress.stage);
    std::string line = fmt::format("SEARCHING TRAJECTORIES — %s\n  candidates tested %lld of %lld   flown %lld   found %lld",
                                   stage.c_str(), progress.candidates_screened, progress.candidates_considered,
                                   progress.candidates_flown, progress.candidates_succeeded);
    if (progress.integrator_steps > 0) {
        line += "   " + fmt::count(progress.integrator_steps) + " integrator steps";
    }
    if (progress.cancelled) {
        line = "CANCELLING — the search stops between candidates\n" + line;
    }
    status_colour_ = palette::SECONDARY;
    status_ = line;
}

void MissionPanel::show_plan(const PlanSummary& plan, const std::string& error) {
    searching_ = false;
    plan_button_text_ = "SEARCH";
    choices_.clear();

    if (!plan.valid) {
        summary_.clear();
        status_colour_ = palette::CRITICAL;
        status_ = "NO PLAN -- " + error;
        execute_enabled_ = false;
        cancel_enabled_ = false;
        return;
    }
    execute_enabled_ = !plan.armed;
    cancel_enabled_ = true;
    status_colour_ = palette::SECONDARY;
    status_ = plan.armed ? "EXECUTING -- the autopilot has the attitude" : "review, then EXECUTE to arm the burns";
    format(plan);
}

std::string MissionPanel::alternative_name(int index, int total) {
    // With two options, calling one "BALANCED" would invent a middle ground the
    // search did not find; with one, any label is a comparison with nothing.
    if (total >= 3) {
        static const char* names[] = {"FAST", "BALANCED", "LOW ΔV"};
        return names[index];
    }
    if (total == 2) {
        static const char* names[] = {"FASTER", "CHEAPER"};
        return names[index];
    }
    return "ONLY OPTION";
}

void MissionPanel::show_alternatives(const std::vector<PlanAlternative>& alternatives) {
    choices_.clear();
    if (alternatives.empty()) {
        return;
    }
    struct Indexed {
        const PlanAlternative* alternative;
        int source_index;
    };
    // The SOURCE index travels with the row: the table reorders by flight time,
    // and a button that sent its column position would choose another
    // trajectory.
    std::vector<Indexed> feasible;
    std::vector<Indexed> refused;
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        (alternatives[i].feasible ? feasible : refused).push_back(Indexed{&alternatives[i], static_cast<int>(i)});
    }

    summary_.push_back(plain(""));
    summary_.push_back(plain("TRAJECTORIES FOUND", kLabel));
    if (feasible.empty()) {
        summary_.push_back(plain("  none — every geometry the search flew was refused"));
    } else {
        // Sorted by flight time, so that the left column is always the fastest:
        // the reading rule 47 draws.
        std::stable_sort(feasible.begin(), feasible.end(), [](const Indexed& a, const Indexed& b) {
            return a.alternative->time_of_flight_s < b.alternative->time_of_flight_s;
        });
        const auto shown = std::vector<Indexed>(feasible.begin(),
                                                feasible.begin() + static_cast<long>(std::min<std::size_t>(3, feasible.size())));
        const int total = static_cast<int>(shown.size());
        const auto table_row = [&](const std::string& label, auto&& cell) {
            std::string line = "  " + fmt::rpad(label, 12) + " ";
            for (const auto& entry : shown) {
                line += fmt::rpad(cell(*entry.alternative), 16);
            }
            summary_.push_back(plain(line));
        };
        {
            std::string line = "  " + fmt::rpad("", 12) + " ";
            for (int i = 0; i < total; ++i) {
                line += fmt::rpad(alternative_name(i, total), 16);
            }
            summary_.push_back(plain(line));
        }
        table_row("FLIGHT", [](const PlanAlternative& a) { return fmt::duration(a.time_of_flight_s); });
        table_row("DEPART ΔV", [](const PlanAlternative& a) { return fmt::format("%.0f m/s", a.injection_delta_v); });
        table_row("CAPTURE ΔV", [](const PlanAlternative& a) { return fmt::format("%.0f m/s", a.capture_delta_v); });
        table_row("TOTAL ΔV", [](const PlanAlternative& a) { return fmt::format("%.0f m/s", a.total_delta_v); });
        table_row("ORBIT", [](const PlanAlternative& a) {
            return fmt::format("%.0f × %.0f km", a.predicted_periapsis_m / 1000.0, a.predicted_apoapsis_m / 1000.0);
        });
        table_row("INC", [](const PlanAlternative& a) { return fmt::format("%.1f°", a.predicted_inclination_deg); });

        // One button per column. With one option there is no choice to offer --
        // it already is the plan -- and the row stays empty rather than offering a
        // button that changes nothing.
        if (total >= 2) {
            for (int i = 0; i < total; ++i) {
                choices_.push_back(Choice{alternative_name(i, total), shown[static_cast<std::size_t>(i)].source_index});
            }
        }
    }

    if (!refused.empty()) {
        summary_.push_back(plain(""));
        summary_.push_back(plain(fmt::format("REFUSED (%zu)", refused.size()), kLabel));
        for (const auto& entry : refused) {
            summary_.push_back(plain("  " + fmt::rpad(short_label(entry.alternative->label), 22) + " " +
                                     (entry.alternative->failure.empty() ? std::string{"?"} : entry.alternative->failure)));
        }
    }
}

void MissionPanel::format(const PlanSummary& p) {
    summary_.clear();
    const double departure = p.seconds_to_ignition;
    const double arrival = p.seconds_to_insertion;
    summary_.push_back(plain(fmt::upper(p.origin.empty() ? "?" : p.origin) + " → " +
                                 fmt::upper(p.target.empty() ? "?" : p.target),
                             kTitle));
    summary_.push_back(plain(""));
    summary_.push_back(row("DEPARTURE", departure > 0.0 ? "in " + fmt::duration(departure) : "passed"));
    summary_.push_back(row("ARRIVAL", arrival > 0.0 ? "in " + fmt::duration(arrival) : "passed"));
    summary_.push_back(row("FLIGHT TIME", fmt::format("%.2f days   %s branch", p.time_of_flight_days,
                                                      p.branch.empty() ? "?" : p.branch.c_str())));
    summary_.push_back(row("TRANSFER ANGLE", fmt::format("%.1f°", p.transfer_angle_deg)));
    summary_.push_back(plain(""));
    summary_.push_back(
        row("INJECTION", fmt::format("%.1f m/s over ", p.injection_delta_v) + fmt::duration(p.injection_duration_s)));
    summary_.push_back(row("MIDCOURSE", fmt::format("%.1f m/s  (folded into the injection)", p.midcourse_delta_v)));
    summary_.push_back(
        row("CAPTURE", fmt::format("%.1f m/s over ", p.insertion_delta_v) + fmt::duration(p.insertion_duration_s)));
    {
        TextLine total = row("TOTAL ΔV", "");
        total.back().text = "  ";
        total.push_back(TextSpan{fmt::format("%.1f m/s", p.total_delta_v), kTotal});
        total.push_back(TextSpan{" of " + fmt::speed(p.delta_v_available) + " available", palette::PRIMARY});
        summary_.push_back(total);
    }
    summary_.push_back(plain(""));
    summary_.push_back(row("PROPELLANT", fmt::format("%.1f kg required, %.1f kg left after", p.propellant_required_kg,
                                                     p.propellant_remaining_kg)));
    summary_.push_back(plain(""));
    summary_.push_back(plain("PREDICTED ORBIT AT ARRIVAL", kLabel));
    summary_.push_back(row("  PE", fmt::distance(p.predicted_periapsis_m)));
    summary_.push_back(row("  AP", fmt::distance(p.predicted_apoapsis_m)));
    summary_.push_back(row("  ECC", fmt::format("%.5f", p.predicted_eccentricity)));
    summary_.push_back(row("  INC", fmt::format("%.2f°   RAAN %.1f°", p.predicted_inclination_deg,
                                                p.predicted_raan_deg)));
}

}  // namespace sf::app
