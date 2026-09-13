// Does the error control actually control the error, and does the propagator
// fail honestly when it cannot meet the request?

#include "core/celestial/body_catalog.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "tests/support/analytic_ephemeris.hpp"
#include "tests/support/kepler.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>
#include <vector>

using namespace sf;
using sf::coordinates::StateVector;
using sf::math::Vec3;

namespace {
constexpr double kGm = 3.9860043550702266e14;
const celestial::BodyId kCentre = celestial::bodies::earth;
}  // namespace

TEST(tightening_the_tolerance_tightens_the_error) {
    sft::FixedPointMassProvider provider{kCentre, kGm, 0.0};
    const std::vector<celestial::BodyId> ids{kCentre};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);
    const gravity::PointMassGravity forces{provider, catalog,
                                           coordinates::ReferenceFrame::ssb_j2000()};

    const double a = 1.2e7;
    const double e = 0.3;
    const double rp = a * (1.0 - e);
    const double vp = std::sqrt(kGm * (1.0 + e) / (a * (1.0 - e)));
    const double period = 2.0 * units::pi * std::sqrt(a * a * a / kGm);

    const StateVector start{Vec3{rp, 0.0, 0.0}, Vec3{0.0, vp, 0.0}};
    const StateVector exact = sft::kepler_propagate(start, kGm, period);

    propagation::PropagationState initial{};
    initial.state = start;

    const auto t0 = time::CoordinateTime::j2000();

    std::vector<double> errors;
    std::vector<std::size_t> step_counts;

    for (const double rtol : {1.0e-6, 1.0e-8, 1.0e-10, 1.0e-12}) {
        propagation::IntegratorConfig cfg{};
        cfg.relative_tolerance = rtol;
        cfg.absolute_tolerance_position = rtol * 1.0e7;   // same relative scale as the orbit
        cfg.absolute_tolerance_velocity = rtol * 1.0e4;
        cfg.initial_step = time::Duration::seconds(10.0);
        cfg.max_step = time::Duration::seconds(600.0);

        propagation::DormandPrince54Propagator propagator{forces, cfg};
        const auto result = propagator.propagate(initial, t0, t0 + time::Duration::seconds(period));
        REQUIRE(result.ok());

        const double error = (result.state.state.position - exact.position).norm();
        errors.push_back(error);
        step_counts.push_back(result.stats.accepted_steps);

        std::ostringstream os;
        os << "rtol " << rtol << ": |dr| = " << error << " m after one orbit, "
           << result.stats.accepted_steps << " steps, " << result.stats.rejected_steps
           << " rejected, " << result.stats.force_evaluations << " force evaluations";
        INFO(os.str());
    }

    for (std::size_t i = 1; i < errors.size(); ++i) {
        CHECK(errors[i] < errors[i - 1]);
        CHECK(step_counts[i] >= step_counts[i - 1]);
    }

    // Two decades of tolerance must buy at least one decade of accuracy.  The
    // relation is not exactly linear because the step size, and therefore the
    // number of error contributions, changes at the same time.
    const double improvement = errors.front() / errors.back();
    std::ostringstream os;
    os << "error improved by a factor " << improvement << " for 1e6 in tolerance";
    INFO(os.str());
    CHECK(improvement > 1.0e3);

    CHECK_NEAR_ABS(errors.back(), 0.0, 1.0e-2,
                   "at rtol = 1e-12 the absolute position floor used here is rtol*1e7 = 1e-5 m per "
                   "step, and one orbit takes ~480 steps, so worst-case linear accumulation is "
                   "4.8e-3 m. Measured: 5.9e-4 m, i.e. 5e-11 relative to the 1.2e7 m semi-major "
                   "axis. The 1e-2 m bound is the worst-case estimate rounded up");
}

TEST(an_impossible_tolerance_is_reported_not_silently_relaxed) {
    sft::FixedPointMassProvider provider{kCentre, kGm, 0.0};
    const std::vector<celestial::BodyId> ids{kCentre};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);
    const gravity::PointMassGravity forces{provider, catalog,
                                           coordinates::ReferenceFrame::ssb_j2000()};

    propagation::IntegratorConfig cfg{};
    cfg.relative_tolerance = 1.0e-18;              // below double precision
    cfg.absolute_tolerance_position = 1.0e-12;
    cfg.absolute_tolerance_velocity = 1.0e-15;
    cfg.min_step = time::Duration::seconds(1.0);   // and a floor it cannot go under
    cfg.max_step = time::Duration::seconds(60.0);
    cfg.initial_step = time::Duration::seconds(60.0);

    propagation::DormandPrince54Propagator propagator{forces, cfg};

    propagation::PropagationState initial{};
    initial.state.position = Vec3{7.0e6, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, trajectory::circular_speed(kGm, 7.0e6), 0.0};

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::hours(1.0));

    CHECK(!result.ok());
    CHECK(result.status == propagation::PropagationStatus::MinimumStepReached);
    INFO("reported: " + result.message);

    // And the partial result is still usable: it says where it got to.
    CHECK(result.time >= t0);
    CHECK(result.time < t0 + time::Duration::hours(1.0));
}

TEST(the_step_budget_is_enforced) {
    sft::FixedPointMassProvider provider{kCentre, kGm, 0.0};
    const std::vector<celestial::BodyId> ids{kCentre};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);
    const gravity::PointMassGravity forces{provider, catalog,
                                           coordinates::ReferenceFrame::ssb_j2000()};

    propagation::IntegratorConfig cfg{};
    cfg.max_steps = 5;
    cfg.max_step = time::Duration::seconds(1.0);
    cfg.initial_step = time::Duration::seconds(1.0);

    propagation::DormandPrince54Propagator propagator{forces, cfg};

    propagation::PropagationState initial{};
    initial.state.position = Vec3{7.0e6, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, trajectory::circular_speed(kGm, 7.0e6), 0.0};

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::hours(1.0));

    CHECK(result.status == propagation::PropagationStatus::MaxStepsExceeded);
    CHECK(result.stats.accepted_steps <= 5);
}

TEST(the_propagator_lands_exactly_on_the_requested_epoch) {
    sft::FixedPointMassProvider provider{kCentre, kGm, 0.0};
    const std::vector<celestial::BodyId> ids{kCentre};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);
    const gravity::PointMassGravity forces{provider, catalog,
                                           coordinates::ReferenceFrame::ssb_j2000()};

    propagation::DormandPrince54Propagator propagator{forces, propagation::IntegratorConfig{}};

    propagation::PropagationState initial{};
    initial.state.position = Vec3{7.0e6, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, trajectory::circular_speed(kGm, 7.0e6), 0.0};

    const auto t0 = time::CoordinateTime::from_seconds_since_j2000(820497600.0);
    for (const double dt : {1.0, 137.5, 3600.0, 86400.0 + 0.25}) {
        const auto target = t0 + time::Duration::seconds(dt);
        const auto result = propagator.propagate(initial, t0, target);
        REQUIRE(result.ok());
        CHECK(result.time == target);
        CHECK_NEAR_ABS((result.time - target).seconds(), 0.0, 0.0,
                       "the final step is clipped to land on the requested epoch, and the two-part "
                       "CoordinateTime makes that landing exact rather than approximate");
    }
}

TEST(proper_time_tracks_coordinate_time_in_the_newtonian_regime) {
    sft::FixedPointMassProvider provider{kCentre, kGm, 0.0};
    const std::vector<celestial::BodyId> ids{kCentre};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);
    const gravity::PointMassGravity forces{provider, catalog,
                                           coordinates::ReferenceFrame::ssb_j2000()};

    propagation::DormandPrince54Propagator propagator{forces, propagation::IntegratorConfig{}};

    propagation::PropagationState initial{};
    initial.state.position = Vec3{7.0e6, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, trajectory::circular_speed(kGm, 7.0e6), 0.0};

    const auto t0 = time::CoordinateTime::j2000();
    const double dt = 3600.0;
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::seconds(dt));
    REQUIRE(result.ok());

    // Milestone 0 integrates dtau/dt = 1.  When Milestone 4 replaces that with
    // 1/gamma, this test becomes the Newtonian-limit check: at 7.5 km/s the
    // relativistic difference is (v/c)^2/2 ~ 3.1e-10, i.e. 1.1e-6 s per hour.
    CHECK_NEAR_ABS(result.state.proper_time.seconds(), dt, 1.0e-9,
                   "dtau/dt = 1 exactly in the Newtonian regime; the residual is the accumulation "
                   "of the integrator's sum over ~100 steps of a constant derivative, which is "
                   "exact to rounding");
}
