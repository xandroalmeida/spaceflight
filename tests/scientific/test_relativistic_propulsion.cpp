// Special-relativistic propulsion against closed-form solutions.
//
// Everything here runs in FLAT SPACETIME, with no gravity at all -- which is not
// a convenience but the stated domain of validity
// (docs/physics/relativistic-propulsion.md section 8). The rocket equation and
// hyperbolic motion are exact there, so every deviation is integration error.

#include "core/gravity/force_model.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/propulsion/main_engine_force.hpp"
#include "core/relativity/kinematics.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/units/conversions.hpp"
#include "core/units/constants.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>
#include <vector>

using namespace sf;
using sf::math::Vec3;
using sf::propagation::Kinematics;

namespace {

constexpr double c = units::c;

// Constant thrust in a fixed direction, with no mass loss: the idealisation that
// makes the proper acceleration constant and the motion exactly hyperbolic. A
// real engine cannot do this (F/m0 grows as the tank empties), which is why the
// rocket-equation tests use the real one.
class ConstantThrust final : public gravity::ForceModel {
public:
    explicit ConstantThrust(Vec3 thrust) : thrust_(thrust) {}
    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState&,
                                                time::CoordinateTime) const override {
        gravity::ForceResult result{};
        result.proper_thrust = thrust_;
        return result;
    }
    [[nodiscard]] std::string_view name() const override { return "ConstantThrust"; }

private:
    Vec3 thrust_;
};

class ConstantGravity final : public gravity::ForceModel {
public:
    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState&,
                                                time::CoordinateTime) const override {
        gravity::ForceResult result{};
        result.acceleration = Vec3{0.0, -9.80665, 0.0};
        return result;
    }
    [[nodiscard]] std::string_view name() const override { return "ConstantGravity"; }
};

propagation::IntegratorConfig relativistic_config() {
    propagation::IntegratorConfig cfg{};
    cfg.kinematics = Kinematics::SpecialRelativistic;
    cfg.relative_tolerance = 1.0e-13;
    cfg.absolute_tolerance_position = 1.0e-3;
    cfg.absolute_tolerance_velocity = 1.0e-6;
    cfg.absolute_tolerance_mass = 1.0e-9;
    cfg.initial_step = time::Duration::seconds(1.0);
    cfg.max_step = time::Duration::days(1.0);
    return cfg;
}

}  // namespace

TEST(the_kinematic_conversions_hold_across_the_whole_ladder) {
    // The beta ladder of rule 31, with gamma from the closed form as the
    // reference and gamma from u as the thing under test.
    const std::vector<double> ladder{0.01, 0.1, 0.5, 0.9, 0.99, 0.999};

    for (const double beta : ladder) {
        const double gamma_reference = 1.0 / std::sqrt(1.0 - beta * beta);
        const double rapidity = std::atanh(beta);
        const Vec3 u = relativity::proper_velocity_from_rapidity(Vec3::unit_x(), rapidity);

        std::ostringstream os;
        os << "beta = " << beta << ": gamma from u = " << relativity::lorentz_factor(u)
           << ", closed form " << gamma_reference << ", u/c = " << u.norm() / c
           << ", rapidity " << rapidity;
        INFO(os.str());

        CHECK_NEAR_REL(relativity::lorentz_factor(u), gamma_reference, 1.0e-14,
                       "gamma = sqrt(1 + (u/c)^2) against 1/sqrt(1 - beta^2). Both are exact "
                       "expressions of the same number; the first has no cancellation and the "
                       "second loses digits as beta -> 1, which is why the state carries u");
        CHECK_NEAR_REL(relativity::beta(u), beta, 1.0e-14, "beta recovered from u");
        CHECK_NEAR_REL(relativity::rapidity(u), rapidity, 1.0e-14,
                       "phi = asinh(u/c) inverts u = c sinh(phi)");
        CHECK(relativity::beta(u) < 1.0);

        // Round trip through the coordinate velocity.
        const Vec3 v = relativity::coordinate_velocity(u);
        CHECK_NEAR_REL(relativity::proper_velocity(v).norm(), u.norm(), 1.0e-12,
                       "v = u/gamma and u = gamma*v are inverses; the residual grows towards "
                       "beta = 1 because the SECOND one cancels, which is the documented price of "
                       "stating a state by its coordinate velocity");
    }

    // The low-speed end, where the naive gamma - 1 collapses.
    const Vec3 slow = relativity::proper_velocity(Vec3{1.0, 0.0, 0.0});
    CHECK_EQ(relativity::lorentz_factor(slow) - 1.0, 0.0);
    CHECK_NEAR_REL(relativity::lorentz_factor_minus_one(slow), 0.5 / (c * c), 1.0e-12,
                   "at 1 m/s, gamma - 1 = 5.6e-18: subtracting gives exactly zero, and "
                   "(u/c)^2/(gamma+1) gives every digit");
}

TEST(constant_proper_acceleration_reproduces_hyperbolic_motion) {
    // The textbook case, and the one that checks u, beta, position and proper
    // time all at once. With du/dt = a constant:
    //     u(t)    = a t
    //     gamma   = sqrt(1 + (a t/c)^2)
    //     x(t)    = (c^2/a)(gamma - 1)
    //     tau(t)  = (c/a) asinh(a t/c)
    const double mass = 1000.0;
    const double a = 9.80665;  // one g
    const ConstantThrust forces{Vec3{a * mass, 0.0, 0.0}};

    propagation::DormandPrince54Propagator propagator{forces, relativistic_config()};

    propagation::PropagationState initial{};
    initial.mass = mass;

    const auto t0 = time::CoordinateTime::j2000();
    const double year = 365.25 * 86400.0;

    for (const double years : {0.01, 0.1, 1.0}) {
        const double dt = years * year;
        const auto result = propagator.propagate(initial, t0, t0 + time::Duration::seconds(dt));
        REQUIRE(result.ok());

        const double at_over_c = a * dt / c;
        const double gamma = std::sqrt(1.0 + at_over_c * at_over_c);
        const double expected_beta = at_over_c / gamma;
        const double expected_x = (c * c / a) * (gamma - 1.0);
        const double expected_tau = (c / a) * std::asinh(at_over_c);

        const double beta = result.state.state.velocity.norm() / c;

        std::ostringstream os;
        os << years << " yr at 1 g: beta = " << beta << " (exact " << expected_beta
           << "), x = " << result.state.state.position.x << " m (exact " << expected_x
           << "), tau = " << result.state.proper_time.seconds() / year << " yr (exact "
           << expected_tau / year << ")";
        INFO(os.str());

        CHECK_NEAR_REL(beta, expected_beta, 1.0e-11,
                       "beta = (at/c)/sqrt(1+(at/c)^2) is the exact solution of du/dt = a; the "
                       "residual is the integrator at rtol = 1e-13");
        CHECK_NEAR_REL(result.state.state.position.x, expected_x, 1.0e-10,
                       "x = (c^2/a)(gamma - 1), the integral of v = u/gamma");
        CHECK_NEAR_REL(result.state.proper_time.seconds(), expected_tau, 1.0e-11,
                       "tau = (c/a) asinh(at/c), the integral of dtau/dt = 1/gamma. This is the "
                       "twin paradox as an ODE: after a year of coordinate time the ship's clock "
                       "reads less, by a factor the same equation predicts");
        CHECK(beta < 1.0);
    }
}

TEST(the_relativistic_rocket_equation_holds_at_every_rung) {
    // Delta-phi = (eta w / c) ln(m0/m1), independent of the burn profile and of
    // the starting speed -- because rapidity is additive and velocity is not.
    const double w_fraction = 0.1;
    const double efficiency = 0.98;
    const propulsion::EngineSpec engine{"torch", 1.0, w_fraction, efficiency};
    const spacecraft::Spacecraft ship{"probe", 1000.0, 9000.0, engine};

    propulsion::MainEngineForce main_engine{ship};
    main_engine.set_throttle(1.0);

    propagation::DormandPrince54Propagator propagator{main_engine, relativistic_config()};

    for (const double start_beta : {0.0, 0.5, 0.9}) {
        propagation::PropagationState initial{};
        initial.mass = ship.initial_mass();
        if (start_beta > 0.0) {
            initial.state.velocity = relativity::coordinate_velocity(
                relativity::proper_velocity_from_rapidity(Vec3::unit_x(),
                                                          std::atanh(start_beta)));
        }

        const double burn_seconds = 2000.0;  // q = 1 kg/s
        const auto t0 = time::CoordinateTime::j2000();
        const auto result =
            propagator.propagate(initial, t0, t0 + time::Duration::seconds(burn_seconds));
        REQUIRE(result.ok());

        // The burn is measured in PROPER time: dm0/dtau = -q.
        const double mass_after = result.state.mass;
        const double predicted_delta_phi =
            efficiency * w_fraction * std::log(ship.initial_mass() / mass_after);

        const double phi_before = std::atanh(start_beta);
        const double phi_after = relativity::rapidity(
            relativity::proper_velocity(result.state.state.velocity));

        std::ostringstream os;
        os << "from beta = " << start_beta << ": burnt "
           << (ship.initial_mass() - mass_after) << " kg, delta-phi = " << phi_after - phi_before
           << " (predicted " << predicted_delta_phi << "), final beta = "
           << result.state.state.velocity.norm() / c;
        INFO(os.str());

        CHECK_NEAR_REL(phi_after - phi_before, predicted_delta_phi, 1.0e-9,
                       "the rocket equation in rapidity. Note what this test does NOT need to "
                       "know: how long the burn took in coordinate time, or what the starting "
                       "speed was. Rapidity is additive, which is exactly why the relativistic "
                       "rocket equation is written in it");
        CHECK(result.state.state.velocity.norm() / c < 1.0);
    }
}

TEST(the_speed_limit_is_structural_and_no_clamp_exists) {
    // An absurd burn: mass ratio e^(10/0.098) ~ 1e44. The point is not that the
    // ship could carry it -- it is that the equations cannot produce |v| >= c
    // even when asked to, so there is nowhere a clamp would go (rule 13).
    const double w_fraction = 0.1;
    const double efficiency = 0.98;
    const propulsion::EngineSpec engine{"absurd", 1.0, w_fraction, efficiency};

    const double target_phi = 10.0;
    const double expected_beta = std::tanh(target_phi);

    // Rather than burn 1e44 kg, integrate the kinematics directly: the state IS
    // u, so a rapidity of 10 is just a large finite number.
    const Vec3 u = relativity::proper_velocity_from_rapidity(Vec3::unit_x(), target_phi);

    std::ostringstream os;
    os << "rapidity 10 -> u/c = " << u.norm() / c << ", beta = " << relativity::beta(u)
       << " (tanh(10) = " << expected_beta << "), gamma = " << relativity::lorentz_factor(u);
    INFO(os.str());

    CHECK_NEAR_REL(relativity::beta(u), expected_beta, 1.0e-14,
                   "beta = tanh(phi) by construction of u = c sinh(phi)");
    CHECK(relativity::beta(u) < 1.0);

    // Where the DOUBLE gives out, which is not where the physics does.
    //
    // beta = 1 - 1/(2 gamma^2), so 1 - beta drops below half an ulp of 1
    // (1.11e-16) at gamma ~ 6.7e7. Past that, v = u/gamma rounds to exactly c and
    // beta reads 1.0 -- not because anything overflowed or was clamped, but
    // because the coordinate velocity of such a state is not a distinguishable
    // double. Measured: at u/c = 6.7e7, 1 - beta = 1.11e-16; at 1e8, zero.
    //
    // The state variable is unaffected: u, gamma and the rapidity are all exact
    // there, and every equation the propagator integrates is written in u. This
    // is the strongest argument for the choice of variable, and it only shows up
    // when you go looking.
    for (const double u_over_c : {1.0e7, 1.0e8, 1.0e9}) {
        const Vec3 extreme = Vec3::unit_x() * (u_over_c * c);
        std::ostringstream detail;
        detail << "u/c = " << u_over_c << ": gamma = " << relativity::lorentz_factor(extreme)
               << ", rapidity = " << relativity::rapidity(extreme) << ", 1 - beta = "
               << 1.0 - relativity::beta(extreme);
        INFO(detail.str());

        CHECK(relativity::beta(extreme) <= 1.0);
        CHECK_NEAR_REL(relativity::lorentz_factor(extreme), u_over_c, 1.0e-14,
                       "gamma = sqrt(1 + (u/c)^2) -> u/c for large u, computed without ever "
                       "evaluating 1/sqrt(1 - beta^2), which here would be 1/sqrt of a number "
                       "that rounds to zero");
        CHECK_NEAR_REL(relativity::rapidity(extreme), std::asinh(u_over_c), 1.0e-14,
                       "and the rapidity stays exact, which is the coordinate that never "
                       "saturates");
    }
}

TEST(thrust_perpendicular_to_the_motion_is_weaker_by_gamma) {
    // The anisotropy of section 2.3: the SAME force gives du/dt = F/m0 along the
    // motion and F/(m0 gamma) across it. A model that ignored this would pass
    // every longitudinal test and steer wrongly by a factor of gamma.
    const double mass = 1000.0;
    const double thrust = 1.0e4;
    const double beta = 0.9;
    const double gamma = 1.0 / std::sqrt(1.0 - beta * beta);

    const Vec3 u0 = relativity::proper_velocity_from_rapidity(Vec3::unit_x(), std::atanh(beta));
    const double dt = 1.0;

    auto run = [&](const Vec3& direction) {
        const ConstantThrust forces{direction * thrust};
        propagation::DormandPrince54Propagator propagator{forces, relativistic_config()};
        propagation::PropagationState initial{};
        initial.mass = mass;
        initial.state.velocity = relativity::coordinate_velocity(u0);
        const auto t0 = time::CoordinateTime::j2000();
        const auto result = propagator.propagate(initial, t0, t0 + time::Duration::seconds(dt));
        return relativity::proper_velocity(result.state.state.velocity);
    };

    const Vec3 along = run(Vec3::unit_x());
    const Vec3 across = run(Vec3::unit_y());

    const double du_parallel = (along - u0).norm();
    const double du_transverse = (across - u0).norm();

    std::ostringstream os;
    os << "at beta = " << beta << " (gamma = " << gamma << "), the same " << thrust
       << " N for " << dt << " s gives du = " << du_parallel << " m/s along the motion and "
       << du_transverse << " m/s across it; ratio " << du_parallel / du_transverse;
    INFO(os.str());

    CHECK_NEAR_REL(du_parallel, thrust / mass * dt, 1.0e-6,
                   "longitudinal: du/dt = F/m0, the proper acceleration, with no gamma anywhere. "
                   "The bound is loose because u0 was reconstructed through the cancelling "
                   "direction of the conversion");
    CHECK_NEAR_REL(du_parallel / du_transverse, gamma, 1.0e-5,
                   "and the ratio of the two responses is exactly gamma = 2.294 at beta = 0.9");
}

TEST(the_newtonian_limit_is_reached_numerically_not_asserted) {
    // Rule 31: at v << c the relativistic propagator must CONVERGE on the
    // Newtonian one. The two integrate different equations; the claim is that
    // their answers agree to O(beta^2).
    const propulsion::EngineSpec engine{"modest", 1.0, 1.0e-4, 0.5};
    const spacecraft::Spacecraft ship{"probe", 1000.0, 1000.0, engine};

    propulsion::MainEngineForce main_engine{ship};
    main_engine.set_throttle(1.0);

    propagation::PropagationState initial{};
    initial.mass = ship.initial_mass();

    const auto t0 = time::CoordinateTime::j2000();
    const double dt = 100.0;

    auto newtonian_config = relativistic_config();
    newtonian_config.kinematics = Kinematics::Newtonian;

    propagation::DormandPrince54Propagator relativistic{main_engine, relativistic_config()};
    propagation::DormandPrince54Propagator newtonian{main_engine, newtonian_config};

    const auto a = relativistic.propagate(initial, t0, t0 + time::Duration::seconds(dt));
    const auto b = newtonian.propagate(initial, t0, t0 + time::Duration::seconds(dt));
    REQUIRE(a.ok());
    REQUIRE(b.ok());

    const double beta = b.state.state.velocity.norm() / c;
    const double difference =
        std::abs(a.state.state.velocity.norm() - b.state.state.velocity.norm()) /
        b.state.state.velocity.norm();

    std::ostringstream os;
    os << "after " << dt << " s the ship reaches beta = " << beta
       << "; the two kinematics differ by " << difference << " in speed, and by "
       << std::abs(a.state.proper_time.seconds() - b.state.proper_time.seconds()) << " s in "
       << "proper time";
    INFO(os.str());

    CHECK_NEAR_REL(difference, 0.5 * beta * beta, 0.2,
                   "the leading correction is beta^2/2 = 3.3e-6 at this speed. Demanding the two "
                   "be EQUAL would be wrong: they integrate different equations, and the "
                   "Newtonian one is the approximation. 20% covers the next order");

    CHECK_NEAR_REL(a.state.proper_time.seconds(), dt * (1.0 - 0.5 * beta * beta / 3.0), 1.0e-5,
                   "proper time under constant proper acceleration from rest over a short burn: "
                   "tau = integral dt/gamma with beta growing linearly, so the deficit is a third "
                   "of the instantaneous one");
    CHECK_NEAR_ABS(b.state.proper_time.seconds(), dt, 1.0e-9,
                   "dtau/dt = 1 identically in the Newtonian regime, so tau is the integral of a "
                   "constant; the residual is the accumulation of ~100 steps of the integrator's "
                   "own sum, not physics");
}

TEST(relativistic_kinematics_refuses_to_carry_a_gravity_field) {
    // The refusal of section 8: flat-spacetime dynamics plus a Newtonian field is
    // a valid approximation bolted to an invalid one, and the error is silent.
    const ConstantGravity gravity;
    propagation::DormandPrince54Propagator propagator{gravity, relativistic_config()};

    propagation::PropagationState initial{};
    initial.mass = 1000.0;

    const auto t0 = time::CoordinateTime::j2000();
    const auto refused = propagator.propagate(initial, t0, t0 + time::Duration::seconds(10.0));

    CHECK(!refused.ok());
    CHECK(refused.status == propagation::PropagationStatus::UnsupportedRegime);
    INFO("reported: " + refused.message);

    // Opting in is possible, and then the caller owns the claim.
    auto permissive = relativistic_config();
    permissive.allow_gravity_with_relativistic_kinematics = true;
    propagation::DormandPrince54Propagator allowed{gravity, permissive};
    const auto accepted = allowed.propagate(initial, t0, t0 + time::Duration::seconds(10.0));
    CHECK(accepted.ok());
    CHECK_NEAR_REL(accepted.state.state.velocity.y, -9.80665 * 10.0, 1.0e-9,
                   "at these speeds the two kinematics agree to 1e-15, so the opted-in answer is "
                   "the Newtonian one -- which is the whole point: the approximation is fine HERE, "
                   "and the refusal exists because it stops being fine without warning");
}


TEST(burning_the_whole_tank_in_cruise_mode_reaches_the_predicted_beta) {
    // The payoff. A 1-tonne hull with 19 tonnes of propellant and an exhaust
    // velocity of 0.5c: the relativistic rocket equation says this reaches
    //
    //     phi = (eta w/c) ln(m0/m1) = 0.5 * ln(20) = 1.49787
    //     beta = tanh(phi) = 0.904762
    //
    // and it takes 8 years of burning to get there, which is not an artefact of
    // the model but the cost of finite thrust. Both are checked.
    const propulsion::MultiModeEngine engine{
        {{"IMPULSE", propulsion::EngineSpec{"IMPULSE", 0.0222376, 0.03, 1.0}},
         {"CRUISE", propulsion::EngineSpec{"CRUISE", 7.470950e-05, 0.5, 1.0}}}};

    spacecraft::Spacecraft ship{"torch", 1000.0, 19000.0, engine};
    ship.select_mode("CRUISE");

    propulsion::MainEngineForce main_engine{ship};
    main_engine.set_throttle(1.0);

    auto cfg = relativistic_config();
    cfg.max_step = time::Duration::days(30.0);
    propagation::DormandPrince54Propagator propagator{main_engine, cfg};

    propagation::PropagationState initial{};
    initial.mass = ship.initial_mass();

    const auto t0 = time::CoordinateTime::j2000();
    // Long enough for the tank to run dry: the burn lasts 2.54e8 s of PROPER
    // time, and coordinate time runs longer as gamma climbs.
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::seconds(6.0e8));
    REQUIRE(result.ok());

    const double year = 365.25 * 86400.0;
    const double mass_ratio = ship.initial_mass() / result.state.mass;
    const double predicted_phi = 0.5 * std::log(mass_ratio);
    const double predicted_beta = std::tanh(predicted_phi);
    const double beta = result.state.state.velocity.norm() / c;

    std::ostringstream os;
    os << "after " << 6.0e8 / year << " yr of coordinate time: mass " << result.state.mass
       << " kg (ratio " << mass_ratio << "), beta = " << beta << " (predicted " << predicted_beta
       << "), gamma = " << relativity::lorentz_factor(
                                relativity::proper_velocity(result.state.state.velocity))
       << ", proper time " << result.state.proper_time.seconds() / year << " yr, distance "
       << result.state.state.position.x / 9.4607e15 << " light years";
    INFO(os.str());

    CHECK_NEAR_REL(result.state.mass, ship.dry_mass(), 1.0e-9,
                   "the tank runs dry and thrust stops with no special case: q = 0 when there is "
                   "nothing left to consume");
    CHECK_NEAR_REL(beta, std::tanh(0.5 * std::log(20.0)), 1.0e-6,
                   "the relativistic rocket equation with the full 20:1 mass ratio. The residual "
                   "is the integrator plus the moment the tank empties mid-step");
    CHECK(beta > 0.9);
    CHECK(beta < 1.0);

    // The ship's clock ran slow: this is the twin paradox with a real engine.
    CHECK(result.state.proper_time.seconds() < 6.0e8);
    const double dilation = result.state.proper_time.seconds() / 6.0e8;
    std::ostringstream clock;
    clock << "the ship's clock read " << dilation << " of the coordinate elapsed time";
    INFO(clock.str());
    CHECK(dilation < 0.95);
    CHECK(dilation > 0.5);
}

TEST(impulse_mode_cannot_reach_relativistic_speed_and_says_so_in_the_arithmetic) {
    // The other half of the trade: the high-thrust mode has the same tank and
    // gets nowhere near, because delta-phi is (w/c) ln(ratio) and w is 16.7x
    // smaller. No amount of thrust fixes an exhaust velocity.
    const propulsion::EngineSpec impulse{"IMPULSE", 0.0222376, 0.03, 1.0};
    const spacecraft::Spacecraft ship{"torch", 1000.0, 19000.0, impulse};

    const double phi = 0.03 * std::log(20.0);
    const double reachable = std::tanh(phi);

    std::ostringstream os;
    os << "impulse mode, same 20:1 tank: phi = " << phi << " -> beta = " << reachable
       << " (cruise reaches " << std::tanh(0.5 * std::log(20.0)) << ")";
    INFO(os.str());

    CHECK_NEAR_REL(reachable, 0.089631, 1.0e-5,
                   "tanh(0.0899). The two modes differ by a factor 16.7 in exhaust velocity and "
                   "therefore in rapidity, which is a factor 10 in speed at these values");
    CHECK_NEAR_REL(ship.delta_v_budget(ship.initial_mass()) / c, phi, 1.0e-12,
                   "and the Newtonian budget divided by c IS the rapidity, which is why the two "
                   "agree here and would not at 0.5c");
}
