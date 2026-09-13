// Lambert's problem, checked against propagation rather than against a table.
//
// The strong test is a round trip through a DIFFERENT piece of code: Lambert says
// "leave r1 with this velocity and you arrive at r2 after dt"; the Kepler f-and-g
// solution (for elliptic arcs) and the numerical propagator (for any conic) are
// asked whether that is true.  See docs/physics/lambert.md section 4.

#include "core/celestial/body_catalog.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/navigation/trajectory_planner.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/lambert.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/analytic_ephemeris.hpp"
#include "tests/support/kepler.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>
#include <vector>

using namespace sf;
using sf::coordinates::StateVector;
using sf::math::Vec3;
using sf::trajectory::LambertDegenerate;
using sf::trajectory::solve_lambert;
using sf::trajectory::TransferDirection;

namespace {

constexpr double kGm = 3.9860043550702266e14;
const celestial::BodyId kEarth = celestial::bodies::earth;

// Propagates a state numerically under a single point mass -- an entirely
// different code path from the Lagrange f and g functions.
StateVector numerical_propagate(const StateVector& start, double dt) {
    sft::FixedPointMassProvider provider{kEarth, kGm, 0.0};
    const std::vector<celestial::BodyId> ids{kEarth};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);
    const gravity::PointMassGravity forces{provider, catalog,
                                           coordinates::ReferenceFrame::ssb_j2000()};

    propagation::IntegratorConfig cfg{};
    cfg.relative_tolerance = 1.0e-13;
    cfg.absolute_tolerance_position = 1.0e-7;
    cfg.absolute_tolerance_velocity = 1.0e-10;
    cfg.max_step = time::Duration::seconds(300.0);

    propagation::DormandPrince54Propagator propagator{forces, cfg};
    propagation::PropagationState initial{};
    initial.state = start;

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::seconds(dt));
    return result.state.state;
}

}  // namespace

TEST(stumpff_functions_are_continuous_through_zero) {
    CHECK_NEAR_ABS(trajectory::stumpff_c(0.0), 0.5, 0.0,
                   "C(0) = 1/2 is the limit of (1-cos sqrt(psi))/psi, taken analytically by the "
                   "series branch; no rounding is involved");
    CHECK_NEAR_ABS(trajectory::stumpff_s(0.0), 1.0 / 6.0, 0.0, "S(0) = 1/6, same argument");

    // Exact closed-form values at psi = pi^2.
    const double psi = units::pi * units::pi;
    CHECK_NEAR_REL(trajectory::stumpff_c(psi), 2.0 / psi, 1.0e-15,
                   "C(pi^2) = (1 - cos pi)/pi^2 = 2/pi^2 exactly");
    CHECK_NEAR_REL(trajectory::stumpff_s(psi), 1.0 / psi, 1.0e-15,
                   "S(pi^2) = (pi - sin pi)/(pi^2)^{3/2} = pi/pi^3 = 1/pi^2 exactly");

    // The series and closed-form branches must agree where they meet, or the
    // solver would see a jump in dt(psi) and the bisection would misbehave.
    for (const double eps : {1.0e-6, -1.0e-6}) {
        const double from_series = trajectory::stumpff_c(eps * 0.99);
        const double from_closed = trajectory::stumpff_c(eps * 1.01);

        std::ostringstream os;
        os << "branch agreement at psi = " << eps << ": series " << from_series << ", closed form "
           << from_closed << ", difference " << std::abs(from_series - from_closed);
        INFO(os.str());

        CHECK_NEAR_REL(from_series, from_closed, 1.0e-7,
                       "the two branches straddle the threshold psi = 1e-6. Two effects set the "
                       "bound. First, C genuinely varies: dC/dpsi = -1/24, so the 2e-8 gap in psi "
                       "is 1.7e-9 relative. Second, and larger, the CLOSED FORM is inaccurate "
                       "here: (1 - cos(1e-3)) subtracts two numbers differing by 5e-7, losing "
                       "about seven digits -- which is the entire reason the series branch exists "
                       "(docs/physics/lambert.md section 2). 1e-7 covers both; a disagreement "
                       "above that would mean the series coefficients are wrong");
    }
}

TEST(lambert_recovers_the_velocity_that_generated_the_arc) {
    // Take a known orbit, sample two points on it, and ask Lambert to reconstruct
    // the connecting arc.  The answer is known exactly: it is the orbit we started
    // from.
    const double a = 1.2e7;
    const double e = 0.35;
    const double rp = a * (1.0 - e);
    const double vp = std::sqrt(kGm * (1.0 + e) / (a * (1.0 - e)));
    const double period = 2.0 * units::pi * std::sqrt(a * a * a / kGm);

    const StateVector start{Vec3{rp, 0.0, 0.0}, Vec3{0.0, vp * 0.9, vp * 0.435889894}};

    for (const double fraction : {0.1, 0.3, 0.45, 0.7, 0.9}) {
        const double dt = fraction * period;
        const StateVector arrival = sft::kepler_propagate(start, kGm, dt);

        const auto solution = solve_lambert(start.position, arrival.position,
                                            time::Duration::seconds(dt), kGm);

        const double departure_error =
            (solution.departure_velocity - start.velocity).norm() / start.velocity.norm();
        const double arrival_error =
            (solution.arrival_velocity - arrival.velocity).norm() / arrival.velocity.norm();

        std::ostringstream os;
        os << "dt = " << fraction << " T, transfer angle "
           << units::rad_to_deg(solution.transfer_angle) << " deg, " << solution.iterations
           << " iterations: departure error " << departure_error << ", arrival error "
           << arrival_error;
        INFO(os.str());

        CHECK_NEAR_ABS(departure_error, 0.0, 1.0e-9,
                       "Lambert must reconstruct the exact velocity that produced the arc. The "
                       "residual is the bisection tolerance on the time of flight (1e-10 relative) "
                       "propagated into the velocity, which for these arcs is of the same order");
        CHECK_NEAR_ABS(arrival_error, 0.0, 1.0e-9, "same argument at the far end");

        CHECK_NEAR_REL(solution.semi_major_axis, a, 1.0e-9,
                       "the reconstructed arc must lie on the orbit it was sampled from");
        CHECK_NEAR_REL(solution.achieved_time_of_flight, dt, 1.0e-10,
                       "the bisection's own convergence criterion, verified from the outside");
    }
}

TEST(lambert_arcs_actually_arrive_where_they_promise) {
    // The same check run through the numerical propagator: an independent code
    // path, and one that works for hyperbolic arcs too.
    const Vec3 r1{7.0e6, 0.0, 0.0};
    const Vec3 r2{-2.0e6, 9.0e6, 1.0e6};

    for (const double hours : {0.5, 1.0, 2.0, 4.0}) {
        const double dt = hours * 3600.0;
        const auto solution = solve_lambert(r1, r2, time::Duration::seconds(dt), kGm);

        const StateVector arrival =
            numerical_propagate(StateVector{r1, solution.departure_velocity}, dt);
        const double miss = (arrival.position - r2).norm();

        std::ostringstream os;
        os << hours << " h transfer (a = " << solution.semi_major_axis << " m, "
           << (solution.semi_major_axis > 0.0 ? "elliptic" : "hyperbolic")
           << "): departure speed " << solution.departure_velocity.norm() << " m/s, miss distance "
           << miss << " m";
        INFO(os.str());

        CHECK_NEAR_ABS(miss, 0.0, 1.0e-2,
                       "the miss distance combines the bisection tolerance (1e-10 of the time of "
                       "flight, worth ~1e-3 m of along-track position at these speeds) with the "
                       "integrator's global error at rtol = 1e-13. 1e-2 m over a 9e6 m transfer is "
                       "1e-9 relative");
    }
}

TEST(lambert_reproduces_the_hohmann_transfer) {
    // A Hohmann transfer IS a Lambert solution: half an ellipse between two
    // circular orbits.  At exactly 180 degrees the problem is degenerate, so we
    // ask for 179.9 and expect the answer to be within the difference that makes.
    const double r1 = 7.0e6;
    const double r2 = 4.2164e7;
    const auto hohmann = navigation::plan_hohmann(kGm, r1, r2);

    const double angle = units::deg_to_rad(179.9);
    const Vec3 departure{r1, 0.0, 0.0};
    const Vec3 arrival{r2 * std::cos(angle), r2 * std::sin(angle), 0.0};

    const auto solution =
        solve_lambert(departure, arrival, time::Duration::seconds(hohmann.transfer_time), kGm);

    const double v_circular_1 = std::sqrt(kGm / r1);
    const double delta_v_departure = (solution.departure_velocity - Vec3{0.0, v_circular_1, 0.0}).norm();

    std::ostringstream os;
    os << "Hohmann: dv1 = " << hohmann.delta_v_departure << " m/s, dv2 = " << hohmann.delta_v_arrival
       << " m/s, transfer " << hohmann.transfer_time / 3600.0 << " h; Lambert at 179.9 deg gives "
       << "dv1 = " << delta_v_departure << " m/s";
    INFO(os.str());

    CHECK_NEAR_REL(delta_v_departure, hohmann.delta_v_departure, 5.0e-3,
                   "Lambert is asked for a 179.9 degree transfer in the same flight time as the "
                   "180 degree Hohmann, so the arc is slightly different and the delta-v differs "
                   "by a comparable relative amount. 0.5% covers that 0.1 degree; anything larger "
                   "would mean the two formulations disagree about the physics rather than about "
                   "the geometry");

    CHECK_NEAR_REL(solution.semi_major_axis, hohmann.transfer_semi_major_axis, 5.0e-3,
                   "the transfer ellipse is the same conic, up to the 0.1 degree of geometry");
}

TEST(lambert_refuses_the_geometries_that_have_no_unique_answer) {
    const Vec3 r1{7.0e6, 0.0, 0.0};

    // Exactly antipodal: every plane containing both points is a valid transfer
    // plane.  This is a real property of the problem, not a numerical limitation,
    // and the error message has to say so.
    CHECK_THROWS_AS(solve_lambert(r1, Vec3{-7.0e6, 0.0, 0.0}, time::Duration::hours(1.0), kGm),
                    LambertDegenerate);

    // Same point: zero transfer angle.
    CHECK_THROWS_AS(solve_lambert(r1, r1, time::Duration::hours(1.0), kGm), LambertDegenerate);
    CHECK_THROWS_AS(solve_lambert(Vec3{}, r1, time::Duration::hours(1.0), kGm), LambertDegenerate);

    // Nonsense inputs.
    CHECK_THROWS_AS(solve_lambert(r1, Vec3{0.0, 7.0e6, 0.0}, time::Duration::seconds(0.0), kGm),
                    std::invalid_argument);
    CHECK_THROWS_AS(solve_lambert(r1, Vec3{0.0, 7.0e6, 0.0}, time::Duration::hours(1.0), 0.0),
                    std::invalid_argument);

    // Just off antipodal is fine -- and that is what the planner must ask for.
    const double angle = units::deg_to_rad(179.5);
    const Vec3 nearly_opposite{7.0e6 * std::cos(angle), 7.0e6 * std::sin(angle), 0.0};
    const auto solution = solve_lambert(r1, nearly_opposite, time::Duration::hours(1.5), kGm);
    CHECK(solution.departure_velocity.norm() > 0.0);
    CHECK(std::isfinite(solution.semi_major_axis));
}

TEST(the_retrograde_branch_is_the_long_way_round) {
    const Vec3 r1{7.0e6, 0.0, 0.0};
    const Vec3 r2{0.0, 7.0e6, 0.0};
    const auto dt = time::Duration::minutes(45.0);

    const auto prograde = solve_lambert(r1, r2, dt, kGm, TransferDirection::Prograde);
    const auto retrograde = solve_lambert(r1, r2, dt, kGm, TransferDirection::Retrograde);

    std::ostringstream os;
    os << "prograde arc " << units::rad_to_deg(prograde.transfer_angle) << " deg at "
       << prograde.departure_velocity.norm() << " m/s, a = " << prograde.semi_major_axis
       << " m; retrograde arc " << units::rad_to_deg(retrograde.transfer_angle) << " deg at "
       << retrograde.departure_velocity.norm() << " m/s, a = " << retrograde.semi_major_axis << " m";
    INFO(os.str());

    CHECK_NEAR_ABS(prograde.transfer_angle + retrograde.transfer_angle, units::two_pi, 1.0e-12,
                   "the two branches are the short and long way round the same pair of points, so "
                   "their transfer angles sum to 2*pi exactly");

    // Covering 270 degrees instead of 90 in the SAME time needs a higher mean
    // motion, and by Kepler's third law that means a SMALLER semi-major axis --
    // which, by vis-viva at a fixed radius, means a lower speed at departure, not
    // a higher one.  (The first version of this test asserted the opposite from
    // intuition; the solver was right.)
    CHECK(retrograde.semi_major_axis < prograde.semi_major_axis);
    CHECK(retrograde.departure_velocity.norm() < prograde.departure_velocity.norm());

    // And both actually arrive.
    for (const auto& solution : {prograde, retrograde}) {
        const StateVector arrival =
            numerical_propagate(StateVector{r1, solution.departure_velocity}, dt.seconds());
        CHECK_NEAR_ABS((arrival.position - r2).norm(), 0.0, 1.0e-2,
                       "same round-trip bound as the previous test");
    }
}
