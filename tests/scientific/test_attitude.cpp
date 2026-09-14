// Rigid body rotation against closed-form results.
//
// Torque-free motion has exact invariants and two analytic signatures -- the
// precession rate of a symmetric top, and the growth rate of the intermediate
// axis instability -- neither of which any plausible bug reproduces by accident.
// See docs/physics/attitude.md section 4.

#include "core/attitude/inertia.hpp"
#include "core/attitude/pointing_controller.hpp"
#include "core/attitude/rcs.hpp"
#include "core/celestial/body_catalog.hpp"
#include "core/gravity/composite_force_model.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "tests/support/analytic_ephemeris.hpp"
#include "core/gravity/force_model.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace sf;
using sf::attitude::InertiaTensor;
using sf::math::Quaternion;
using sf::math::Vec3;

namespace {

// Free space, no gravity, no thrust: the only dynamics is rotation.
class TorqueFree final : public gravity::ForceModel {
public:
    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState&,
                                                time::CoordinateTime) const override {
        return {};
    }
    [[nodiscard]] std::string_view name() const override { return "TorqueFree"; }
};

// A constant torque in the body frame, for the impulse test.
class ConstantBodyTorque final : public gravity::ForceModel {
public:
    explicit ConstantBodyTorque(Vec3 torque) : torque_(torque) {}
    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState&,
                                                time::CoordinateTime) const override {
        gravity::ForceResult result{};
        result.torque = torque_;
        return result;
    }
    [[nodiscard]] std::string_view name() const override { return "ConstantBodyTorque"; }

private:
    Vec3 torque_;
};

propagation::IntegratorConfig attitude_config() {
    propagation::IntegratorConfig cfg{};
    cfg.relative_tolerance = 1.0e-12;
    cfg.absolute_tolerance_position = 1.0e-6;
    cfg.absolute_tolerance_velocity = 1.0e-9;
    cfg.absolute_tolerance_orientation = 1.0e-12;
    cfg.absolute_tolerance_angular_velocity = 1.0e-12;
    cfg.initial_step = time::Duration::seconds(0.5);
    cfg.max_step = time::Duration::seconds(30.0);
    return cfg;
}

}  // namespace

TEST(the_inertia_tensor_refuses_impossible_bodies) {
    // The triangle inequalities are what separates a tensor from three numbers.
    CHECK_THROWS_AS(InertiaTensor::principal(1.0, 1.0, 5.0), std::invalid_argument);
    CHECK_THROWS_AS(InertiaTensor::principal(1.0, 0.0, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(InertiaTensor::principal(-1.0, 2.0, 2.0), std::invalid_argument);

    // A thin rod is the marginal case: I1 + I2 = I3 exactly.
    const auto rod = InertiaTensor::principal(1.0, 2.0, 3.0);
    CHECK_NEAR_REL(rod.principal_moments().z, 3.0, 0.0, "read back verbatim");

    // Closed forms, checked against the textbook expressions.
    const auto sphere = InertiaTensor::solid_sphere(10.0, 2.0);
    CHECK_NEAR_REL(sphere.principal_moments().x, 0.4 * 10.0 * 4.0, 1.0e-15,
                   "I = 2mr^2/5 about every axis of a solid sphere");

    const auto box = InertiaTensor::solid_box(12.0, Vec3{1.0, 2.0, 3.0});
    CHECK_NEAR_REL(box.principal_moments().x, 12.0 * (4.0 + 9.0) / 12.0, 1.0e-15,
                   "I_xx = m(b^2 + c^2)/12 for a solid box");

    const auto cylinder = InertiaTensor::solid_cylinder(6.0, 1.0, 4.0);
    CHECK_NEAR_REL(cylinder.principal_moments().z, 0.5 * 6.0 * 1.0, 1.0e-15,
                   "I_zz = mr^2/2 about the axis of a solid cylinder");

    // The inverse is a real inverse.
    const Vec3 torque{3.0, -5.0, 7.0};
    const Vec3 alpha = box.angular_acceleration(torque);
    CHECK_NEAR_ABS((box.tensor() * alpha - torque).norm(), 0.0, 1.0e-12,
                   "I * (I^-1 tau) = tau; the residual is the rounding of the adjugate divided by "
                   "the determinant");
}

TEST(torque_free_rotation_conserves_angular_momentum_and_energy) {
    const auto inertia = InertiaTensor::principal(1200.0, 1800.0, 2400.0);
    const TorqueFree forces;

    propagation::DormandPrince54Propagator propagator{forces, attitude_config()};
    propagator.set_inertia(&inertia);

    propagation::PropagationState initial{};
    initial.mass = 1000.0;
    initial.attitude.orientation = Quaternion::from_axis_angle(Vec3{1.0, 2.0, 3.0}, sf::units::Angle::radians(0.9));
    initial.attitude.angular_velocity = Vec3{0.04, 0.02, 0.05};

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::hours(1.0));
    REQUIRE(result.ok());
    INFO(result.stats.to_string());

    const auto momentum = [&](const propagation::PropagationState& s) {
        return s.attitude.orientation.rotate(inertia.angular_momentum(s.attitude.angular_velocity));
    };

    const Vec3 l0 = momentum(initial);
    const Vec3 l1 = momentum(result.state);
    const double t_before = inertia.rotational_energy(initial.attitude.angular_velocity);
    const double t_after = inertia.rotational_energy(result.state.attitude.angular_velocity);

    std::ostringstream os;
    os << "after 1 h: |L| drift " << std::abs(l1.norm() - l0.norm()) / l0.norm()
       << ", direction drift " << (l1 - l0).norm() / l0.norm() << ", energy drift "
       << std::abs(t_after - t_before) / t_before;
    INFO(os.str());

    CHECK_NEAR_ABS((l1 - l0).norm() / l0.norm(), 0.0, 1.0e-10,
                   "the angular momentum VECTOR is conserved in the inertial frame with no torque; "
                   "the residual is the integrator's own error at rtol = 1e-12 over ~1e2 steps. "
                   "Note this is the vector, not just its magnitude: a bug in the gyroscopic term "
                   "typically preserves |L| while letting its direction wander");
    CHECK_NEAR_ABS(std::abs(t_after - t_before) / t_before, 0.0, 1.0e-10,
                   "rotational kinetic energy is the second exact invariant; same argument");

    // omega itself must NOT be constant -- if it were, the gyroscopic term would
    // be doing nothing and the test above would pass for the wrong reason.
    CHECK((result.state.attitude.angular_velocity - initial.attitude.angular_velocity).norm() >
          1.0e-3);

    std::ostringstream drift;
    drift << "quaternion drift before renormalising: " << result.stats.max_quaternion_drift;
    INFO(drift.str());
    CHECK_NEAR_ABS(result.stats.max_quaternion_drift, 0.0, 1.0e-10,
                   "measured, not assumed (docs/physics/attitude.md section 5): the projection back "
                   "onto the unit sphere is only legitimate while the thing being projected away is "
                   "this small. 1e-10 per step is four orders above what a healthy step produces "
                   "and would fail loudly if the step control ever broke");
}

TEST(a_symmetric_top_precesses_at_the_analytic_rate) {
    // I1 = I2 != I3 and no torque: the transverse part of omega rotates in the
    // BODY frame at Omega = omega_3 (I3 - I1)/I1.
    const double i_a = 1500.0;
    const double i_c = 2400.0;
    const double spin = 0.05;

    const auto inertia = InertiaTensor::principal(i_a, i_a, i_c);
    const TorqueFree forces;

    propagation::DormandPrince54Propagator propagator{forces, attitude_config()};
    propagator.set_inertia(&inertia);

    propagation::PropagationState initial{};
    initial.mass = 1000.0;
    initial.attitude.angular_velocity = Vec3{0.01, 0.0, spin};

    const double predicted = spin * (i_c - i_a) / i_a;
    const double period = units::two_pi / predicted;

    const auto t0 = time::CoordinateTime::j2000();
    const auto quarter = propagator.propagate(initial, t0, t0 + time::Duration::seconds(period / 4.0));
    REQUIRE(quarter.ok());

    const Vec3 w = quarter.state.attitude.angular_velocity;
    std::ostringstream os;
    os << "predicted precession " << predicted << " rad/s (period " << period
       << " s); after a quarter period omega_transverse = (" << w.x << ", " << w.y << ")";
    INFO(os.str());

    // A quarter turn takes (0.01, 0) to (0, 0.01).
    CHECK_NEAR_ABS(w.x, 0.0, 1.0e-9,
                   "after exactly a quarter of the analytic precession period the transverse "
                   "component must have rotated onto the other axis; any error in the rate shows "
                   "up here linearly");
    CHECK_NEAR_REL(w.y, 0.01, 1.0e-9, "and with its magnitude unchanged");
    CHECK_NEAR_REL(w.z, spin, 1.0e-12,
                   "the component along the symmetry axis is exactly constant for a symmetric top");

    // Measured rate, from the angle swept.
    const double measured = std::atan2(w.y, w.x) / (period / 4.0);
    CHECK_NEAR_REL(measured, predicted, 1.0e-9,
                   "the same statement read as a rate rather than as a position");
}

TEST(the_intermediate_axis_is_unstable_at_the_predicted_rate) {
    // Dzhanibekov. With I1 < I2 < I3, a perturbation about the intermediate axis
    // grows as exp(lambda t) with
    //     lambda = omega_2 sqrt((I2-I1)(I3-I2)/(I1 I3)).
    const double i1 = 1200.0;
    const double i2 = 1800.0;
    const double i3 = 2400.0;
    const double spin = 0.05;

    const auto inertia = InertiaTensor::principal(i1, i2, i3);
    const TorqueFree forces;

    auto cfg = attitude_config();
    cfg.max_step = time::Duration::seconds(5.0);
    propagation::DormandPrince54Propagator propagator{forces, cfg};
    propagator.set_inertia(&inertia);

    const double seed = 1.0e-6;
    propagation::PropagationState initial{};
    initial.mass = 1000.0;
    initial.attitude.angular_velocity = Vec3{seed, spin, 0.0};

    // Linearised about omega = (0, Omega, 0):
    //     d(omega_1)/dt = -a omega_3,   a = (I3 - I2) Omega / I1
    //     d(omega_3)/dt = -b omega_1,   b = (I2 - I1) Omega / I3
    // so lambda^2 = a b, and with omega_3(0) = 0 the solution is NOT a pure
    // exponential:
    //     omega_1(t) =  seed cosh(lambda t)
    //     omega_3(t) = -seed sqrt(b/a) sinh(lambda t)
    //
    // The first version of this test fitted log(|perturbation|)/t and expected
    // lambda; it got 0.0155 against 0.0177 and the CODE was right. cosh(x) is
    // e^x/2 for large x, so that fit converges to lambda only as ln(2)/t -> 0.
    const double a = (i3 - i2) * spin / i1;
    const double b = (i2 - i1) * spin / i3;
    const double lambda = std::sqrt(a * b);
    const double window = 4.0 / lambda;  // four e-foldings, still linear

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::seconds(window));
    REQUIRE(result.ok());

    const Vec3 w = result.state.attitude.angular_velocity;
    const double expected_1 = seed * std::cosh(lambda * window);
    const double expected_3 = -seed * std::sqrt(b / a) * std::sinh(lambda * window);

    std::ostringstream os;
    os << "lambda = " << lambda << " 1/s (e-folding " << 1.0 / lambda << " s). After " << window
       << " s: omega_1 = " << w.x << " (predicted " << expected_1 << "), omega_3 = " << w.z
       << " (predicted " << expected_3 << ")";
    INFO(os.str());

    CHECK_NEAR_REL(w.x, expected_1, 2.0e-3,
                   "against the exact solution of the LINEARISED equations. The residual is the "
                   "nonlinear correction: after four e-foldings the perturbation is 2.7e-5 rad/s "
                   "against a 0.05 rad/s spin, i.e. 5.5e-4 of it, and the leading correction is "
                   "of that order. 0.2% bounds it");
    CHECK_NEAR_REL(w.z, expected_3, 2.0e-3,
                   "the same for the other component, including its SIGN -- the perturbation grows "
                   "into the third axis with a definite handedness, and getting the gyroscopic "
                   "term backwards would flip it");
    CHECK(w.x > 20.0 * seed);

    // And the extreme axes are stable: the same seed about axis 3 must not grow.
    propagation::PropagationState stable{};
    stable.mass = 1000.0;
    stable.attitude.angular_velocity = Vec3{seed, 0.0, spin};
    const auto stable_result =
        propagator.propagate(stable, t0, t0 + time::Duration::seconds(window));
    REQUIRE(stable_result.ok());
    const Vec3 ws = stable_result.state.attitude.angular_velocity;

    std::ostringstream os2;
    os2 << "about the major axis the same seed stays at " << std::hypot(ws.x, ws.y);
    INFO(os2.str());
    CHECK(std::hypot(ws.x, ws.y) < 2.0 * seed);
}

TEST(a_constant_torque_changes_the_angular_momentum_by_its_impulse) {
    const auto inertia = InertiaTensor::principal(1000.0, 1000.0, 1000.0);  // sphere: no gyroscopic term
    const Vec3 torque{2.0, 0.0, 0.0};
    const ConstantBodyTorque forces{torque};

    propagation::DormandPrince54Propagator propagator{forces, attitude_config()};
    propagator.set_inertia(&inertia);

    propagation::PropagationState initial{};
    initial.mass = 1000.0;

    const double dt = 60.0;
    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::seconds(dt));
    REQUIRE(result.ok());

    // For an isotropic body starting at rest, omega = I^-1 tau t exactly.
    const Vec3 expected = inertia.angular_acceleration(torque) * dt;
    CHECK_NEAR_REL(result.state.attitude.angular_velocity.x, expected.x, 1.0e-12,
                   "with I isotropic the gyroscopic term vanishes identically and the solution is "
                   "linear in time, which a 5th order integrator reproduces to rounding");
    CHECK_NEAR_ABS(result.state.attitude.angular_velocity.y, 0.0, 1.0e-14, "no torque about y");

    // The rotation angle is 1/2 alpha t^2.
    const double angle = result.state.attitude.orientation.to_axis_angle().angle.radians();
    CHECK_NEAR_REL(angle, 0.5 * expected.x / dt * dt * dt, 1.0e-9,
                   "constant angular acceleration about a fixed body axis integrates to "
                   "theta = alpha t^2 / 2; the axis does not move because the body is isotropic");
}


TEST(rcs_couples_produce_torque_with_no_net_force) {
    // Twelve thrusters in six couples. A couple is two thrusters whose forces
    // cancel and whose torques add -- which is what a well laid out RCS is, and
    // the reason this test can demand EXACT cancellation.
    const propulsion::EngineSpec thruster{"rcs", 0.02, 3.0e-5, 1.0};
    const auto rcs = attitude::RcsSystem::couples(2.5, thruster);

    INFO(rcs.describe());
    CHECK_EQ(rcs.size(), std::size_t{12});

    for (const Vec3 wanted : {Vec3{50.0, 0.0, 0.0}, Vec3{0.0, -30.0, 0.0}, Vec3{0.0, 0.0, 10.0},
                              Vec3{20.0, 20.0, -20.0}}) {
        const auto output = rcs.evaluate(wanted);

        CHECK_NEAR_ABS(output.force_body.norm(), 0.0, 1.0e-12,
                       "the two halves of each couple push in exactly opposite directions with "
                       "exactly equal thrust, so the forces cancel to rounding. A layout that did "
                       "not cancel would show up here as a parasitic push -- see the next test");

        // The delivered torque points where it was asked to, even though the
        // greedy allocator does not guarantee the magnitude.
        CHECK_NEAR_ABS(angle_between(output.torque_body, wanted), 0.0, 1.0e-12,
                       "the allocator opens only the thrusters whose torque agrees with the "
                       "request, and the couples are orthogonal, so the direction is exact");
        CHECK(output.mass_flow > 0.0);
    }

    // Asking for nothing costs nothing.
    const auto idle = rcs.evaluate(Vec3{});
    CHECK_EQ(idle.mass_flow, 0.0);
    CHECK_EQ(idle.torque_body.norm(), 0.0);
}

TEST(an_unbalanced_thruster_pushes_the_ship_while_it_turns_it) {
    // The insight the model gives away for free: rotating is not free, and a
    // thruster that is not part of a couple displaces the ship as well
    // (docs/physics/attitude.md section 6).
    const propulsion::EngineSpec spec{"rcs", 0.02, 3.0e-5, 1.0};
    const attitude::RcsSystem single{{attitude::RcsThruster{
        "single", Vec3{0.0, 2.5, 0.0}, Vec3{0.0, 0.0, 1.0}, spec}}};

    const auto output = single.evaluate(std::vector<double>{1.0});
    const double thrust = spec.thrust_at(1.0);

    std::ostringstream os;
    os << "one thruster at 2.5 m: torque " << output.torque_body.x << " N m, parasitic force "
       << output.force_body.norm() << " N";
    INFO(os.str());

    CHECK_NEAR_REL(output.torque_body.x, 2.5 * thrust, 1.0e-12,
                   "tau = r x F with r perpendicular to F, so |tau| = 2.5 * F exactly");
    CHECK_NEAR_REL(output.force_body.z, thrust, 1.0e-12,
                   "and the full thrust still acts on the ship: this is the parasitic push that a "
                   "'just rotate the ship' model hides");
    CHECK(output.mass_flow > 0.0);
}

TEST(the_pointing_controller_slews_to_prograde_and_stops_there) {
    constexpr double kGm = 3.9860043550702266e14;
    const auto earth = celestial::bodies::earth;
    sft::FixedPointMassProvider provider{earth, kGm, 0.0};
    const std::vector<celestial::BodyId> ids{earth};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);
    const auto ssb = coordinates::ReferenceFrame::ssb_j2000();

    const auto inertia = InertiaTensor::solid_box(1000.0, Vec3{8.0, 3.0, 3.0});
    const propulsion::EngineSpec thruster{"rcs", 0.02, 3.0e-5, 1.0};
    const auto rcs = attitude::RcsSystem::couples(2.0, thruster);

    attitude::PointingController controller{provider, inertia, ssb};
    attitude::PointingCommand command{};
    command.mode = navigation::GuidanceMode::Prograde;
    command.reference = earth;
    controller.set_command(command);

    const attitude::RcsForce rcs_force{rcs, controller};

    gravity::CompositeForceModel forces;
    forces.add(std::make_unique<gravity::PointMassGravity>(provider, catalog, ssb));
    forces.add_reference(rcs_force);

    const double radius = 6.778e6;
    propagation::PropagationState initial{};
    initial.mass = 1000.0;
    initial.state.position = Vec3{radius, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, trajectory::circular_speed(kGm, radius), 0.0};
    // Nose pointing radially outwards: 90 degrees away from prograde.
    initial.attitude.orientation = math::Quaternion::identity();

    auto cfg = attitude_config();
    cfg.max_step = time::Duration::seconds(5.0);
    propagation::DormandPrince54Propagator propagator{forces, cfg};
    propagator.set_inertia(&inertia);

    const auto t0 = time::CoordinateTime::j2000();
    const double initial_error = controller.pointing_error(initial, t0);

    double worst_overshoot = 0.0;
    propagation::PropagationState state = initial;
    auto t = t0;
    std::vector<double> history;
    for (int i = 0; i < 60; ++i) {
        const auto step = propagator.propagate(state, t, t + time::Duration::seconds(10.0));
        REQUIRE(step.ok());
        state = step.state;
        t = step.time;
        history.push_back(controller.pointing_error(state, t));
    }

    const double final_error = history.back();
    for (std::size_t i = 20; i < history.size(); ++i) {
        worst_overshoot = std::max(worst_overshoot, history[i]);
    }

    std::ostringstream os;
    os << "initial error " << units::rad_to_deg(initial_error) << " deg -> after 600 s "
       << units::rad_to_deg(final_error) << " deg; worst error after 200 s was "
       << units::rad_to_deg(worst_overshoot) << " deg; propellant used "
       << (initial.mass - state.mass) << " kg";
    INFO(os.str());

    CHECK_NEAR_REL(initial_error, units::pi / 2.0, 1.0e-9,
                   "the nose starts along +x, the velocity is along +y, and the reference body is "
                   "at rest: exactly 90 degrees apart by construction");

    // The residual is not slop: it is the TRACKING LAG of a proportional
    // controller following a target that moves. Prograde rotates at the orbital
    // rate n, so holding it requires omega = n, and the derivative term then
    // demands -2 zeta omega_n n of torque, which only a standing proportional
    // error can supply:
    //
    //     theta_lag = 2 zeta n / omega_n
    //
    const double orbital_rate = std::sqrt(kGm / (radius * radius * radius));
    const double damping_ratio = 1.0;
    const double natural_frequency = 0.05;
    const double predicted_lag = 2.0 * damping_ratio * orbital_rate / natural_frequency;

    std::ostringstream lag;
    lag << "predicted tracking lag 2 zeta n / omega_n = " << units::rad_to_deg(predicted_lag)
        << " deg, measured " << units::rad_to_deg(final_error) << " deg";
    INFO(lag.str());

    CHECK_NEAR_REL(final_error, predicted_lag, 2.0e-2,
                   "a PD controller is type 0: tracking a RAMP leaves a standing error, and this "
                   "is that error in closed form. The first version of this test demanded < 1 deg "
                   "and the controller was right. Removing the lag needs feed-forward of the "
                   "target rate or an integral term -- neither implemented, both named in "
                   "docs/physics/attitude.md section 7. 2% covers the thrusters' granularity");

    CHECK(worst_overshoot < 1.5 * predicted_lag + units::deg_to_rad(1.0));

    // It cost propellant, because rotating always does.
    CHECK(state.mass < initial.mass);
    CHECK(initial.mass - state.mass < 5.0);
}
