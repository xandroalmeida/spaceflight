#include "core/navigation/maneuver.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sf::navigation {
namespace {

std::string lowered(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

}  // namespace

std::string_view to_string(GuidanceMode mode) {
    switch (mode) {
        case GuidanceMode::Inertial:   return "INERTIAL";
        case GuidanceMode::Prograde:   return "PROGRADE";
        case GuidanceMode::Retrograde: return "RETROGRADE";
        case GuidanceMode::Normal:     return "NORMAL";
        case GuidanceMode::AntiNormal: return "ANTI_NORMAL";
        case GuidanceMode::RadialOut:  return "RADIAL_OUT";
        case GuidanceMode::RadialIn:   return "RADIAL_IN";
        case GuidanceMode::Hull:       return "HULL";
        case GuidanceMode::Rendezvous: return "RENDEZVOUS";
    }
    return "?";
}

std::optional<GuidanceMode> guidance_from_string(std::string_view name) {
    const std::string key = lowered(name);
    if (key == "inertial") return GuidanceMode::Inertial;
    if (key == "prograde") return GuidanceMode::Prograde;
    if (key == "retrograde") return GuidanceMode::Retrograde;
    if (key == "normal") return GuidanceMode::Normal;
    if (key == "anti_normal" || key == "antinormal") return GuidanceMode::AntiNormal;
    if (key == "radial_out" || key == "radialout") return GuidanceMode::RadialOut;
    if (key == "radial_in" || key == "radialin") return GuidanceMode::RadialIn;
    if (key == "hull") return GuidanceMode::Hull;
    if (key == "rendezvous") return GuidanceMode::Rendezvous;
    return std::nullopt;
}

void Maneuver::validate() const {
    if (!(throttle >= 0.0) || !(throttle <= 1.0)) {
        throw std::invalid_argument("maneuver \"" + name + "\": throttle must be in [0, 1]");
    }
    if (!(duration.seconds() > 0.0)) {
        throw std::invalid_argument("maneuver \"" + name + "\": duration must be > 0 s "
                                    "(an instantaneous burn is a planning fiction, not a maneuver "
                                    "-- docs/architecture/navigation.md section 2)");
    }
    if (!duration.is_finite() || !ignition.is_finite()) {
        throw std::invalid_argument("maneuver \"" + name + "\": non-finite epoch or duration");
    }
    if (guidance == GuidanceMode::Rendezvous && !(arrival > cutoff())) {
        throw std::invalid_argument("maneuver \"" + name +
                                    "\": RENDEZVOUS guidance needs its arrival after the cutoff "
                                    "(the law diverges as the time to go reaches zero)");
    }
    if (guidance == GuidanceMode::Inertial && inertial_direction.norm() <= 0.0) {
        throw std::invalid_argument("maneuver \"" + name +
                                    "\": INERTIAL guidance needs a non-zero direction");
    }
}

ManeuverPlan& ManeuverPlan::add(Maneuver maneuver) {
    maneuver.validate();

    for (const auto& existing : maneuvers_) {
        const bool overlaps =
            maneuver.ignition < existing.cutoff() && existing.ignition < maneuver.cutoff();
        if (overlaps) {
            throw std::invalid_argument("maneuver \"" + maneuver.name + "\" overlaps \"" +
                                        existing.name + "\"; one engine cannot burn in two "
                                        "directions at once");
        }
    }

    const auto position = std::lower_bound(
        maneuvers_.begin(), maneuvers_.end(), maneuver.ignition,
        [](const Maneuver& m, const time::CoordinateTime& t) { return m.ignition < t; });
    maneuvers_.insert(position, std::move(maneuver));
    return *this;
}

const Maneuver* ManeuverPlan::active_at(time::CoordinateTime t) const {
    for (const auto& maneuver : maneuvers_) {
        if (maneuver.active_at(t)) {
            return &maneuver;
        }
        if (maneuver.ignition > t) {
            break;  // sorted: nothing later can be active now
        }
    }
    return nullptr;
}

std::vector<time::CoordinateTime> ManeuverPlan::switch_times(time::CoordinateTime from,
                                                             time::CoordinateTime to) const {
    const auto lo = std::min(from, to);
    const auto hi = std::max(from, to);

    std::vector<time::CoordinateTime> times;
    for (const auto& maneuver : maneuvers_) {
        for (const auto t : {maneuver.ignition, maneuver.cutoff()}) {
            if (t > lo && t < hi) {
                times.push_back(t);
            }
        }
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    if (from > to) {
        std::reverse(times.begin(), times.end());  // follow the integration direction
    }
    return times;
}

time::Duration ManeuverPlan::total_burn_time() const {
    time::Duration total{};
    for (const auto& maneuver : maneuvers_) {
        total += maneuver.duration;
    }
    return total;
}

std::string ManeuverPlan::describe() const {
    if (maneuvers_.empty()) {
        return "(no maneuvers -- coasting)";
    }
    std::ostringstream os;
    os << std::setprecision(10);
    for (std::size_t i = 0; i < maneuvers_.size(); ++i) {
        const auto& m = maneuvers_[i];
        os << (i > 0 ? "\n" : "") << "  " << (i + 1) << ". " << m.name << ": ignition "
           << m.ignition.to_string() << ", " << m.duration.seconds() << " s at throttle "
           << m.throttle << ", " << to_string(m.guidance);
        if (m.guidance != GuidanceMode::Inertial) {
            os << " relative to " << m.reference.name();
        }
    }
    return os.str();
}

}  // namespace sf::navigation
