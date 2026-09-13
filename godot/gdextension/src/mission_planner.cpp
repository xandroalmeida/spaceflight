// Planning a transfer, for the scene.
//
// Kept out of simulation_node.cpp because it is a different kind of code: that
// file advances a state sixty times a second, this one runs a search and two
// differential corrections once, when a pilot asks for it. Mixing them would hide
// a one-second blocking call inside a file whose every other function is a frame
// budget.
//
// The physics is all in core/: Lambert (trajectory/lambert.hpp), the corrector
// (navigation/targeting.hpp), the B-plane (navigation/b_plane.hpp). This chooses
// WHEN to leave and wires the result into a plan.
// See docs/physics/b-plane.md section 9.

#include "mission_planner.hpp"

#include "core/coordinates/reference_frame.hpp"
#include "core/navigation/b_plane.hpp"
#include "core/navigation/mission.hpp"
#include "core/navigation/targeting.hpp"
#include "core/navigation/trajectory_planner.hpp"
#include "core/trajectory/lambert.hpp"
#include "core/units/constants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include <string>

namespace spaceflight_godot {
namespace {

using sf::math::Vec3;

constexpr double kGoldenRatioInverse = 0.6180339887498949;

// Closest approach to `target`, refined off the dense output.
//
// Refined rather than argmin over samples for the same reason the CLI refines it:
// the corrector differences this map, and an argmin over a fixed grid is a
// staircase. The time of closest approach is itself one of the three targeted
// quantities (docs/physics/b-plane.md section 5).
struct Approach {
    sf::time::CoordinateTime time{};
    Vec3 relative_position{};
    Vec3 relative_velocity{};
    double distance{std::numeric_limits<double>::infinity()};
    bool valid{false};
};

Approach find_approach(const sf::propagation::Trajectory& arc,
                       const sf::ephemeris::EphemerisProvider& provider,
                       sf::celestial::BodyId target,
                       const sf::coordinates::ReferenceFrame& frame) {
    Approach best{};
    if (arc.empty()) {
        return best;
    }
    auto separation = [&](sf::time::CoordinateTime t) {
        const auto state = arc.state_at(t);
        const auto body = provider.state(target, t, frame);
        return (state.state.position - body.state.position).norm();
    };

    const auto samples = arc.sample(1200);
    std::size_t index = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto body = provider.state(target, samples[i].first, frame);
        const double d = (samples[i].second.state.position - body.state.position).norm();
        if (d < best.distance) {
            best.distance = d;
            best.time = samples[i].first;
            index = i;
        }
    }
    if (samples.size() < 3) {
        return best;
    }

    double lo = samples[index > 0 ? index - 1 : 0].first.seconds_since_j2000();
    double hi = samples[std::min(index + 1, samples.size() - 1)].first.seconds_since_j2000();
    for (int i = 0; i < 60 && hi - lo > 1.0e-6; ++i) {
        const double a = hi - (hi - lo) * kGoldenRatioInverse;
        const double b = lo + (hi - lo) * kGoldenRatioInverse;
        const auto ta = sf::time::CoordinateTime::from_seconds_since_j2000(a);
        const auto tb = sf::time::CoordinateTime::from_seconds_since_j2000(b);
        if (!arc.contains(ta) || !arc.contains(tb)) {
            break;
        }
        if (separation(ta) < separation(tb)) {
            hi = b;
        } else {
            lo = a;
        }
    }
    const auto t_ca = sf::time::CoordinateTime::from_seconds_since_j2000(0.5 * (lo + hi));
    if (!arc.contains(t_ca)) {
        return best;
    }
    const auto state = arc.state_at(t_ca);
    const auto body = provider.state(target, t_ca, frame);
    best.time = t_ca;
    best.relative_position = state.state.position - body.state.position;
    best.relative_velocity = state.state.velocity - body.state.velocity;
    best.distance = best.relative_position.norm();
    best.valid = true;
    return best;
}

}  // namespace

TransferPlan plan_transfer(const TransferRequest& request) {
    TransferPlan plan{};

    const auto frame = sf::coordinates::ReferenceFrame::ssb_j2000();
    const auto& provider = *request.provider;
    const double central_gm = provider.gravitational_parameter(request.center);
    const double target_gm = provider.gravitational_parameter(request.target);
    const double target_radius = provider.mean_radius(request.target);
    if (!(target_gm > 0.0) || !(target_radius > 0.0)) {
        plan.message = "target has no GM or radius in the loaded kernels";
        return plan;
    }

    // ---- 1. when to leave --------------------------------------------------
    //
    // The scene's parking orbit is wherever the pilot left it, and most points in
    // it are a bad place to start: a transfer angle near 180 degrees is degenerate
    // for Lambert, and the wrong phase costs kilometres per second. So coast the
    // CURRENT state forward once, and try a Lambert departure from every sample.
    // One propagation, then arithmetic.
    sf::propagation::Trajectory coast;
    auto scan_state = request.initial;
    const auto scan_end =
        request.epoch + sf::time::Duration::seconds(request.search_window_s);
    {
        // Recording is a setter, not an argument, and it costs no force
        // evaluations (ADR-0006).
        request.propagator->set_trajectory_recorder(&coast);
        const auto result = request.propagator->propagate(scan_state, request.epoch, scan_end);
        request.propagator->set_trajectory_recorder(nullptr);
        if (!result.ok()) {
            plan.message = "could not coast the parking orbit: " + result.message;
            return plan;
        }
    }

    // Every candidate is kept, not just the cheapest, because delta-v turns out
    // to be the wrong way to choose (see below).
    struct Candidate {
        sf::time::CoordinateTime departure{};
        sf::propagation::PropagationState state{};
        Vec3 departure_velocity{};
        double tof{0.0};
        double cost{0.0};
        double angle{0.0};
    };
    std::vector<Candidate> candidates;

    int rejected_angle = 0;
    int rejected_throw = 0;
    int rejected_tof = 0;
    int considered = 0;
    double closest_angle = 0.0;
    std::string first_throw;
    const auto samples = coast.sample(request.departure_samples);
    for (const auto& [t_depart, state] : samples) {
        const auto center_state = provider.state(request.center, t_depart, frame);
        const Vec3 r1 = state.state.position - center_state.state.position;

        // A WIDE spread of flight times, and that is where the search really
        // lives. What decides whether a transfer exists at all is the angle
        // between where the ship is and where the Moon WILL BE, and the Moon moves
        // 13 degrees a day while the parking orbit only changes which side of the
        // Earth the ship leaves from. A two-hour departure window barely moves the
        // geometry; three days of flight time moves it 40 degrees.
        //
        // Measured with a narrow spread: 144 candidates, 140 of them unreachable
        // within one revolution and the other 4 sitting above 174 degrees, where
        // the transfer plane is ill-conditioned. Nothing to fly.
        // The flight time stays near what was ASKED for. Ranging wider and then
        // picking the cheapest looks like a better search and is not: a longer
        // transfer is always cheaper -- it approaches the minimum-energy ellipse --
        // and it is also far more sensitive to the departure velocity, which is
        // what the corrector then has to invert. Left free, the search reached for
        // 7.2 days, saved 150 m/s that a ship with 26 942 km/s of budget does not
        // need, and handed the corrector a problem it stalled on at 53 000 km.
        for (const double scale : {0.7, 0.85, 1.0, 1.15, 1.3, 1.5}) {
            const auto tof = sf::time::Duration::seconds(request.time_of_flight_s * scale);
            const auto t_arrive = t_depart + tof;
            // Relative to the central body AT ARRIVAL, which is a different point
            // in space from the central body at departure: over 4.5 days the Earth
            // moves 1.1e9 m, three times the whole Earth-Moon distance. Subtracting
            // the departure-epoch Earth put r2 a million kilometres off and every
            // Lambert solve failed as unreachable -- 381 of 384 candidates.
            const auto centred = sf::coordinates::ReferenceFrame::centered_on(request.center);
            const Vec3 r2 = provider.state(request.target, t_arrive, centred).state.position;

            // Lambert is degenerate at a transfer angle of exactly 180 degrees and
            // ill-conditioned near it: the plane of the transfer stops being
            // defined by the two positions (docs/physics/lambert.md section 3).
            const double transfer_angle = sf::math::angle_between(r1, r2);
            ++considered;
            closest_angle = transfer_angle;
            // Only the genuinely degenerate band is refused. At exactly pi every
            // plane containing both points is a solution and the transfer plane is
            // undefined (docs/physics/lambert.md section 3); near it the plane is
            // defined but badly conditioned, and a lunar injection there comes out
            // at an inclination nobody asked for. 177 degrees is where that starts
            // to bite.
            if (transfer_angle > 3.09 || transfer_angle < 0.09) {
                ++rejected_angle;
                continue;
            }

            sf::trajectory::LambertSolution solution{};
            try {
                solution = sf::trajectory::solve_lambert(r1, r2, tof, central_gm);
            } catch (const std::exception& e) {
                ++rejected_throw;
                if (first_throw.empty()) {
                    first_throw = e.what();
                }
                continue;
            }
            // LambertSolution reports the time of flight it ACHIEVED rather than a
            // boolean: the iteration can return something that is not the transfer
            // that was asked for, and comparing the two is the honest check.
            if (!std::isfinite(solution.achieved_time_of_flight) ||
                std::abs(solution.achieved_time_of_flight - tof.seconds()) >
                    1.0e-6 * tof.seconds()) {
                ++rejected_tof;
                continue;
            }

            const Vec3 departure_velocity =
                center_state.state.velocity + solution.departure_velocity;
            candidates.push_back(Candidate{t_depart, state, departure_velocity, tof.seconds(),
                                           (departure_velocity - state.state.velocity).norm(),
                                           transfer_angle});
        }
    }

    if (candidates.empty()) {
        plan.message = "no Lambert departure in the search window (" +
                       std::to_string(considered) + " considered, " +
                       std::to_string(rejected_angle) + " rejected on transfer angle, " +
                       std::to_string(rejected_throw) + " threw, " +
                       std::to_string(rejected_tof) + " missed the time of flight; last angle " +
                       std::to_string(closest_angle) + " rad). First failure: " + first_throw;
        return plan;
    }

    // ---- 1b. choose by FLYING, not by price --------------------------------
    //
    // The cheapest Lambert solution is the wrong one to hand a corrector, and the
    // measurements say so plainly: selecting on delta-v picked a 7.2-day transfer
    // that saved 150 m/s -- irrelevant to a ship carrying 26 942 km/s -- and left
    // stage 1 stalled at 53 000 km. Narrowing the flight-time band instead picked
    // a different bad one and stalled at 1.4e7 km.
    //
    // So: fly the few cheapest once each, in the full model, and keep whichever
    // actually ends up nearest the target. One propagation per candidate, and it
    // measures the thing that matters instead of a proxy for it.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.cost < b.cost; });
    const std::size_t trials =
        std::min<std::size_t>(candidates.size(), static_cast<std::size_t>(request.trial_count));

    auto fly_candidate = [&](const Candidate& c, sf::propagation::Trajectory* arc,
                             sf::time::CoordinateTime until) {
        const Vec3 impulse = c.departure_velocity - c.state.state.velocity;
        *request.plan = sf::navigation::ManeuverPlan{};
        auto injection = sf::navigation::maneuver_for_delta_v(
            *request.craft, c.state.mass, impulse.norm(), c.departure,
            sf::navigation::GuidanceMode::Inertial, request.center, 1.0, "transfer injection");
        injection.ignition = c.departure;
        injection.inertial_direction = impulse.normalized();
        request.plan->add(std::move(injection));
        return sf::navigation::run_mission(*request.propagator, *request.executor, c.state,
                                           c.departure, until, arc);
    };

    std::vector<std::pair<double, std::size_t>> ranked;
    for (std::size_t i = 0; i < trials; ++i) {
        const auto& c = candidates[i];
        const auto t_arrive_i = c.departure + sf::time::Duration::seconds(c.tof);
        const auto flight = fly_candidate(c, nullptr, t_arrive_i);
        const auto target_there = provider.state(request.target, flight.time, frame);
        ranked.emplace_back((flight.state.state.position - target_there.state.position).norm(), i);
    }
    std::sort(ranked.begin(), ranked.end());

    // Try them in that order and keep the first that CLOSES.
    //
    // Not belt and braces: whether the corrector converges depends on the
    // departure geometry, and from an arbitrary parking orbit some geometries
    // simply do not invert -- stage 1 stalls at a few thousand kilometres and
    // stage 2 cannot recover from there. Measured across runs that differed only
    // in which frame the pilot pressed the key. One attempt is a coin toss; three
    // is a plan.
    // The per-candidate pipeline: correct, shape the flyby, build the burns.
    auto shape_and_build = [&](const Candidate& winner, double flown_miss) -> TransferPlan {
    TransferPlan plan{};
    plan.lambert_delta_v = winner.cost;
    const auto best_departure = winner.departure;
    const auto best_state = winner.state;
    const auto best_departure_velocity = winner.departure_velocity;

    plan.departure = best_departure;
    plan.time_of_flight_s = winner.tof;
    plan.transfer_angle = winner.angle;
    plan.candidate_miss = flown_miss;
    const auto t_arrive = best_departure + sf::time::Duration::seconds(winner.tof);

    // ---- 2. fly it in the full model, AS A FINITE BURN ----------------------
    //
    // `departure_velocity` is the velocity the two-body plan wants after the
    // injection. What the scene actually flies is an engine running for six
    // minutes, and the two are not the same trajectory: the CLI measures
    // 3393.6 m/s of engine delta-v producing 2780.0 m/s of speed change, the
    // difference being what gravity took while the engine was on. Correcting an
    // impulse and then flying a burn misses by hundreds of thousands of
    // kilometres -- measured, by doing exactly that.
    //
    // So every trial writes its burn into the plan the force model already reads
    // and flies it through run_mission, which splits the arc at ignition and
    // cutoff and arms the executor for each leg.
    auto build_injection = [&](const Vec3& departure_velocity) {
        const Vec3 impulse = departure_velocity - best_state.state.velocity;
        *request.plan = sf::navigation::ManeuverPlan{};
        auto injection = sf::navigation::maneuver_for_delta_v(
            *request.craft, best_state.mass, impulse.norm(), best_departure,
            sf::navigation::GuidanceMode::Inertial, request.center, 1.0, "transfer injection");
        injection.ignition = best_departure;
        injection.inertial_direction = impulse.normalized();
        request.plan->add(std::move(injection));
    };

    auto fly = [&](const Vec3& departure_velocity, sf::propagation::Trajectory* arc,
                   sf::time::CoordinateTime until) {
        build_injection(departure_velocity);
        return sf::navigation::run_mission(*request.propagator, *request.executor, best_state,
                                           best_departure, until, arc);
    };

    // The probe flies PAST the nominal arrival so that closest approach is
    // INTERIOR to the window. Pinned at the end of it, the time of closest
    // approach stops responding to the departure velocity and the third row of the
    // Jacobian goes to zero (docs/physics/b-plane.md section 9.2).
    const auto probe_end = t_arrive + sf::time::Duration::seconds(winner.tof * 0.25);

    sf::navigation::TargetingConfig targeting{};
    targeting.position_tolerance = request.position_tolerance_m;

    // Stage one: reach the body at all. From the raw Lambert solution the miss is
    // hundreds of thousands of kilometres, and at that distance there is no
    // hyperbolic flyby to read a B-plane off -- the Jacobian columns come out
    // nearly parallel. Measured; section 9.
    const Vec3 target_at_arrival = provider.state(request.target, t_arrive, frame).state.position;
    const auto reach = sf::navigation::correct_departure(
        [&](const Vec3& v) {
            return fly(v, nullptr, t_arrive).state.state.position;
        },
        best_departure_velocity, target_at_arrival, targeting);

    plan.reach_miss_initial = reach.initial_miss;
    plan.reach_miss_final = reach.miss_distance;
    plan.reach_message = reach.message;
    plan.reach_iterations = reach.iterations;
    plan.reach_evaluations = reach.evaluations;
    Vec3 departure_velocity = reach.departure_velocity;

    // ---- 3. open the periapsis --------------------------------------------
    const double wanted_periapsis = target_radius + request.flyby_altitude_m;
    auto b_plane_of = [&](const Vec3& v) -> std::optional<sf::navigation::BPlane> {
        sf::propagation::Trajectory probe;
        (void)fly(v, &probe, probe_end);
        const Approach approach = find_approach(probe, provider, request.target, frame);
        if (!approach.valid) {
            return std::nullopt;
        }
        try {
            return sf::navigation::b_plane_from_state(approach.relative_position,
                                                      approach.relative_velocity, target_gm);
        } catch (const std::domain_error&) {
            return std::nullopt;
        }
    };

    for (int pass = 0; pass < 3; ++pass) {
        const auto measured = b_plane_of(departure_velocity);
        if (!measured.has_value()) {
            plan.message = "the transfer does not reach the target as a hyperbolic approach";
            return plan;
        }
        const auto aim = sf::navigation::aim_for_periapsis(
            wanted_periapsis, target_gm, measured->v_infinity, request.b_plane_angle);

        // The tolerance the pilot means is on the PERIAPSIS, not on |B|:
        // dr_p/db = b / (r_p + mu/v_inf^2), which for a lunar flyby is about 0.6.
        const double aim_magnitude = std::hypot(aim.b_dot_t, aim.b_dot_r);
        const double dr_p_db =
            aim_magnitude /
            (wanted_periapsis + target_gm / (measured->v_infinity * measured->v_infinity));
        auto b_targeting = targeting;
        b_targeting.position_tolerance = request.periapsis_tolerance_m / dr_p_db;

        const double t_target = t_arrive.seconds_since_j2000();
        const auto correction = sf::navigation::correct_departure(
            [&](const Vec3& v) -> Vec3 {
                sf::propagation::Trajectory probe;
                (void)fly(v, &probe, probe_end);
                const Approach approach = find_approach(probe, provider, request.target, frame);
                if (!approach.valid) {
                    return Vec3{1.0e12, 1.0e12, 1.0e12};
                }
                try {
                    const auto bp = sf::navigation::b_plane_from_state(
                        approach.relative_position, approach.relative_velocity, target_gm);
                    return Vec3{bp.b_dot_t, bp.b_dot_r,
                                bp.v_infinity *
                                    (approach.time.seconds_since_j2000() - t_target)};
                } catch (const std::domain_error&) {
                    return Vec3{1.0e12, 1.0e12, 1.0e12};
                }
            },
            departure_velocity, Vec3{aim.b_dot_t, aim.b_dot_r, 0.0}, b_targeting);

        departure_velocity = correction.departure_velocity;
        plan.b_plane_miss = correction.miss_distance;
        plan.shape_message = correction.message;
        plan.passes = pass + 1;

        const auto achieved = b_plane_of(departure_velocity);
        if (achieved.has_value() &&
            std::abs(achieved->periapsis_radius - wanted_periapsis) <
                request.periapsis_tolerance_m) {
            break;
        }
    }

    const auto final_b_plane = b_plane_of(departure_velocity);
    if (!final_b_plane.has_value()) {
        plan.message = "lost the hyperbolic approach while shaping the flyby";
        return plan;
    }
    plan.v_infinity = final_b_plane->v_infinity;
    plan.periapsis_radius = final_b_plane->periapsis_radius;
    plan.flyby_altitude_m = final_b_plane->periapsis_radius - target_radius;

    // ---- 4. the burns ------------------------------------------------------
    // The injection is already in request.plan -- it is what the last trial flew,
    // and therefore what was corrected. Taking it from there rather than rebuilding
    // it is what guarantees the flown plan IS the corrected one.
    build_injection(departure_velocity);
    plan.maneuvers = *request.plan;
    plan.injection_delta_v = (departure_velocity - best_state.state.velocity).norm();

    // A plan that does not reach the target is not a plan. Saying so here is the
    // difference between a mission that fails loudly at planning time and one that
    // fails quietly four simulated days later, somewhere past the Moon.
    const double altitude_error = std::abs(plan.flyby_altitude_m - request.flyby_altitude_m);
    if (altitude_error > std::max(50.0e3, 10.0 * request.periapsis_tolerance_m)) {
        plan.message = "targeting did not close: the flyby comes out at " +
                       std::to_string(plan.flyby_altitude_m / 1000.0) + " km instead of " +
                       std::to_string(request.flyby_altitude_m / 1000.0) +
                       " km. Stage 1 ended " + std::to_string(plan.reach_miss_final / 1000.0) +
                       " km out (" + plan.reach_message + ")";
        return plan;
    }

    if (request.insert) {
        sf::propagation::Trajectory arc;
        const auto flight = fly(departure_velocity, &arc, probe_end);
        const Approach approach = find_approach(arc, provider, request.target, frame);
        // The insertion must ignite after the injection has finished: one engine
        // cannot burn in two directions, and ManeuverPlan refuses the overlap
        // outright. Reaching this means the "closest approach" landed inside the
        // departure burn, which only happens when the trajectory never went
        // anywhere near the target.
        const bool after_injection =
            !plan.maneuvers.empty() &&
            approach.time > plan.maneuvers.maneuvers().front().cutoff();
        if (approach.valid && after_injection) {
            const auto burn = sf::navigation::plan_insertion(
                plan.periapsis_radius, target_gm, plan.v_infinity, request.insert_apoapsis_m);
            plan.insertion_delta_v = burn.delta_v;
            plan.insertion = approach.time;
            plan.orbit_period_s = burn.period;

            // Retrograde about the TARGET, centred on periapsis so the finite burn
            // straddles the point it was planned for instead of starting there.
            auto insertion = sf::navigation::maneuver_for_delta_v(
                *request.craft, flight.state.mass, burn.delta_v, approach.time,
                sf::navigation::GuidanceMode::Retrograde, request.target, 1.0,
                "orbit insertion", sf::navigation::BurnCentering::CenterOnIgnition);
            plan.maneuvers.add(std::move(insertion));
        }
    }

    plan.arrival = t_arrive;
    plan.valid = true;
    plan.message = "planned";
    return plan;
    };

    // Try the candidates in order of how near they actually got, and keep the
    // first that CLOSES.
    //
    // Not belt and braces: whether the corrector converges depends on the
    // departure geometry, and from an arbitrary parking orbit some geometries
    // simply do not invert -- stage 1 stalls at a few thousand kilometres and
    // stage 2 cannot recover. Measured across runs that differed only in which
    // frame the plan was asked for: one attempt is a coin toss.
    TransferPlan attempt{};
    for (std::size_t rank = 0; rank < ranked.size() && rank < 3; ++rank) {
        attempt = shape_and_build(candidates[ranked[rank].second], ranked[rank].first);
        attempt.candidates_tried = static_cast<int>(trials);
        attempt.attempts = static_cast<int>(rank) + 1;
        if (attempt.valid) {
            return attempt;
        }
    }
    if (!attempt.message.empty()) {
        attempt.message = "no departure in the window closed; last: " + attempt.message;
    }
    return attempt;
}

}  // namespace spaceflight_godot
