#include "core/navigation/direct_transfer.hpp"

#include "core/celestial/solar_system.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/gravity/composite_force_model.hpp"
#include "core/gravity/oblateness_gravity.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/navigation/maneuver_executor.hpp"
#include "core/navigation/mission.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace sf::navigation {

using math::Vec3;

namespace {

const auto kFrame = coordinates::ReferenceFrame::ssb_j2000();

// Section 4: the law's demand at ignition is set to this fraction of what the
// engine gives the full ship. The rest is margin for gravity and for the
// velocity the ship starts with.
constexpr double kDemandFraction = 0.75;
// Section 5: how far from the origin's surface the straight line to the
// destination has to pass, and how many departure points along one orbit are
// looked at.
constexpr double kOriginClearance = 300.0e3;   // [m]
constexpr int kDepartureSamples = 72;
constexpr int kFlightAttempts = 4;
// The earliest departure: long enough for the pilot to read the plan and arm it.
constexpr double kLeadSeconds = 120.0;
// Section 6: the engine stops this long before the arrival, the law's
// singularity kept out of the flight. With the terminal acceleration driven to
// zero the last second is worth well under 1 m/s.
[[nodiscard]] double cutoff_margin(double time_of_flight) {
    return std::min(1.0, 0.001 * time_of_flight);
}
// Peak speed the choice of T may imply, as a fraction of c: the ship must not be
// asked to outrun what the law's Newtonian estimate of its speed assumes.
constexpr double kPeakBetaLimit = 0.9;

std::unique_ptr<gravity::CompositeForceModel> build_forces(const SimulationState& state,
                                                           const gravity::ForceModel* thrust) {
    auto model = std::make_unique<gravity::CompositeForceModel>();
    model->add(std::make_unique<gravity::PointMassGravity>(*state.provider, state.catalog, kFrame));
    if (state.orientation != nullptr) {
        for (const auto body : state.j2_bodies) {
            model->add(gravity::OblatenessGravity::for_body(*state.provider, *state.orientation, body, kFrame));
        }
    }
    if (thrust != nullptr) {
        model->add_reference(*thrust);
    }
    return model;
}

propagation::IntegratorConfig integrator_for(const SimulationState& state, const spacecraft::Spacecraft& craft) {
    // Special relativity, whatever the request carried: this transfer is the one
    // that can go fast, and it is flown the way the game flies it
    // (docs/physics/relativistic-propulsion.md section 9).
    auto config = state.integrator;
    config.kinematics = propagation::Kinematics::SpecialRelativistic;
    config.allow_gravity_with_relativistic_kinematics = true;
    config.stop_inside_body = true;
    config.minimum_mass = craft.dry_mass();
    return config;
}

double point_to_segment(const Vec3& point, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const double length2 = ab.norm_squared();
    const double t = length2 > 0.0 ? std::clamp(dot(point - a, ab) / length2, 0.0, 1.0) : 0.0;
    return (a + ab * t - point).norm();
}

// One direct-transfer maneuver pair for a departure state and a flight time.
struct Design {
    time::CoordinateTime departure{};
    time::CoordinateTime arrival{};
    double time_of_flight{0.0};
    DirectArrival geometry{};
    Maneuver injection{};
    Maneuver insertion{};
};

Design design_for(const ephemeris::EphemerisProvider& provider, celestial::BodyId destination,
                  const propagation::PropagationState& departure_state, time::CoordinateTime departure,
                  double time_of_flight, double altitude) {
    Design d{};
    d.departure = departure;
    d.time_of_flight = time_of_flight;
    d.arrival = departure + time::Duration::seconds(time_of_flight);
    d.geometry = direct_arrival_geometry(provider, destination, d.arrival, departure_state.state.position, altitude);

    Maneuver leg{};
    leg.guidance = GuidanceMode::Rendezvous;
    leg.reference = destination;
    leg.throttle = 1.0;
    leg.arrival = d.arrival;
    leg.arrival_offset = d.geometry.offset;
    leg.arrival_velocity = d.geometry.velocity;

    // One guided burn, labelled in two parts (section 7): the part that
    // accelerates is the injection, the part that brakes is the capture. With
    // a(t_f) = 0 the thrust turns round a THIRD of the way, not half-way.
    const double half = time_of_flight / 3.0;
    const double burning = time_of_flight - cutoff_margin(time_of_flight);
    d.injection = leg;
    d.injection.name = "injection";
    d.injection.ignition = departure;
    d.injection.duration = time::Duration::seconds(half);
    d.insertion = leg;
    d.insertion.name = "insertion";
    d.insertion.ignition = departure + time::Duration::seconds(half);
    d.insertion.duration = time::Duration::seconds(burning - half);
    return d;
}

// Section 4: the flight time that puts the law's demand at ignition at
// kDemandFraction of the engine, by bisection in log T.
double choose_time_of_flight(const ephemeris::EphemerisProvider& provider, const spacecraft::Spacecraft& craft,
                             celestial::BodyId destination, const propagation::PropagationState& departure_state,
                             time::CoordinateTime departure, double altitude) {
    const double available = craft.engine().max_thrust() / departure_state.mass;
    const ManeuverPlan empty{};
    const ManeuverExecutor law{provider, craft, empty, kFrame};

    const auto too_short = [&](double time_of_flight) {
        const auto d = design_for(provider, destination, departure_state, departure, time_of_flight, altitude);
        const double demand = law.rendezvous_acceleration(departure_state, departure, d.injection).norm();
        const auto target = provider.state(destination, d.arrival, kFrame);
        const double distance = (target.state.position + d.geometry.offset - departure_state.state.position).norm();
        const double peak_speed = 1.5 * distance / time_of_flight;
        return demand > kDemandFraction * available || peak_speed > kPeakBetaLimit * units::c;
    };

    double low = std::log(60.0);
    double high = std::log(20.0 * 365.25 * 86400.0);
    if (!too_short(std::exp(low))) {
        return std::exp(low);
    }
    if (too_short(std::exp(high))) {
        return std::exp(high);
    }
    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (low + high);
        (too_short(std::exp(mid)) ? low : high) = mid;
    }
    return std::exp(high);
}

struct Flight {
    bool ok{false};
    propagation::PropagationStatus status{propagation::PropagationStatus::Success};
    std::string message;
    propagation::PropagationState at_departure{};
    propagation::PropagationState at_midpoint{};
    propagation::PropagationState at_cutoff{};
    propagation::PropagationState at_arrival{};
    propagation::PropagationState settled{};
    time::CoordinateTime settled_at{};
    double peak_speed{0.0};
    TrajectoryPrediction trajectory{};
};

Flight fly(const SimulationState& state, const MissionRequest& request, const spacecraft::Spacecraft& craft,
           const propagation::PropagationState& initial, const Design& design, double settle_seconds) {
    Flight out{};
    ManeuverPlan plan{};
    plan.add(design.injection);
    plan.add(design.insertion);
    ManeuverExecutor executor{*state.provider, craft, plan, kFrame};
    executor.set_kinematics(propagation::Kinematics::SpecialRelativistic);
    auto forces = build_forces(state, &executor);
    propagation::DormandPrince54Propagator propagator{*forces, integrator_for(state, craft)};

    const auto cutoff = design.insertion.cutoff();
    const auto end = design.arrival + time::Duration::seconds(settle_seconds);

    // The flight in pieces: the named instants, and enough samples in between
    // to track the peak speed and, when asked, draw the trajectory.
    std::vector<time::CoordinateTime> marks{design.departure, design.insertion.ignition, cutoff, design.arrival, end};
    const int samples = std::max(request.want_trajectory ? request.trajectory_samples : 0, 200);
    for (int i = 1; i < samples; ++i) {
        marks.push_back(state.epoch + (end - state.epoch) * (static_cast<double>(i) / samples));
    }
    std::sort(marks.begin(), marks.end());
    marks.erase(std::unique(marks.begin(), marks.end()), marks.end());

    auto current = initial;
    auto now = state.epoch;
    for (const auto mark : marks) {
        if (!(mark > now)) {
            continue;
        }
        const auto leg = run_mission(propagator, executor, current, now, mark);
        if (!leg.ok()) {
            out.status = leg.status;
            out.message = leg.message;
            return out;
        }
        current = leg.state;
        now = mark;

        const auto origin = state.provider->state(request.origin, now, kFrame);
        const auto destination = state.provider->state(request.destination, now, kFrame);
        out.peak_speed = std::max(out.peak_speed, (current.state.velocity - origin.state.velocity).norm());
        if (request.want_trajectory) {
            out.trajectory.samples.push_back(TrajectoryPrediction::Sample{
                now, current.state.position - origin.state.position,
                current.state.position - destination.state.position});
        }
        if (now == design.departure) out.at_departure = current;
        if (now == design.insertion.ignition) out.at_midpoint = current;
        if (now == cutoff) out.at_cutoff = current;
        if (now == design.arrival) out.at_arrival = current;
    }
    out.settled = current;
    out.settled_at = now;
    out.ok = true;
    return out;
}

}  // namespace

DirectArrival direct_arrival_geometry(const ephemeris::EphemerisProvider& provider, celestial::BodyId destination,
                                      time::CoordinateTime arrival, const Vec3& ship_position, double altitude) {
    DirectArrival out{};
    const auto target = provider.state(destination, arrival, kFrame);
    const double gm = provider.gravitational_parameter(destination);
    out.radius = provider.mean_radius(destination) + altitude;

    // The near side: the arrival point faces where the ship comes from, so the
    // approach does not pass through the destination.
    Vec3 towards_ship = ship_position - target.state.position;
    if (!(towards_ship.norm() > 0.0)) {
        towards_ship = Vec3::unit_x();
    }
    const Vec3 n = towards_ship.normalized();
    out.offset = n * out.radius;

    // The destination's neighbourhood, measured against what it orbits.
    const auto* entry = celestial::find_entry(destination);
    Vec3 orbit_normal = Vec3::unit_z();
    if (entry != nullptr) {
        const auto parent = provider.state(entry->parent, arrival, kFrame);
        const double gm_parent = provider.gravitational_parameter(entry->parent);
        const Vec3 relative_position = target.state.position - parent.state.position;
        const Vec3 relative_velocity = target.state.velocity - parent.state.velocity;
        if (gm_parent > 0.0 && gm > 0.0) {
            out.hill_radius = relative_position.norm() * std::cbrt(gm / (3.0 * gm_parent));
        }
        if (cross(relative_position, relative_velocity).norm() > 0.0) {
            orbit_normal = cross(relative_position, relative_velocity).normalized();
        }
    }
    out.station = out.hill_radius > 0.0 && out.radius > 0.5 * out.hill_radius;
    if (out.station || !(gm > 0.0)) {
        out.velocity = Vec3{};
        return out;
    }

    // Prograde about the destination's own orbit, in the plane that holds the
    // arrival direction.
    Vec3 along = cross(orbit_normal, n);
    if (along.norm() < 1.0e-6) {
        along = cross(Vec3::unit_x(), n);
    }
    out.velocity = along.normalized() * std::sqrt(gm / out.radius);
    return out;
}

MissionPlanResult plan_direct_mission(const SimulationState& state, const MissionRequest& request) {
    MissionPlanResult result{};
    const auto* craft = request.spacecraft.vehicle;
    if (state.provider == nullptr || craft == nullptr) {
        result.status = MissionPlanStatus::Invalid;
        result.failure = FailureReason{TransferFailure::NoFeasibleTrajectory, "no ephemeris or no spacecraft"};
        return result;
    }
    const auto& provider = *state.provider;
    const double altitude = request.target_orbit.periapsis_altitude;
    const auto cancelled = [&] { return request.effort.cancelled && request.effort.cancelled(); };
    SearchProgress progress{};
    progress.stage = "direct";
    const auto report = [&] {
        if (request.effort.on_progress) {
            request.effort.on_progress(progress);
        }
    };

    // The ship, in the integration frame.
    propagation::PropagationState initial{};
    {
        const auto origin = provider.state(request.origin, state.epoch, kFrame);
        initial.state.position = origin.state.position + state.vehicle.position;
        initial.state.velocity = origin.state.velocity + state.vehicle.velocity;
        initial.mass = state.mass > 0.0 ? state.mass : craft->initial_mass();
    }
    if (!craft->has_propellant(initial.mass)) {
        result.status = MissionPlanStatus::InsufficientPropellant;
        result.failure = FailureReason{TransferFailure::InsufficientDepartureDeltaV, "the tank is empty"};
        return result;
    }

    // Section 5: departure points along one orbit, coasting in the full model.
    std::vector<std::pair<time::CoordinateTime, propagation::PropagationState>> departures;
    {
        const double gm_origin = provider.gravitational_parameter(request.origin);
        const auto elements = trajectory::elements_from_state(state.vehicle, gm_origin);
        const bool orbiting = elements.bound && elements.period > 0.0 && elements.period < 2.0 * 86400.0;
        const int count = orbiting ? kDepartureSamples : 1;
        const double spacing = orbiting ? elements.period / count : 0.0;

        auto coast_forces = build_forces(state, nullptr);
        propagation::DormandPrince54Propagator coast{*coast_forces, integrator_for(state, *craft)};
        auto current = initial;
        auto now = state.epoch;
        for (int i = 0; i < count; ++i) {
            const auto at = state.epoch + time::Duration::seconds(kLeadSeconds + spacing * i);
            const auto leg = coast.propagate(current, now, at);
            if (!leg.ok()) {
                break;
            }
            current = leg.state;
            now = at;
            departures.emplace_back(at, current);
        }
    }

    const auto origin_radius = provider.mean_radius(request.origin);
    int attempts = 0;
    std::string last_problem = "no departure point within one orbit has a clear line to the destination";
    for (const auto& [departure, departure_state] : departures) {
        if (cancelled()) {
            result.status = MissionPlanStatus::Cancelled;
            return result;
        }
        ++progress.candidates_considered;
        const double time_of_flight =
            choose_time_of_flight(provider, *craft, request.destination, departure_state, departure, altitude);
        const auto design = design_for(provider, request.destination, departure_state, departure, time_of_flight,
                                       altitude);

        // The straight line to the arrival point has to clear the origin.
        const auto origin = provider.state(request.origin, departure, kFrame);
        const auto target = provider.state(request.destination, design.arrival, kFrame);
        const double clearance = point_to_segment(origin.state.position, departure_state.state.position,
                                                  target.state.position + design.geometry.offset);
        if (clearance < origin_radius + kOriginClearance) {
            report();
            continue;
        }
        ++progress.candidates_screened;
        if (++attempts > kFlightAttempts) {
            break;
        }

        // One revolution of the final orbit to read it as an orbit; ten minutes
        // at a station point, which has no revolution.
        const double gm = provider.gravitational_parameter(request.destination);
        const double settle = design.geometry.station || !(gm > 0.0)
                                  ? 600.0
                                  : 2.0 * units::pi * std::sqrt(std::pow(design.geometry.radius, 3) / gm);

        ++progress.candidates_flown;
        report();
        const auto flight = fly(state, request, *craft, initial, design, settle);
        if (!flight.ok) {
            std::ostringstream os;
            os << "the guided flight from a departure " << (departure - state.epoch).seconds()
               << " s from now stopped: " << propagation::to_string(flight.status) << " -- " << flight.message;
            last_problem = os.str();
            if (flight.status == propagation::PropagationStatus::OutOfPropellant) {
                result.status = MissionPlanStatus::InsufficientPropellant;
                result.failure = FailureReason{TransferFailure::InsufficientDepartureDeltaV, last_problem};
                return result;
            }
            continue;
        }

        // How close the flight came to the prescribed arrival.
        const auto destination_at_arrival = provider.state(request.destination, design.arrival, kFrame);
        const Vec3 relative_position = flight.at_arrival.state.position - destination_at_arrival.state.position;
        const Vec3 relative_velocity = flight.at_arrival.state.velocity - destination_at_arrival.state.velocity;
        const double miss = (relative_position - design.geometry.offset).norm();
        const double speed_error = (relative_velocity - design.geometry.velocity).norm();
        const double tolerance = std::max(5.0e3, 0.05 * design.geometry.radius);

        const auto destination_settled = provider.state(request.destination, flight.settled_at, kFrame);
        coordinates::StateVector settled{};
        settled.position = flight.settled.state.position - destination_settled.state.position;
        settled.velocity = flight.settled.state.velocity - destination_settled.state.velocity;
        const auto elements = trajectory::elements_from_state(settled, gm);
        const double radius = provider.mean_radius(request.destination);

        if (miss > tolerance) {
            std::ostringstream os;
            os << "the guided flight arrived " << miss / 1000.0 << " km from the arrival point (tolerance "
               << tolerance / 1000.0 << " km)";
            last_problem = os.str();
            continue;
        }

        // ---- the plan, and what its flight measured ----
        ++progress.candidates_succeeded;
        report();
        result.status = MissionPlanStatus::Planned;
        result.maneuvers.add(design.injection);
        result.maneuvers.add(design.insertion);
        result.trajectory = flight.trajectory;

        const auto& engine = craft->engine();
        const double m0 = flight.at_departure.mass;
        const double m_mid = flight.at_midpoint.mass;
        const double m_cut = flight.at_cutoff.mass;
        auto& metrics = result.metrics;
        metrics.direct = true;
        metrics.station = design.geometry.station;
        metrics.origin = request.origin;
        metrics.destination = request.destination;
        metrics.departure = design.departure;
        metrics.arrival = design.arrival;
        metrics.capture_ignition = design.insertion.ignition;
        metrics.capture_cutoff = design.insertion.cutoff();
        metrics.time_of_flight_s = design.time_of_flight;
        metrics.injection_delta_v = engine.delta_v_for_mass_ratio(m0, m_mid);
        metrics.capture_delta_v = engine.delta_v_for_mass_ratio(m_mid, m_cut);
        metrics.total_delta_v = metrics.injection_delta_v + metrics.capture_delta_v;
        metrics.injection_duration_s = design.injection.duration.seconds();
        metrics.capture_duration_s = design.insertion.duration.seconds();
        metrics.peak_speed = flight.peak_speed;
        metrics.arrival_miss = miss;
        metrics.arrival_speed_error = speed_error;
        metrics.requested_periapsis_altitude = altitude;
        metrics.requested_apoapsis_altitude = altitude;
        if (design.geometry.station) {
            // A point, not an orbit: its "elements" would say the ship falls into
            // Phobos, which over the minutes the station is read is the truth and
            // the wrong question. What is reported is where the ship is.
            metrics.predicted_periapsis_altitude = settled.position.norm() - radius;
            metrics.predicted_apoapsis_altitude = settled.position.norm() - radius;
        } else {
            metrics.predicted_periapsis_altitude = elements.periapsis_radius - radius;
            metrics.predicted_apoapsis_altitude =
                elements.bound ? elements.apoapsis_radius - radius : settled.position.norm() - radius;
        }
        metrics.predicted_eccentricity = elements.eccentricity;
        metrics.predicted_inclination = elements.inclination;
        metrics.predicted_raan = elements.raan;
        metrics.mass_at_departure = m0;
        metrics.propellant_required = initial.mass - flight.settled.mass;
        metrics.propellant_remaining = flight.settled.mass - craft->dry_mass();
        metrics.delta_v_available = engine.delta_v_for_mass_ratio(m0, craft->dry_mass());

        MissionAlternative chosen{};
        std::ostringstream label;
        label << "direct/" << design.time_of_flight / 60.0 << "min";
        chosen.label = label.str();
        chosen.feasible = true;
        chosen.departure = design.departure;
        chosen.time_of_flight_s = design.time_of_flight;
        chosen.departure_coast_s = (design.departure - state.epoch).seconds();
        chosen.injection_delta_v = metrics.injection_delta_v;
        chosen.capture_delta_v = metrics.capture_delta_v;
        chosen.total_delta_v = metrics.total_delta_v;
        chosen.predicted_periapsis_altitude = metrics.predicted_periapsis_altitude;
        chosen.predicted_apoapsis_altitude = metrics.predicted_apoapsis_altitude;
        chosen.predicted_eccentricity = metrics.predicted_eccentricity;
        chosen.predicted_inclination = metrics.predicted_inclination;
        chosen.predicted_raan = metrics.predicted_raan;
        result.alternatives.push_back(chosen);
        return result;
    }

    result.status = MissionPlanStatus::NoSolution;
    result.failure = FailureReason{TransferFailure::NoFeasibleTrajectory, last_problem};
    return result;
}

}  // namespace sf::navigation
