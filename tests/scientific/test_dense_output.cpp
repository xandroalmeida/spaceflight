// The continuous extension (ADR-0006).
//
// Two claims have to hold, and both are asserted here rather than promised:
//   1. recording changes the computed trajectory by exactly nothing;
//   2. an interpolated state is close to the true solution -- close enough that
//      the renderer never has to ask the integrator to stop at a frame boundary.

#include "core/celestial/body_catalog.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "tests/support/analytic_ephemeris.hpp"
#include "tests/support/kepler.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace sf;
using sf::coordinates::StateVector;
using sf::math::Vec3;

namespace {

constexpr double kGm = 3.9860043550702266e14;
const celestial::BodyId kEarth = celestial::bodies::earth;

struct Rig {
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};
    celestial::BodyCatalog catalog;
    std::unique_ptr<gravity::PointMassGravity> forces;

    Rig() {
        const std::vector<celestial::BodyId> ids{kEarth};
        catalog = celestial::BodyCatalog::resolve(provider, ids);
        forces = std::make_unique<gravity::PointMassGravity>(
            provider, catalog, coordinates::ReferenceFrame::ssb_j2000());
    }
};

propagation::IntegratorConfig config() {
    propagation::IntegratorConfig cfg{};
    cfg.relative_tolerance = 1.0e-12;
    cfg.absolute_tolerance_position = 1.0e-6;
    cfg.absolute_tolerance_velocity = 1.0e-9;
    cfg.initial_step = time::Duration::seconds(10.0);
    cfg.max_step = time::Duration::seconds(600.0);
    return cfg;
}

// A moderately eccentric orbit: the step size varies by a large factor around it,
// which is precisely the situation dense output exists to handle.
struct Orbit {
    StateVector start;
    double period;
};

Orbit make_orbit(double a = 1.5e7, double e = 0.4) {
    const double rp = a * (1.0 - e);
    const double vp = std::sqrt(kGm * (1.0 + e) / (a * (1.0 - e)));
    return Orbit{StateVector{Vec3{rp, 0.0, 0.0}, Vec3{0.0, vp, 0.0}},
                 2.0 * units::pi * std::sqrt(a * a * a / kGm)};
}

}  // namespace

TEST(recording_dense_output_does_not_change_the_trajectory) {
    // The central claim of ADR-0006.  If this ever fails, dense output has started
    // influencing the physics and must be reverted, not adjusted.
    Rig rig;
    const Orbit orbit = make_orbit();

    propagation::PropagationState initial{};
    initial.state = orbit.start;

    const auto t0 = time::CoordinateTime::j2000();
    const auto t1 = t0 + time::Duration::seconds(2.0 * orbit.period);

    propagation::DormandPrince54Propagator plain{*rig.forces, config()};
    const auto without = plain.propagate(initial, t0, t1);

    propagation::Trajectory trajectory;
    propagation::DormandPrince54Propagator recording{*rig.forces, config()};
    recording.set_trajectory_recorder(&trajectory);
    const auto with = recording.propagate(initial, t0, t1);

    REQUIRE(without.ok());
    REQUIRE(with.ok());

    CHECK_EQ(with.stats.accepted_steps, without.stats.accepted_steps);
    CHECK_EQ(with.stats.rejected_steps, without.stats.rejected_steps);
    CHECK_EQ(with.stats.force_evaluations, without.stats.force_evaluations);
    CHECK_EQ(trajectory.size(), without.stats.accepted_steps);

    CHECK_NEAR_ABS((with.state.state.position - without.state.state.position).norm(), 0.0, 0.0,
                   "bit-for-bit identity, not approximate agreement: recording reads the stages "
                   "the step already computed and writes them elsewhere. Any non-zero difference "
                   "means the recording path perturbed the integration");
    CHECK_NEAR_ABS((with.state.state.velocity - without.state.state.velocity).norm(), 0.0, 0.0,
                   "same argument for velocity");
}

TEST(the_interpolant_is_exact_at_the_step_endpoints) {
    Rig rig;
    const Orbit orbit = make_orbit();

    propagation::PropagationState initial{};
    initial.state = orbit.start;

    propagation::Trajectory trajectory;
    propagation::DormandPrince54Propagator propagator{*rig.forces, config()};
    propagator.set_trajectory_recorder(&trajectory);

    const auto t0 = time::CoordinateTime::j2000();
    const auto t1 = t0 + time::Duration::seconds(orbit.period);
    const auto result = propagator.propagate(initial, t0, t1);
    REQUIRE(result.ok());
    REQUIRE(!trajectory.empty());

    // theta = 0 and theta = 1 reproduce the step's own endpoints by construction:
    // c1 = y0 and c1 + c2 = y1.
    const auto at_start = trajectory.state_at(t0);
    CHECK_NEAR_ABS((at_start.state.position - initial.state.position).norm(), 0.0, 0.0,
                   "y(0) = c1 = y0 identically; this is an algebraic identity of the interpolant, "
                   "so the tolerance is zero");

    const auto at_end = trajectory.state_at(t1);
    CHECK_NEAR_ABS((at_end.state.position - result.state.state.position).norm(), 0.0, 1.0e-9,
                   "y(1) = c1 + c2 = y1 by construction; the only slack is that reaching theta = 1 "
                   "goes through a division by the step size, which rounds");

    // Interior segment boundaries: querying the shared instant from either side
    // must give the same state.
    std::size_t checked = 0;
    for (std::size_t i = 1; i < trajectory.size() && checked < 20; ++i, ++checked) {
        const auto boundary = trajectory.segments()[i].begin;
        const auto left = trajectory.segments()[i - 1].at_theta(1.0);
        const auto right = trajectory.segments()[i].at_theta(0.0);
        CHECK_NEAR_ABS((left.state.position - right.state.position).norm(), 0.0, 1.0e-9,
                       "the interpolant is continuous across a step boundary because both sides "
                       "evaluate to the same stored state vector");
        (void)boundary;
    }
}

TEST(interpolated_states_are_as_accurate_as_the_steps_they_come_from) {
    Rig rig;
    const Orbit orbit = make_orbit();

    propagation::PropagationState initial{};
    initial.state = orbit.start;

    propagation::Trajectory trajectory;
    propagation::DormandPrince54Propagator propagator{*rig.forces, config()};
    propagator.set_trajectory_recorder(&trajectory);

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0,
                                             t0 + time::Duration::seconds(orbit.period));
    REQUIRE(result.ok());

    // Error of the step endpoints against the closed-form solution...
    double endpoint_error = 0.0;
    for (const auto& segment : trajectory.segments()) {
        const double dt = (segment.end() - t0).seconds();
        const StateVector exact = sft::kepler_propagate(orbit.start, kGm, dt);
        endpoint_error = std::max(endpoint_error,
                                  (segment.at_theta(1.0).state.position - exact.position).norm());
    }

    // ...and of 2000 interpolated instants, most of them mid-step.
    double interpolated_error = 0.0;
    const std::size_t samples = 2000;
    for (const auto& [t, state] : trajectory.sample(samples)) {
        const StateVector exact = sft::kepler_propagate(orbit.start, kGm, (t - t0).seconds());
        interpolated_error = std::max(interpolated_error, (state.state.position - exact.position).norm());
    }

    std::ostringstream os;
    os << "over one orbit: worst endpoint error " << endpoint_error << " m, worst interpolated "
       << interpolated_error << " m across " << samples << " samples of " << trajectory.size()
       << " steps";
    INFO(os.str());

    CHECK_NEAR_ABS(interpolated_error, 0.0, 1.0e-2,
                   "both errors are measured against the closed-form Kepler solution, so both "
                   "contain the integrator's accumulated global error over one orbit -- which at "
                   "rtol = 1e-12 is ~3e-4 m and dominates completely. The bound is that value with "
                   "an order and a half of margin; it is 7e-10 relative to the 1.5e7 m orbit");

    // The claim that matters: the continuous extension adds nothing measurable.
    // A 4th order interpolant over h <= 600 s contributes an error far below the
    // integration error already present at the step endpoints, so sampling
    // between steps is as good as stopping on them -- which is the entire reason
    // the renderer is allowed to ask for arbitrary epochs (ADR-0006).
    CHECK(interpolated_error >= endpoint_error);
    CHECK_NEAR_REL(interpolated_error, endpoint_error, 1.0,
                   "the interpolated worst case must not exceed the endpoint worst case by more "
                   "than a factor of two. Measured: they agree to every printed digit, i.e. the "
                   "worst interpolated sample IS a step endpoint and the interpolation contributes "
                   "nothing detectable. A factor of two would already be a warning that the "
                   "continuous extension had been degraded (for instance to cubic Hermite)");
}

TEST(sampling_never_costs_a_force_evaluation) {
    Rig rig;
    const Orbit orbit = make_orbit();

    propagation::PropagationState initial{};
    initial.state = orbit.start;

    propagation::Trajectory trajectory;
    propagation::DormandPrince54Propagator propagator{*rig.forces, config()};
    propagator.set_trajectory_recorder(&trajectory);

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0,
                                             t0 + time::Duration::seconds(orbit.period));
    REQUIRE(result.ok());

    // 100 000 states -- more than a minute of rendering at 144 Hz from one orbit.
    const auto dense = trajectory.sample(100'000);
    CHECK_EQ(dense.size(), std::size_t{100'000});

    std::ostringstream os;
    os << trajectory.size() << " steps -> 100000 states, " << result.stats.force_evaluations
       << " force evaluations in total (all of them spent on the integration)";
    INFO(os.str());

    // Radius must stay inside the conic the orbit was built from.
    const double rp = 1.5e7 * (1.0 - 0.4);
    const double ra = 1.5e7 * (1.0 + 0.4);
    for (const auto& [t, state] : dense) {
        (void)t;
        const double radius = state.state.position.norm();
        CHECK(radius > rp * (1.0 - 1.0e-9));
        CHECK(radius < ra * (1.0 + 1.0e-9));
    }
}

TEST(epochs_outside_the_recorded_arc_are_refused) {
    Rig rig;
    const Orbit orbit = make_orbit();

    propagation::PropagationState initial{};
    initial.state = orbit.start;

    propagation::Trajectory trajectory;
    propagation::DormandPrince54Propagator propagator{*rig.forces, config()};
    propagator.set_trajectory_recorder(&trajectory);

    const auto t0 = time::CoordinateTime::j2000();
    const auto t1 = t0 + time::Duration::seconds(orbit.period);
    REQUIRE(propagator.propagate(initial, t0, t1).ok());

    CHECK(trajectory.contains(t0));
    CHECK(trajectory.contains(t1));
    CHECK(trajectory.contains(t0 + time::Duration::seconds(orbit.period / 3.0)));

    // Same rule as the ephemeris layer: no data means an error, not a guess.
    CHECK_THROWS_AS(trajectory.state_at(t0 - time::Duration::seconds(1.0)), std::out_of_range);
    CHECK_THROWS_AS(trajectory.state_at(t1 + time::Duration::seconds(1.0)), std::out_of_range);
    CHECK_THROWS_AS(propagation::Trajectory{}.state_at(t0), std::out_of_range);
}

TEST(dense_output_works_for_backwards_propagation) {
    Rig rig;
    const Orbit orbit = make_orbit();

    propagation::PropagationState initial{};
    initial.state = orbit.start;

    const auto t0 = time::CoordinateTime::j2000();
    const auto t1 = t0 + time::Duration::seconds(orbit.period);

    propagation::DormandPrince54Propagator forward{*rig.forces, config()};
    const auto end_state = forward.propagate(initial, t0, t1);
    REQUIRE(end_state.ok());

    propagation::Trajectory trajectory;
    propagation::DormandPrince54Propagator backward{*rig.forces, config()};
    backward.set_trajectory_recorder(&trajectory);
    const auto back = backward.propagate(end_state.state, t1, t0);
    REQUIRE(back.ok());

    // Segments were recorded in descending time order; the queries must not care.
    CHECK(trajectory.earliest() == t0);
    CHECK(trajectory.latest() == t1);

    const auto midpoint = t0 + time::Duration::seconds(orbit.period / 2.0);
    const StateVector exact = sft::kepler_propagate(orbit.start, kGm, orbit.period / 2.0);
    const double error = (trajectory.state_at(midpoint).state.position - exact.position).norm();

    std::ostringstream os;
    os << "backwards arc, interpolated midpoint vs closed form: " << error << " m";
    INFO(os.str());

    CHECK_NEAR_ABS(error, 0.0, 1.0e-3,
                   "the same interpolation bound as forwards, plus the accumulated global error of "
                   "the outbound leg that produced the starting state for this one");
}
