// A frame carried without torque along a curved worldline still turns.
//
// See docs/physics/spin-transport.md.

#include "core/attitude/inertia.hpp"
#include "core/celestial/body_catalog.hpp"
#include "core/gravity/force_model.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/relativity/kinematics.hpp"
#include "core/relativity/spin_transport.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/analytic_ephemeris.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>

using namespace sf;
using sf::math::Vec3;
using sf::propagation::Kinematics;

namespace {

constexpr double c = units::c;
constexpr double c2 = units::c_squared;

// The textbook form, computed independently from v and dv/dt, to check the
// rewrite in spin-transport.md section 2.1 rather than trust it.
Vec3 jackson_thomas(const Vec3& u, const Vec3& du_dt) {
    const double gamma = relativity::lorentz_factor(u);
    const Vec3 v = u / gamma;
    const Vec3 a = (du_dt - v * (dot(v, du_dt) / c2)) / gamma;
    return cross(a, v) * (gamma * gamma / ((gamma + 1.0) * c2));
}

// Thrust perpendicular to the motion, of fixed magnitude: the ship traces a
// circle at constant gamma. Mass flow is deliberately zero -- this is a test of
// kinematics, and a depleting mass would change gamma and destroy the closed
// form being checked.
class TransverseThrust final : public gravity::ForceModel {
public:
    TransverseThrust(double magnitude, Vec3 plane_normal)
        : magnitude_(magnitude), normal_(plane_normal.normalized()) {}

    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState& s,
                                                time::CoordinateTime) const override {
        gravity::ForceResult result{};
        const Vec3 u = s.state.velocity;
        if (u.norm() > 0.0) {
            result.proper_thrust = cross(normal_, u).normalized() * magnitude_;
        }
        return result;
    }
    [[nodiscard]] std::string_view name() const override { return "TransverseThrust"; }

private:
    double magnitude_;
    Vec3 normal_;
};

class NoForce final : public gravity::ForceModel {
public:
    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState&,
                                                time::CoordinateTime) const override {
        return {};
    }
    [[nodiscard]] std::string_view name() const override { return "NoForce"; }
};

}  // namespace

TEST(the_cancellation_free_rewrite_agrees_with_the_textbook_form) {
    // This checks algebra, not physics: (du/dt x u)/((gamma+1)c^2) must equal
    // (gamma^2/(gamma+1))(a x v)/c^2 for any state.
    for (const double rapidity : {1.0e-6, 0.1, 1.0, 3.0, 10.0, 21.0}) {
        const Vec3 u = Vec3::unit_x() * (c * std::sinh(rapidity));
        const Vec3 du_dt = Vec3{3.0, 17.0, -5.0};  // deliberately not aligned

        const Vec3 mine = relativity::thomas_precession(u, du_dt);
        const Vec3 theirs = jackson_thomas(u, du_dt);

        std::ostringstream os;
        os << "phi = " << rapidity << " (beta = " << std::tanh(rapidity) << ", gamma = "
           << std::cosh(rapidity) << "): |omega_T| = " << mine.norm() << ", textbook "
           << theirs.norm();
        INFO(os.str());

        CHECK_NEAR_REL(mine.norm(), theirs.norm(), 1.0e-12,
                       "the same quantity by two routes. The textbook route forms "
                       "dv/dt = (1/gamma)[du/dt - v(v.du/dt)/c^2], which is a difference of "
                       "nearly equal vectors as beta -> 1; 1e-12 is what THAT costs, not what "
                       "the implementation costs. At phi = 21 (beta = 1 - 5e-19) the textbook "
                       "form is the one losing digits");
        CHECK_NEAR_ABS(cross(mine, theirs).norm() / (mine.norm() * theirs.norm()), 0.0, 1.0e-12,
                       "and they point the same way");
    }
}

TEST(collinear_thrust_produces_exactly_no_precession) {
    // Not "below tolerance": zero, because the cross product of parallel vectors
    // is zero. There is no branch in the code that could get this wrong.
    // Along an axis the cross product is bit-exact zero: every term is a product
    // of zeros.
    const Vec3 axial = Vec3::unit_x() * (2.0 * c);
    CHECK_EQ(relativity::thomas_precession(axial, Vec3::unit_x() * 9.81).norm(), 0.0);
    CHECK_EQ(relativity::thomas_precession(axial, Vec3::unit_x() * -9.81).norm(), 0.0);
    CHECK_EQ(relativity::thomas_precession(axial, Vec3{}).norm(), 0.0);
    CHECK_EQ(relativity::thomas_precession(Vec3{}, Vec3{1.0, 2.0, 3.0}).norm(), 0.0);

    // Off-axis it is zero analytically and a few ulp numerically, because
    // normalising a vector does not give something exactly parallel to it. The
    // honest statement is the RATIO: the precession is negligible against the
    // rate a transverse burn of the same magnitude would produce.
    const Vec3 u = Vec3{0.6, -0.8, 0.0} * (2.0 * c);
    const Vec3 forward = u.normalized();
    const Vec3 sideways = Vec3{0.8, 0.6, 0.0};
    const double along = relativity::thomas_precession(u, forward * 9.81).norm();
    const double across = relativity::thomas_precession(u, sideways * 9.81).norm();

    std::ostringstream os;
    os << "collinear burn: " << along << " rad/s; the same burn across the track: " << across
       << " rad/s; ratio " << along / across;
    INFO(os.str());

    CHECK_NEAR_ABS(along / across, 0.0, 1.0e-15,
                   "zero to a few ulp, not structurally zero: u.normalized() is not exactly "
                   "parallel to u in binary floating point, so the cross product keeps a "
                   "rounding residue. The axial case above IS bit-exact, and the difference "
                   "between the two is arithmetic, not physics");
    INFO("a ship burning along its velocity vector keeps its orientation, at any beta");
}

TEST(thomas_precession_is_retrograde_and_halves_in_the_slow_limit) {
    // The factor 1/2 that Thomas found in 1926, explaining why the naive
    // spin-orbit coupling of the electron came out twice the observed value.
    const double radius = 1.0e6;

    for (const double b : {1.0e-4, 1.0e-2, 0.1}) {
        const double speed = b * c;
        const double omega_orbit = speed / radius;
        const Vec3 u = relativity::proper_velocity(Vec3::unit_y() * speed);
        const double gamma = relativity::lorentz_factor(u);
        // Circular motion: du/dt is transverse, |du/dt| = gamma * |a| with
        // |a| = v^2/r, pointing at the centre (-x here).
        const Vec3 du_dt = Vec3::unit_x() * (-gamma * speed * speed / radius);

        const Vec3 omega_t = relativity::thomas_precession(u, du_dt);
        const double ratio = omega_t.z / omega_orbit;

        std::ostringstream os;
        os << "beta = " << b << ": omega_T/omega_orbit = " << ratio << ", half beta^2 = "
           << -0.5 * b * b;
        INFO(os.str());

        CHECK(omega_t.z < 0.0);
        // gamma - 1 by subtraction is the wrong tool here and this project owns
        // the right one: at beta = 1e-4 the difference is 5e-9 and subtracting 1
        // from a double near 1 leaves 2.2e-16 of noise, i.e. 4e-8 relative. The
        // first version of this test failed for exactly that reason -- in the
        // TEST, not in the implementation.
        CHECK_NEAR_REL(ratio, -relativity::lorentz_factor_minus_one(u), 1.0e-12,
                       "|omega_T| = (gamma - 1) omega exactly for circular motion, using "
                       "gamma^2 beta^2 = gamma^2 - 1 (spin-transport.md section 2.2). Retrograde: "
                       "the sign is negative against a prograde orbit");
        CHECK_NEAR_REL(ratio, -0.5 * b * b, 3.0 * b * b,
                       "and (gamma - 1) -> beta^2/2 in the slow limit, the classic Thomas half. "
                       "The tolerance is itself beta^2 because that is the size of the next term: "
                       "gamma - 1 = beta^2/2 + 3beta^4/8");
    }
}

TEST(a_relativistic_turn_accumulates_the_wigner_rotation) {
    // End to end, against a closed form: after one full revolution at constant
    // beta the orientation has turned by exactly 2*pi*(gamma - 1).
    const double beta_target = 0.8;
    const double gamma = 1.0 / std::sqrt(1.0 - beta_target * beta_target);
    const double speed = beta_target * c;
    const double radius = 1.0e9;
    const double mass = 1000.0;

    // |du/dt| = F/(gamma m) for transverse thrust, and circular motion needs
    // |du/dt| = |u| * omega with omega = v/r.
    const double omega_orbit = speed / radius;
    const double u_magnitude = gamma * speed;
    const double thrust = u_magnitude * omega_orbit * gamma * mass;
    const double period = units::two_pi / omega_orbit;

    const TransverseThrust force{thrust, Vec3::unit_z()};

    propagation::IntegratorConfig cfg{};
    cfg.kinematics = Kinematics::SpecialRelativistic;
    cfg.relative_tolerance = 1.0e-13;
    cfg.absolute_tolerance_position = 1.0e-3;
    cfg.absolute_tolerance_velocity = 1.0e-6;
    cfg.absolute_tolerance_orientation = 1.0e-12;
    cfg.initial_step = time::Duration::seconds(1.0);
    cfg.max_step = time::Duration::seconds(period / 200.0);

    propagation::DormandPrince54Propagator propagator{force, cfg};
    const auto inertia = attitude::InertiaTensor::principal(1000.0, 3000.0, 3000.0);
    propagator.set_inertia(&inertia);

    propagation::PropagationState initial{};
    initial.mass = mass;
    initial.state.position = Vec3{radius, 0.0, 0.0};
    // The propagator is stated in COORDINATE velocity and converts internally.
    initial.state.velocity = Vec3::unit_y() * speed;
    initial.attitude.orientation = math::Quaternion::identity();
    initial.attitude.angular_velocity = Vec3{};  // gyros locked, no torque

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(initial, t0, t0 + time::Duration::seconds(period));
    REQUIRE(result.ok());
    // The ship came back to where it started.
    const double closure = (result.state.state.position - initial.state.position).norm();
    CHECK_NEAR_REL(closure / radius, 0.0, 1.0e-8,
                   "the trajectory closes: transverse thrust does no work, so |u| and therefore "
                   "gamma are constant and the path is a circle");
    CHECK_NEAR_REL(result.state.state.velocity.norm() / c, beta_target, 1.0e-12,
                   "and beta never moved, which is the premise of the closed form below");

    const auto rotation = result.state.attitude.orientation.to_axis_angle();
    const double turned = rotation.angle.radians();
    const double expected = units::two_pi * (gamma - 1.0);

    std::ostringstream os;
    os << "after one revolution at beta = " << beta_target << " (gamma = " << gamma
       << ") the body frame turned " << turned << " rad; 2 pi (gamma - 1) = " << expected
       << " (" << units::rad_to_deg(expected) << " degrees)";
    INFO(os.str());

    CHECK_NEAR_REL(turned, expected, 1.0e-9,
                   "2 pi (gamma - 1) = 2 pi / 3 for gamma = 5/3. Nothing applied a torque: "
                   "angular_velocity is zero throughout and the inertia tensor is irrelevant. "
                   "The rotation is the accumulated Wigner rotation of the boosts themselves. "
                   "1e-9 is the integrator at rtol 1e-13 over 200 steps, not a modelling term");

    // The rotation axis is the orbit normal, and retrograde against it.
    CHECK_NEAR_REL(std::abs(dot(rotation.axis, Vec3::unit_z())), 1.0, 1.0e-9,
                   "the precession is about the orbit normal");
}

TEST(a_newtonian_run_has_no_precession_at_all) {
    // Rule 31 in its strictest form: the feature must be invisible where it does
    // not belong. Newtonian mode is by definition the theory in which O(beta^2)
    // and O(U/c^2) terms do not exist.
    const double speed = 0.5 * c;
    const double radius = 1.0e9;
    const double mass = 1000.0;
    const double thrust = mass * speed * speed / radius;

    const TransverseThrust force{thrust, Vec3::unit_z()};

    propagation::IntegratorConfig cfg{};
    cfg.kinematics = Kinematics::Newtonian;
    cfg.relative_tolerance = 1.0e-12;
    cfg.initial_step = time::Duration::seconds(1.0);

    propagation::DormandPrince54Propagator propagator{force, cfg};
    const auto inertia = attitude::InertiaTensor::principal(1000.0, 3000.0, 3000.0);
    propagator.set_inertia(&inertia);

    propagation::PropagationState initial{};
    initial.mass = mass;
    initial.state.position = Vec3{radius, 0.0, 0.0};
    initial.state.velocity = Vec3::unit_y() * speed;
    initial.attitude.orientation = math::Quaternion::identity();

    const auto t0 = time::CoordinateTime::j2000();
    const auto result = propagator.propagate(
        initial, t0, t0 + time::Duration::seconds(units::two_pi * radius / speed));
    REQUIRE(result.ok());

    const double turned = result.state.attitude.orientation.to_axis_angle().angle.radians();
    CHECK_EQ(turned, 0.0);
    INFO("bit-exact zero, not 'small': the precession terms are never evaluated in this mode");
}

TEST(the_gravity_probe_b_gyroscope_precesses_by_six_point_six_arcseconds_a_year) {
    // Measured in orbit: 6601.8 +/- 18.3 mas/yr, against 6606.1 predicted by the
    // full theory.
    const double gm_earth = 3.9860043550702266e14;
    const double a = 7027.4e3;
    const double speed = std::sqrt(gm_earth / a);
    const double omega_orbit = speed / a;

    const Vec3 v = Vec3::unit_y() * speed;
    const Vec3 grad_u = Vec3::unit_x() * (-gm_earth / (a * a));

    const Vec3 omega_g = relativity::geodetic_precession(v, grad_u);
    const double year = 365.25 * 86400.0;
    const double mas_per_year = units::rad_to_deg(omega_g.norm() * year) * 3600.0 * 1000.0;

    std::ostringstream os;
    os << "GP-B: omega_G = " << omega_g.norm() << " rad/s = " << mas_per_year
       << " mas/yr; the orbit itself runs at " << omega_orbit << " rad/s";
    INFO(os.str());

    CHECK(omega_g.z > 0.0);
    CHECK_NEAR_REL(mas_per_year, 6604.1, 1.0e-4,
                   "6604.1 mas/yr from (3/2)(v x grad U)/c^2 with GP-B's semi-major axis. The "
                   "full GR prediction is 6606.1 -- the 2 mas difference is orbital eccentricity "
                   "and the J2 correction, neither of which this 1PN term carries, and it is 9 "
                   "times smaller than the 18.3 mas experimental uncertainty");
    CHECK_NEAR_ABS(mas_per_year, 6601.8, 18.3,
                   "and it lands inside the published error bar of the 2011 measurement. What is "
                   "NOT here is Lense-Thirring, 39.2 mas/yr, which GP-B also measured: that term "
                   "needs g_0i, the debt relativistic-gravity.md section 7 already declares");

    CHECK_NEAR_REL(omega_g.norm() / omega_orbit, 1.5 * gm_earth / (a * c2), 1.0e-12,
                   "as a fraction of the orbital rate it is exactly (3/2) GM/(rc^2) = 9.5e-10, "
                   "which is the form the closed expression takes on a circle");
}

TEST(the_moon_is_a_gyroscope_and_precesses_by_nineteen_milliarcseconds_a_year) {
    // Lunar laser ranging measures the Earth-Moon system's de Sitter precession
    // in the Sun's field: 19.2 mas/yr. Same expression, a different body, six
    // orders of magnitude away in scale.
    const double gm_sun = 1.32712440041e20;
    const double au = units::au;
    const double speed = std::sqrt(gm_sun / au);

    const Vec3 v = Vec3::unit_y() * speed;
    const Vec3 grad_u = Vec3::unit_x() * (-gm_sun / (au * au));

    const Vec3 omega_g = relativity::geodetic_precession(v, grad_u);
    const double year = 365.25 * 86400.0;
    const double mas_per_year = units::rad_to_deg(omega_g.norm() * year) * 3600.0 * 1000.0;

    std::ostringstream os;
    os << "Earth-Moon in the Sun's field: " << mas_per_year << " mas/yr";
    INFO(os.str());

    CHECK_NEAR_REL(mas_per_year, 19.2, 5.0e-3,
                   "19.2 mas/yr, observed by lunar laser ranging. The 0.5% bound is the circular "
                   "orbit assumed here against the real one, plus the reference value being "
                   "quoted to three figures");
}

TEST(the_geodetic_term_reuses_the_newtonian_acceleration_the_propagator_already_has) {
    // Not a physics test: a statement about where the number comes from. grad U
    // IS PointMassGravity's output, and the metric, the geodesic and this
    // precession all consume the same one.
    const double gm = 1.32712440041e20;
    const double r = units::au;
    const Vec3 grad_u = Vec3::unit_x() * (-gm / (r * r));

    CHECK_NEAR_REL(grad_u.norm(), 5.93008351998e-3, 1.0e-10,
                   "the Sun's Newtonian pull at 1 au, the same 5.93 mm/s^2 that appears in "
                   "test_relativistic_gravity.cpp and in test_solar_system_gravity.cpp");

    // Zero velocity -> no precession. A static gyroscope does not turn, however
    // deep in the potential it sits; only redshift happens there.
    CHECK_EQ(relativity::geodetic_precession(Vec3{}, grad_u).norm(), 0.0);
    // Radial motion -> no precession either: v x grad U vanishes.
    CHECK_EQ(relativity::geodetic_precession(Vec3::unit_x() * 1.0e5, grad_u).norm(), 0.0);
    INFO("a gyroscope falling straight down does not precess; it has to go AROUND something");
}
