#include "core/navigation/mission_execution.hpp"

#include "core/celestial/solar_system.hpp"

#include "core/coordinates/reference_frame.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace sf::navigation {

std::string_view to_string(MissionPhase phase) {
    switch (phase) {
        case MissionPhase::Idle:                 return "IDLE";
        case MissionPhase::Planned:              return "PLANNED";
        case MissionPhase::WaitingForDeparture:  return "WAITING_FOR_DEPARTURE";
        case MissionPhase::Orienting:            return "ORIENTING";
        case MissionPhase::InjectionBurn:        return "INJECTION_BURN";
        case MissionPhase::Coast:                return "COAST";
        case MissionPhase::MidcourseCorrection:  return "MIDCOURSE_CORRECTION";
        case MissionPhase::Approach:             return "APPROACH";
        case MissionPhase::CaptureOrienting:     return "CAPTURE_ORIENTING";
        case MissionPhase::CaptureBurn:          return "CAPTURE_BURN";
        case MissionPhase::OrbitInsertion:       return "ORBIT_INSERTION";
        case MissionPhase::Complete:             return "COMPLETE";
        case MissionPhase::Aborted:              return "ABORTED";
        case MissionPhase::Failed:               return "FAILED";
    }
    return "UNKNOWN";
}

namespace {

using math::Vec3;

const Maneuver* find(const ManeuverPlan& plan, std::string_view name) {
    for (const auto& maneuver : plan.maneuvers()) {
        if (maneuver.name == name) {
            return &maneuver;
        }
    }
    return nullptr;
}

// The destination's Hill radius, from the two gravitational parameters and the
// separation that actually holds right now.
//
//     r_H = d * (m / 3M)^(1/3)
//
// Computed rather than tabulated: the Moon's is 61 500 km, and writing that
// number down would put a Moon-shaped assumption into a class whose whole
// interface is body-agnostic.  The eccentricity of the destination's own orbit
// then shows up honestly, as a Hill radius that breathes by a few per cent over
// a month instead of a constant that does not.
double hill_radius(double gm_origin, double gm_destination, double separation) {
    if (!(gm_origin > 0.0) || !(gm_destination > 0.0) || !(separation > 0.0)) {
        return 0.0;
    }
    return separation * std::cbrt(gm_destination / (3.0 * gm_origin));
}

}  // namespace

void MissionExecution::arm(const MissionPlanResult& plan) {
    clear();
    if (!plan.ok()) {
        // Arming a failure is how a ship ends up flying a burn nobody planned.
        // The old GDExtension planner wrote its trial burns straight into the
        // ship's live plan and relied on the caller to clean up after a failure;
        // this refuses instead of relying.
        phase_ = MissionPhase::Idle;
        return;
    }
    plan_ = plan.maneuvers;
    predicted_ = plan.metrics;
    origin_ = plan.metrics.origin;
    destination_ = plan.metrics.destination;
    phase_ = MissionPhase::Planned;
}

void MissionExecution::abort() {
    plan_ = ManeuverPlan{};
    phase_ = MissionPhase::Aborted;
    has_settle_time_ = false;
}

void MissionExecution::clear() {
    plan_ = ManeuverPlan{};
    predicted_ = MissionMetrics{};
    outcome_ = MissionOutcome{};
    phase_ = MissionPhase::Idle;
    has_settle_time_ = false;
    mass_before_capture_ = 0.0;
    mass_after_capture_ = 0.0;
    closest_distance_ = 0.0;
    have_closest_ = false;
}

double MissionExecution::seconds_to_injection(time::CoordinateTime now) const {
    const auto* injection = find(plan_, "injection");
    return injection == nullptr ? 0.0 : (injection->ignition - now).seconds();
}

double MissionExecution::seconds_to_capture(time::CoordinateTime now) const {
    const auto* insertion = find(plan_, "insertion");
    return insertion == nullptr ? 0.0 : (insertion->ignition - now).seconds();
}

std::optional<attitude::PointingCommand> MissionExecution::pointing_command() const {
    const auto* injection = find(plan_, "injection");
    const auto* insertion = find(plan_, "insertion");

    switch (phase_) {
        case MissionPhase::Planned:
        case MissionPhase::WaitingForDeparture:
        case MissionPhase::Orienting:
        case MissionPhase::InjectionBurn: {
            if (injection == nullptr) {
                return std::nullopt;
            }
            // The injection direction is carried on the maneuver even when the
            // guidance mode is HULL -- maneuver_for_delta_v keeps it, and it is
            // the only record of which way the corrected departure points.
            attitude::PointingCommand command{};
            command.mode = GuidanceMode::Inertial;
            command.inertial_direction = injection->inertial_direction;
            return command;
        }

        case MissionPhase::Coast:
        case MissionPhase::MidcourseCorrection:
        case MissionPhase::Approach:
        case MissionPhase::CaptureOrienting:
        case MissionPhase::CaptureBurn:
        case MissionPhase::OrbitInsertion: {
            // While the TRIM burn is running the nose belongs to it and not to
            // the capture: the two point in different directions when the trim
            // is prograde, and pointing retrograde through a prograde burn would
            // fly the mission backwards.
            const auto* circularisation = find(plan_, "circularisation");
            if (phase_ == MissionPhase::OrbitInsertion && circularisation != nullptr) {
                attitude::PointingCommand command{};
                command.mode = circularisation->guidance == GuidanceMode::Hull
                                   ? GuidanceMode::Retrograde
                                   : circularisation->guidance;
                command.reference = circularisation->reference;
                return command;
            }
            if (insertion == nullptr) {
                return std::nullopt;
            }
            // Retrograde about the DESTINATION, from the injection's cutoff
            // onwards.  The days of coast in between are the slew, which is the
            // point: what the capture burn then measures is the tracking lag of
            // a settled controller, not a race to get pointed.
            attitude::PointingCommand command{};
            command.mode = insertion->guidance == GuidanceMode::Hull ? GuidanceMode::Retrograde
                                                                     : insertion->guidance;
            command.reference = insertion->reference;
            return command;
        }

        // Idle, aborted, complete, failed: the mission has no opinion and the
        // attitude belongs to whoever is flying.  Holding a command here would
        // mean a finished mission still steering the ship.
        case MissionPhase::Idle:
        case MissionPhase::Complete:
        case MissionPhase::Aborted:
        case MissionPhase::Failed:
            return std::nullopt;
    }
    return std::nullopt;
}

void MissionExecution::update(const ephemeris::EphemerisProvider& provider,
                              time::CoordinateTime now,
                              const propagation::PropagationState& state,
                              units::Angle pointing_error) {
    if (phase_ == MissionPhase::Idle || phase_ == MissionPhase::Aborted) {
        return;
    }
    // Terminal: a finished mission does not un-finish because the ship drifted.
    if (phase_ == MissionPhase::Complete || phase_ == MissionPhase::Failed) {
        return;
    }

    const auto* injection = find(plan_, "injection");
    const auto* midcourse = find(plan_, "midcourse");
    const auto* insertion = find(plan_, "insertion");
    // The second capture burn, when the plan has one (Milestone 8). Named rather
    // than inferred: the pilot is told which burn is running, and "CAPTURE BURN"
    // while the trim is firing would be the wrong one.
    const auto* circularisation = find(plan_, "circularisation");

    // A burn that is running outranks every geometric test: whatever else is
    // true, the engine is on and the pilot needs to be told which burn it is.
    if (injection != nullptr && injection->active_at(now)) {
        phase_ = MissionPhase::InjectionBurn;
        return;
    }
    if (midcourse != nullptr && midcourse->active_at(now)) {
        phase_ = MissionPhase::MidcourseCorrection;
        return;
    }
    if (insertion != nullptr && insertion->active_at(now)) {
        phase_ = MissionPhase::CaptureBurn;
        return;
    }
    if (circularisation != nullptr && circularisation->active_at(now)) {
        phase_ = MissionPhase::OrbitInsertion;
        return;
    }

    // ---- before the injection ---------------------------------------------
    if (injection != nullptr && now < injection->ignition) {
        const auto lead = (injection->ignition - now);
        const bool getting_ready = lead <= config_.orienting_lead;
        const bool pointed = pointing_error.radians() <= config_.pointing_settled.radians();
        phase_ = (getting_ready && !pointed) ? MissionPhase::Orienting
                                             : MissionPhase::WaitingForDeparture;
        return;
    }

    // ---- after the injection ----------------------------------------------
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();
    const auto destination_state = provider.state(destination_, now, frame);
    const double gm_destination = provider.gravitational_parameter(destination_);
    const double radius_destination = provider.mean_radius(destination_);

    // Whose gravity the destination's own neighbourhood is measured against: the
    // body it ORBITS, read from the directory.
    //
    // ⚠️ This used to be the mission's origin, which is right for the Moon --
    // the Moon orbits the Earth -- and nonsense for Mars. A Hill radius computed
    // from Mars against the Earth asks how big Mars's neighbourhood is inside a
    // gravity well Mars is not in, and since Mars is ten times the Earth's mass
    // it comes out larger than the separation itself: the ship would be reported
    // as "on approach" from the moment it left Earth orbit.
    //
    // `common_primary` is the SAME rule the planner's geometry uses, so the two
    // cannot disagree about what orbits what.
    const auto primary = celestial::common_primary(origin_, destination_)
                             .value_or(origin_);
    const auto primary_state = provider.state(primary, now, frame);
    const double gm_primary = provider.gravitational_parameter(primary);

    const Vec3 to_destination = state.state.position - destination_state.state.position;
    const double distance = to_destination.norm();
    const double separation =
        (destination_state.state.position - primary_state.state.position).norm();
    const double hill =
        hill_radius(gm_primary, gm_destination, separation) * config_.approach_hill_fraction;

    const bool inside_sphere = hill > 0.0 && distance < hill;

    // After the capture burn: is it an orbit yet, and is it the right one?
    const Maneuver* last_capture_burn =
        circularisation != nullptr ? circularisation : insertion;
    if (last_capture_burn != nullptr && now >= last_capture_burn->cutoff()) {
        if (!(mass_after_capture_ > 0.0)) {
            mass_after_capture_ = state.mass;
        }
        coordinates::StateVector relative{};
        relative.position = to_destination;
        relative.velocity = state.state.velocity - destination_state.state.velocity;
        const auto elements = trajectory::elements_from_state(relative, gm_destination);

        if (!elements.bound) {
            // The burn happened and the orbit did not close.  That is a failure
            // of the MISSION, and it is reported as one rather than as a phase
            // that never advances.
            phase_ = MissionPhase::Failed;
            outcome_.note = "the capture burn finished and the orbit is still hyperbolic";
            record_outcome(provider, now, state);
            return;
        }

        if (!has_settle_time_) {
            // One revolution, so that what is recorded is an ORBIT and not the
            // instant the engine stopped.  The planner reads it the same way.
            settle_at_ = last_capture_burn->cutoff() +
                         time::Duration::seconds(elements.period *
                                                 config_.orbit_settling_revolutions);
            has_settle_time_ = true;
        }

        if (now < settle_at_) {
            phase_ = MissionPhase::OrbitInsertion;
            return;
        }

        record_outcome(provider, now, state);
        // In spec or not, said plainly -- and measured against what was ASKED
        // for, not against what the planner predicted.
        //
        // Against the prediction it would say whether the flight went as
        // planned, which is a different and weaker question: a planner that
        // confidently predicts 300 km when 100 was requested would pass it.  The
        // prediction-versus-flight comparison is what MissionOutcome is for, and
        // it is recorded either way, just above.
        //
        // +-20 km is the campaign's band (80-120 km around a 100 km request),
        // carried across so that the cockpit and the validation suite call the
        // same orbits acceptable.
        const double periapsis_altitude = elements.periapsis_radius - radius_destination;
        const double apoapsis_altitude = elements.apoapsis_radius - radius_destination;
        const double wanted_periapsis = predicted_.requested_periapsis_altitude;
        const double wanted_apoapsis = predicted_.requested_apoapsis_altitude;
        const bool in_spec = std::abs(periapsis_altitude - wanted_periapsis) < 20.0e3 &&
                             std::abs(apoapsis_altitude - wanted_apoapsis) < 20.0e3;
        phase_ = in_spec ? MissionPhase::Complete : MissionPhase::Failed;
        if (!in_spec) {
            std::ostringstream os;
            os << std::fixed << std::setprecision(1) << "captured into "
               << periapsis_altitude / 1000.0 << " x " << apoapsis_altitude / 1000.0
               << " km against a plan of " << wanted_periapsis / 1000.0 << " x "
               << wanted_apoapsis / 1000.0 << " km";
            outcome_.note = os.str();
        }
        return;
    }

    // Between the injection and the capture.
    //
    // The running minimum distance is what "arrival, as flown" means: the plan
    // predicts a closest approach and the flight has its own, and the difference
    // between them is one of section 17's rows.
    if (!have_closest_ || distance < closest_distance_) {
        closest_distance_ = distance;
        closest_at_ = now;
        have_closest_ = true;
    }
    // The mass the burn will start with.  Taken while still coasting, where mass
    // is constant, so it is exact however coarsely the caller samples.
    if (insertion != nullptr && now < insertion->ignition) {
        mass_before_capture_ = state.mass;
    }

    if (insertion != nullptr) {
        const auto lead = insertion->ignition - now;
        const bool getting_ready =
            lead.seconds() > 0.0 && lead <= config_.orienting_lead;
        const bool pointed = pointing_error.radians() <= config_.pointing_settled.radians();
        if (getting_ready && !pointed) {
            phase_ = MissionPhase::CaptureOrienting;
            return;
        }
    }
    phase_ = inside_sphere ? MissionPhase::Approach : MissionPhase::Coast;
}

void MissionExecution::record_outcome(const ephemeris::EphemerisProvider& provider,
                                      time::CoordinateTime now,
                                      const propagation::PropagationState& state) {
    if (outcome_.recorded) {
        return;
    }
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();
    const auto destination_state = provider.state(destination_, now, frame);
    const double gm = provider.gravitational_parameter(destination_);
    const double radius = provider.mean_radius(destination_);

    coordinates::StateVector relative{};
    relative.position = state.state.position - destination_state.state.position;
    relative.velocity = state.state.velocity - destination_state.state.velocity;
    const auto elements = trajectory::elements_from_state(relative, gm);

    const auto row = [](double predicted, double actual) {
        PredictedVersusActual value{};
        value.predicted = predicted;
        value.actual = actual;
        value.recorded = true;
        return value;
    };

    outcome_.periapsis_altitude =
        row(predicted_.predicted_periapsis_altitude, elements.periapsis_radius - radius);
    outcome_.apoapsis_altitude =
        row(predicted_.predicted_apoapsis_altitude,
            std::isfinite(elements.apoapsis_radius)
                ? elements.apoapsis_radius - radius
                : std::numeric_limits<double>::infinity());
    outcome_.eccentricity = row(predicted_.predicted_eccentricity, elements.eccentricity);
    outcome_.inclination_deg =
        row(predicted_.predicted_inclination.degrees(), elements.inclination.degrees());
    // Propellant: what the plan said the whole mission would cost against what
    // the tank actually lost.  The second number includes every gram the RCS
    // spent holding the nose, which the first one also does -- the planner flies
    // the autopilot when the autopilot is what was requested.
    outcome_.propellant_used =
        row(predicted_.propellant_required,
            predicted_.mass_at_departure > 0.0 ? predicted_.mass_at_departure - state.mass
                                               : 0.0);
    // Arrival: closest approach predicted, against closest approach flown.  NOT
    // against "the instant this outcome was recorded", which is a revolution
    // after the capture burn and would report a two-hour discrepancy that is
    // entirely an artefact of when the measurement was taken.
    if (have_closest_) {
        outcome_.arrival_tdb_s = row(predicted_.arrival.seconds_since_j2000(),
                                     closest_at_.seconds_since_j2000());
    }

    // What the capture burn actually bought, by Tsiolkovsky on the mass it
    // consumed.  Recorded only when both ends of the burn were observed: a
    // mission armed after the burn, or one whose phase was never updated during
    // it, has no honest answer and says so instead of printing the plan's number
    // back as though it had been measured.
    if (vehicle_ != nullptr && mass_before_capture_ > 0.0 && mass_after_capture_ > 0.0 &&
        mass_after_capture_ < mass_before_capture_) {
        const double exhaust = vehicle_->engine().effective_exhaust_velocity();
        outcome_.capture_delta_v =
            row(predicted_.capture_delta_v,
                exhaust * std::log(mass_before_capture_ / mass_after_capture_));
    }
    outcome_.recorded = true;
}

std::string MissionExecution::describe() const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "phase             : " << to_string(phase_) << "\n";
    if (!plan_.empty()) {
        os << plan_.describe() << "\n";
    }
    if (outcome_.recorded) {
        const auto line = [&os](const char* label, const PredictedVersusActual& value,
                                const char* unit, double scale) {
            if (!value.recorded) {
                return;
            }
            os << "  " << std::left << std::setw(22) << label << std::right << std::setw(14)
               << value.predicted * scale << std::setw(14) << value.actual * scale
               << std::setw(14) << value.difference() * scale << "  " << unit << "\n";
        };
        os << "predicted vs actual\n"
           << "  " << std::left << std::setw(22) << "" << std::right << std::setw(14)
           << "predicted" << std::setw(14) << "actual" << std::setw(14) << "difference"
           << "\n";
        line("periapsis altitude", outcome_.periapsis_altitude, "km", 1.0e-3);
        line("apoapsis altitude", outcome_.apoapsis_altitude, "km", 1.0e-3);
        line("eccentricity", outcome_.eccentricity, "", 1.0);
        line("inclination", outcome_.inclination_deg, "deg", 1.0);
        line("propellant used", outcome_.propellant_used, "kg", 1.0);
        line("capture delta-v", outcome_.capture_delta_v, "m/s", 1.0);
        line("arrival", outcome_.arrival_tdb_s, "s (TDB)", 1.0);
        if (!outcome_.note.empty()) {
            os << "  note: " << outcome_.note << "\n";
        }
    }
    return os.str();
}

}  // namespace sf::navigation
