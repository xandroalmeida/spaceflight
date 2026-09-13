// Gravity as geometry: the geodesic of the weak-field metric, against the three
// classical tests and the two limits it has to reproduce.
//
// See docs/physics/relativistic-gravity.md.

#include "core/celestial/body_catalog.hpp"
#include "core/gravity/force_model.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/gravity/weak_field_metric.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/relativity/kinematics.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/analytic_ephemeris.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>
#include <vector>

using namespace sf;
using sf::math::Vec3;
using sf::propagation::Kinematics;

namespace {

constexpr double c = units::c;
constexpr double c2 = units::c_squared;
constexpr double kGmSun = 1.32712440041e20;
constexpr double kGmEarth = 3.9860043550702266e14;
const auto kSsb = coordinates::ReferenceFrame::ssb_j2000();

// Nothing but thrust -- and there is no thrust either. In WeakFieldStaticMetric
// mode gravity lives in the metric, so the force model has nothing to do.
class NoForce final : public gravity::ForceModel {
public:
    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState&,
                                                time::CoordinateTime) const override {
        return {};
    }
    [[nodiscard]] std::string_view name() const override { return "NoForce"; }
};

struct CentralBody {
    sft::FixedPointMassProvider provider;
    celestial::BodyCatalog catalog;

    explicit CentralBody(celestial::BodyId id, double gm, double radius = 0.0)
        : provider(id, gm, radius) {
        const std::vector<celestial::BodyId> ids{id};
        catalog = celestial::BodyCatalog::resolve(provider, ids);
    }
};

propagation::IntegratorConfig geodesic_config() {
    propagation::IntegratorConfig cfg{};
    cfg.kinematics = Kinematics::WeakFieldStaticMetric;
    cfg.relative_tolerance = 1.0e-13;
    cfg.absolute_tolerance_position = 1.0e-4;
    cfg.absolute_tolerance_velocity = 1.0e-7;
    cfg.initial_step = time::Duration::seconds(10.0);
    cfg.max_step = time::Duration::seconds(10000.0);
    return cfg;
}

}  // namespace

TEST(the_metric_reduces_to_the_potential_and_its_gradient) {
    CentralBody sun{celestial::bodies::sun, kGmSun};
    const gravity::WeakFieldMetric metric{sun.provider, sun.catalog, kSsb};

    const double au = units::au;
    const auto sample = metric.sample(Vec3{au, 0.0, 0.0}, time::CoordinateTime::j2000());

    std::ostringstream os;
    os << "at 1 au: U/c^2 = " << sample.potential / c2 << ", A = " << sample.a << ", B = "
       << sample.b << ", local light speed = c(1 - " << 1.0 - sample.local_light_speed / c << ")";
    INFO(os.str());

    CHECK_NEAR_REL(sample.potential, kGmSun / au, 1.0e-15, "U = GM/r for a single body");
    CHECK_NEAR_REL(sample.potential_gradient.x, -kGmSun / (au * au), 1.0e-15,
                   "grad U IS the Newtonian acceleration, which is why the relativistic layer "
                   "reuses the number PointMassGravity already computes");

    CHECK_NEAR_REL(sample.a, 1.0 - 2.0 * kGmSun / (au * c2) + 2.0 * std::pow(kGmSun / (au * c2), 2.0),
                   1.0e-15, "A = 1 - 2U/c^2 + 2U^2/c^4, evaluated from the same U");
    CHECK_NEAR_REL(sample.b, 1.0 + 2.0 * kGmSun / (au * c2), 1.0e-15, "B = 1 + 2U/c^2");

    CHECK_NEAR_REL(1.0 - sample.local_light_speed / c, 1.974e-8, 1.0e-3,
                   "the coordinate speed of light at 1 au is c sqrt(A/B) = c(1 - 2U/c^2) to "
                   "leading order, i.e. 1.97e-8 below c. That number is the reason the speed "
                   "limit in this mode is LOCAL and not c");
    CHECK(sample.local_light_speed < c);
}

TEST(the_speed_limit_is_the_local_light_speed_and_it_is_structural) {
    CentralBody earth{celestial::bodies::earth, kGmEarth, 6.371e6};
    const gravity::WeakFieldMetric metric{earth.provider, earth.catalog, kSsb};
    const auto sample = metric.sample(Vec3{6.778e6, 0.0, 0.0}, time::CoordinateTime::j2000());

    for (const double u_over_c : {1.0, 1.0e3, 1.0e6, 1.0e9}) {
        const Vec3 u = Vec3::unit_y() * (u_over_c * c);
        const double u0 = sample.time_component(u);
        const Vec3 v = sample.coordinate_velocity(u);

        // The mass-shell constraint is what DEFINES u0, so it holds identically.
        // Read as a residual it cannot be measured against c^2, though: at
        // u/c = 1e9 the two terms are each 1e18 c^2 and the c^2 they should
        // differ by is 18 orders below their own rounding. Normalise by the size
        // of the terms actually being subtracted -- that is the only scale at
        // which the identity is checkable in double precision.
        const double shell = -sample.a * u0 * u0 + sample.b * u.norm_squared();
        const double residual = std::abs(shell + c2) / (sample.a * u0 * u0);

        std::ostringstream os;
        os << "u/c = " << u_over_c << ": |v|/c = " << v.norm() / c << ", local limit "
           << sample.local_light_speed / c << ", mass shell residual " << residual
           << " of the terms (" << std::abs(shell + c2) / c2 << " of c^2)";
        INFO(os.str());

        CHECK_NEAR_ABS(residual, 0.0, 1.0e-15,
                       "g_uv u^u u^v = -c^2 holds identically because u0 is defined by it, so the "
                       "only thing this can check is the arithmetic: a few ulp of the terms being "
                       "differenced. What matters physically is the line above -- there is nowhere "
                       "a clamp could be inserted, because the limit is the FORM of u0 (rule 13)");

        // Below gamma^2 = 1/eps the strict inequality is representable and must
        // hold; above it, 1 - 1/(2 gamma^2) rounds to 1 and v lands exactly on
        // the local light speed. That is the floating-point grid, not the
        // physics -- the same boundary already documented for beta in
        // docs/physics/relativistic-propulsion.md.
        if (u_over_c < 6.7e7) {
            CHECK(v.norm() < sample.local_light_speed);
        } else {
            CHECK(v.norm() <= sample.local_light_speed);
        }
    }

    // The approach is asymptotic and it never overshoots, however absurd the
    // proper velocity gets.
    const Vec3 extreme = Vec3::unit_y() * (1.0e12 * c);
    CHECK(sample.coordinate_velocity(extreme).norm() <= sample.local_light_speed);
}

TEST(the_geodesic_deflects_light_by_twice_the_newtonian_amount) {
    // For |u| -> infinity with the motion transverse to the gradient, the
    // geodesic term tends to 2 * (Newtonian). That factor of two is the classical
    // signature of general relativity, and it falls out of the forms of A and B
    // without anyone programming it.
    CentralBody sun{celestial::bodies::sun, kGmSun};
    const gravity::WeakFieldMetric metric{sun.provider, sun.catalog, kSsb};

    const double r = units::au;
    const auto sample = metric.sample(Vec3{r, 0.0, 0.0}, time::CoordinateTime::j2000());
    const double newtonian = sample.potential_gradient.norm();

    double previous_ratio = 0.0;
    for (const double u_over_c : {1.0, 10.0, 1.0e3, 1.0e6}) {
        // Motion perpendicular to the radius: the gradient is along -x, so move
        // along y.
        const Vec3 u = Vec3::unit_y() * (u_over_c * c);
        const Vec3 acceleration = sample.geodesic_acceleration(u);
        const double u0 = sample.time_component(u);
        // Compare the coordinate-time second derivative, which is what bends the
        // path: (c/u0)^2 scales du/dtau into d^2x/dt^2 for transverse motion.
        const double ratio = acceleration.norm() * std::pow(c / u0, 2.0) / newtonian;

        std::ostringstream os;
        os << "u/c = " << u_over_c << ": transverse deflection is " << ratio
           << " times the Newtonian value";
        INFO(os.str());
        previous_ratio = ratio;
    }

    CHECK_NEAR_REL(previous_ratio, 2.0, 1.0e-5,
                   "the light-bending factor of 2. At u/c = 1e6 the particle is ultrarelativistic "
                   "and the ratio has converged; the residual is the 1/gamma^2 term still left "
                   "over from a massive particle's trajectory");

    // And at low speed it is 1, not 2: same formula, other limit.
    const Vec3 slow = Vec3::unit_y() * 1.0e4;
    const double slow_ratio =
        sample.geodesic_acceleration(slow).norm() *
        std::pow(c / sample.time_component(slow), 2.0) / newtonian;
    CHECK_NEAR_REL(slow_ratio, 1.0, 1.0e-7,
                   "a slow particle feels Newtonian gravity; a photon feels twice as much. One "
                   "metric, both answers. The bound is a few times U/c^2 = 9.87e-9 at 1 au, "
                   "because the slow limit is 1 + O(U/c^2) and not 1 exactly -- B in the "
                   "denominator and the 2U^2/c^4 term in A both survive at that order. Measured "
                   "residual 3.8e-8, i.e. 3.9 U/c^2");
}

TEST(the_geodesic_converges_on_newtonian_gravity_in_low_earth_orbit) {
    // Rule 31's Newtonian limit, for gravity this time: the geodesic integrator
    // and PointMassGravity must AGREE numerically, to the size of the terms that
    // separate them.
    CentralBody earth{celestial::bodies::earth, kGmEarth, 6.371e6};
    const gravity::WeakFieldMetric metric{earth.provider, earth.catalog, kSsb};
    const gravity::PointMassGravity newtonian_gravity{earth.provider, earth.catalog, kSsb};
    const NoForce nothing;

    const double radius = 6.778e6;
    const double speed = trajectory::circular_speed(kGmEarth, radius);
    const double period = trajectory::circular_period(kGmEarth, radius);

    propagation::PropagationState initial{};
    initial.mass = 1000.0;
    initial.state.position = Vec3{radius, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, speed, 0.0};

    propagation::DormandPrince54Propagator geodesic{nothing, geodesic_config()};
    geodesic.set_metric(&metric);

    auto newtonian_config = geodesic_config();
    newtonian_config.kinematics = Kinematics::Newtonian;
    propagation::DormandPrince54Propagator newtonian{newtonian_gravity, newtonian_config};

    const auto t0 = time::CoordinateTime::j2000();
    const auto t1 = t0 + time::Duration::seconds(period);

    const auto relativistic = geodesic.propagate(initial, t0, t1);
    const auto classical = newtonian.propagate(initial, t0, t1);
    REQUIRE(relativistic.ok());
    REQUIRE(classical.ok());

    const double separation =
        (relativistic.state.state.position - classical.state.state.position).norm();

    // A circular orbit has no defined periapsis, so the familiar 6*pi*mu/(c^2 r)
    // precession formula is NOT a valid oracle here. Both trajectories start at
    // the Newtonian circular speed; that speed is not the circular-geodesic speed
    // in isotropic coordinates. Expanding the circular-geodesic condition for
    // this metric gives Omega = sqrt(mu/r^3) (1 - 3 mu/(2 r c^2)), and propagating
    // the resulting radial/phase mismatch for one Newtonian period gives a
    // leading separation 12*pi*mu/c^2. The old test expected half of this and
    // passed only because the flat-space v->u conversion supplied a compensating
    // O(U/c^2) input error.
    const double expected_separation = 12.0 * units::pi * kGmEarth / c2;

    std::ostringstream os;
    os << "after one Newtonian period the two models differ by " << separation
       << " m; the 1PN circular-frequency expansion predicts " << expected_separation << " m";
    INFO(os.str());

    CHECK(separation > 0.0);
    CHECK_NEAR_REL(separation, expected_separation, 1.0e-3,
                   "12 pi GM/c^2 = 0.1671966 m for the phase/radial mismatch produced by using "
                   "the Newtonian circular speed as the initial condition of the metric. The "
                   "0.1% bound covers the finite-time radial oscillation omitted by the leading "
                   "frequency estimate and integration error. "
                   "Periapsis precession is tested only on the eccentric Mercury orbit below");

    // Proper time runs slow, by the amount the metric says.
    const double dilation = 1.0 - relativistic.state.proper_time.seconds() / period;
    CHECK_NEAR_REL(dilation, kGmEarth / (radius * c2) + 0.5 * speed * speed / c2, 1.0e-3,
                   "dtau/dt = sqrt(A/(1 + B|u|^2/c^2)) gives 1 - U/c^2 - v^2/2c^2 to leading "
                   "order: gravitational and kinematic dilation together");
    CHECK_NEAR_REL(classical.state.proper_time.seconds(), period, 1.0e-14,
                   "in Newtonian mode dtau/dt is identically 1, so proper time should equal the "
                   "elapsed coordinate time. It is not bit-exact because it is accumulated one "
                   "adaptive step at a time; 1e-14 is a few ulp over the ~50 steps of an orbit");
}

TEST(mercury_precesses_by_the_measured_forty_three_arcseconds) {
    // The classical test, against a number that was measured before the theory
    // existed. Nothing in this model was tuned to it.
    CentralBody sun{celestial::bodies::sun, kGmSun};
    const gravity::WeakFieldMetric metric{sun.provider, sun.catalog, kSsb};
    const NoForce nothing;

    const double a = 5.790905e10;
    const double e = 0.205630;
    const double period = units::two_pi * std::sqrt(a * a * a / kGmSun);

    const double rp = a * (1.0 - e);
    const double vp = std::sqrt(kGmSun * (1.0 + e) / (a * (1.0 - e)));

    propagation::PropagationState initial{};
    initial.mass = 1.0;
    initial.state.position = Vec3{rp, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, vp, 0.0};

    auto cfg = geodesic_config();
    cfg.max_step = time::Duration::seconds(20000.0);
    propagation::DormandPrince54Propagator propagator{nothing, cfg};
    propagator.set_metric(&metric);

    // Sample the eccentricity vector many times over many orbits and fit its
    // rotation rate. Sampling densely averages out the periodic part of the
    // osculating element, leaving the secular drift.
    const int orbits = 60;
    const int samples_per_orbit = 8;
    const int total = orbits * samples_per_orbit;

    const auto t0 = time::CoordinateTime::j2000();
    std::vector<double> times;
    std::vector<double> angles;
    times.reserve(static_cast<std::size_t>(total));
    angles.reserve(static_cast<std::size_t>(total));

    propagation::PropagationState state = initial;
    auto t = t0;
    double previous = 0.0;
    bool all_steps_converged = true;
    for (int i = 0; i <= total; ++i) {
        if (i > 0) {
            const auto step = propagator.propagate(
                state, t, t + time::Duration::seconds(period / samples_per_orbit));
            all_steps_converged = all_steps_converged && step.ok();
            state = step.state;
            t = step.time;
        }
        const auto elements = trajectory::elements_from_state(state.state, kGmSun);
        double angle = elements.argument_of_periapsis;
        while (angle - previous > units::pi) {
            angle -= units::two_pi;
        }
        while (previous - angle > units::pi) {
            angle += units::two_pi;
        }
        previous = angle;
        times.push_back((t - t0).seconds());
        angles.push_back(angle);
    }

    REQUIRE(all_steps_converged);

    // Least-squares slope.
    double sum_t = 0.0;
    double sum_a = 0.0;
    double sum_tt = 0.0;
    double sum_ta = 0.0;
    const double n = static_cast<double>(times.size());
    for (std::size_t i = 0; i < times.size(); ++i) {
        sum_t += times[i];
        sum_a += angles[i];
        sum_tt += times[i] * times[i];
        sum_ta += times[i] * angles[i];
    }
    const double slope = (n * sum_ta - sum_t * sum_a) / (n * sum_tt - sum_t * sum_t);

    const double predicted_per_orbit = 6.0 * units::pi * kGmSun / (c2 * a * (1.0 - e * e));
    const double measured_per_orbit = slope * period;
    const double century = 100.0 * 365.25 * 86400.0;

    std::ostringstream os;
    os << "over " << orbits << " orbits: " << measured_per_orbit << " rad/orbit (predicted "
       << predicted_per_orbit << "), which is "
       << units::rad_to_deg(slope * century) * 3600.0 << " arcsec/century";
    INFO(os.str());

    CHECK_NEAR_REL(measured_per_orbit, predicted_per_orbit, 2.0e-3,
                   "6 pi GM / (c^2 a (1-e^2)) per orbit. The residual is the periodic part of the "
                   "osculating argument of periapsis that the linear fit does not fully average "
                   "out over 60 orbits, plus the integrator. 0.2% is what that costs");

    CHECK_NEAR_ABS(units::rad_to_deg(slope * century) * 3600.0, 42.98, 0.2,
                   "and read in the units the nineteenth century measured it in: 42.98 arcsec per "
                   "century, observed by Le Verrier in 1859 and unexplained until 1915. The "
                   "0.2 arcsec bound is the fit residual above, not any uncertainty in the "
                   "reference value");
}

TEST(the_gps_clock_runs_fast_by_thirty_eight_microseconds_a_day) {
    // Both effects at once, and they have opposite signs: the orbiting clock is
    // higher in the potential (runs fast) and moving (runs slow). The net is
    // measured daily by every receiver on Earth.
    CentralBody earth{celestial::bodies::earth, kGmEarth, 6.371e6};
    const gravity::WeakFieldMetric metric{earth.provider, earth.catalog, kSsb};
    const NoForce nothing;

    const double r_gps = 26561.0e3;
    const double r_ground = 6371.0e3;
    const double speed = trajectory::circular_speed(kGmEarth, r_gps);
    const double period = trajectory::circular_period(kGmEarth, r_gps);

    // The ground clock is static in these coordinates: dtau/dt = sqrt(A). No
    // propagation needed -- the metric answers directly.
    const auto ground = metric.sample(Vec3{r_ground, 0.0, 0.0}, time::CoordinateTime::j2000());
    const double ground_rate = ground.proper_time_rate(Vec3{});

    propagation::PropagationState initial{};
    initial.mass = 1.0;
    initial.state.position = Vec3{r_gps, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, speed, 0.0};

    propagation::DormandPrince54Propagator propagator{nothing, geodesic_config()};
    propagator.set_metric(&metric);

    const auto t0 = time::CoordinateTime::j2000();
    const auto result =
        propagator.propagate(initial, t0, t0 + time::Duration::seconds(period));
    REQUIRE(result.ok());

    const double orbit_rate = result.state.proper_time.seconds() / period;
    const double difference_per_day = (orbit_rate - ground_rate) * 86400.0;

    std::ostringstream os;
    os << "orbit clock rate - 1 = " << orbit_rate - 1.0 << ", ground clock rate - 1 = "
       << ground_rate - 1.0 << ", difference = " << difference_per_day * 1.0e6 << " us/day";
    INFO(os.str());

    CHECK_NEAR_REL(difference_per_day * 1.0e6, 38.51, 5.0e-3,
                   "38.51 us/day for a NON-ROTATING ground clock. The 38.6 us/day usually quoted "
                   "includes the Earth's rotation, which moves the ground clock at 465 m/s at the "
                   "equator and costs a further 0.1 us/day -- the difference between the two "
                   "numbers IS that rotation. The bound is the precision of the reference values");

    CHECK(orbit_rate > ground_rate);
    CHECK(orbit_rate < 1.0);
    CHECK(ground_rate < 1.0);
}

TEST(the_shapiro_delay_comes_out_of_the_coordinate_light_speed) {
    // A metric test rather than an integrator test: light travels at
    // c sqrt(A/B) ~ c(1 - 2U/c^2), so a signal grazing the Sun arrives late.
    CentralBody sun{celestial::bodies::sun, kGmSun};
    const gravity::WeakFieldMetric metric{sun.provider, sun.catalog, kSsb};

    const double r1 = 1.496e11;   // Earth
    const double r2 = 1.082e11;   // Venus
    const double impact = 6.957e8;  // grazing the solar limb

    // Integrate dt = ds / (c sqrt(A/B)) along the straight line between them,
    // passing at `impact` from the centre. The straight path is itself an
    // approximation -- the true path bends -- but the bending contributes at
    // higher order to the delay.
    const int steps = 200000;
    const double x1 = -std::sqrt(r1 * r1 - impact * impact);
    const double x2 = std::sqrt(r2 * r2 - impact * impact);
    const double dx = (x2 - x1) / steps;

    double travel = 0.0;
    double flat = 0.0;
    for (int i = 0; i < steps; ++i) {
        const double x = x1 + (static_cast<double>(i) + 0.5) * dx;
        const auto sample = metric.sample(Vec3{x, impact, 0.0}, time::CoordinateTime::j2000());
        travel += dx / sample.local_light_speed;
        flat += dx / c;
    }

    const double delay = (travel - flat) * 1.0e6;
    const double predicted =
        2.0 * kGmSun / (c * c * c) * std::log(4.0 * r1 * r2 / (impact * impact)) * 1.0e6;

    std::ostringstream os;
    os << "one-way Shapiro delay grazing the Sun: " << delay << " us (closed form " << predicted
       << " us)";
    INFO(os.str());

    CHECK_NEAR_REL(delay, predicted, 5.0e-3,
                   "Delta t = (2GM/c^3) ln(4 r1 r2 / d^2), the standard result. The residual is "
                   "the straight-line path assumption and the quadrature over 200 000 steps; the "
                   "measured Earth-Venus round trip is about 240 us, which is twice this");
    CHECK(delay > 0.0);
}
