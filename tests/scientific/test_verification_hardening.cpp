// Milestone 6 falsification tests: independent closed forms, partitioning,
// determinism and runtime invariants. Expected values are deliberately computed
// here in long double and do not call the core function under test.

#include "core/celestial/body_catalog.hpp"
#include "core/gravity/force_model.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/gravity/weak_field_metric.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/relativity/light_time.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "tests/support/analytic_ephemeris.hpp"
#include "tests/support/kepler.hpp"
#include "tests/support/test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

using namespace sf;
using sf::math::Vec3;

namespace {

constexpr double kEarthGm = 3.9860043550702266e14;
constexpr double c = units::c;

class NoForce final : public gravity::ForceModel {
public:
    [[nodiscard]] gravity::ForceResult evaluate(const propagation::PropagationState&,
                                                time::CoordinateTime) const override {
        return {};
    }
    [[nodiscard]] std::string_view name() const override { return "NoForce"; }
};

// x(t) = x0 + velocity*t, an ephemeris independent of the light-time solver.
class LinearEphemeris final : public ephemeris::EphemerisProvider {
public:
    LinearEphemeris(celestial::BodyId id, Vec3 at_j2000, Vec3 velocity)
        : id_(id), at_j2000_(at_j2000), velocity_(velocity) {}

    [[nodiscard]] ephemeris::BodyState state(celestial::BodyId body,
                                             time::CoordinateTime t,
                                             coordinates::ReferenceFrame frame) const override {
        if (body == frame.origin) {
            return ephemeris::BodyState{body, t, frame, {}};
        }
        if (body != id_ || frame.origin != celestial::bodies::solar_system_barycenter ||
            frame.axes != coordinates::FrameAxes::J2000) {
            throw std::invalid_argument("LinearEphemeris: unsupported body or frame");
        }
        return ephemeris::BodyState{
            body, t, frame,
            {at_j2000_ + velocity_ * t.seconds_since_j2000(), velocity_}};
    }

    [[nodiscard]] double gravitational_parameter(celestial::BodyId) const override { return 1.0; }
    [[nodiscard]] double mean_radius(celestial::BodyId) const override { return 0.0; }
    [[nodiscard]] ephemeris::CoverageWindow coverage(celestial::BodyId body) const override {
        return ephemeris::CoverageWindow{time::CoordinateTime::from_seconds_since_j2000(-1.0e12),
                                         time::CoordinateTime::from_seconds_since_j2000(1.0e12),
                                         body == id_};
    }
    [[nodiscard]] bool has_body(celestial::BodyId body) const override { return body == id_; }

private:
    celestial::BodyId id_;
    Vec3 at_j2000_;
    Vec3 velocity_;
};

propagation::IntegratorConfig orbital_config() {
    propagation::IntegratorConfig cfg{};
    cfg.relative_tolerance = 1.0e-10;
    cfg.absolute_tolerance_position = 1.0e-3;
    cfg.absolute_tolerance_velocity = 1.0e-6;
    cfg.initial_step = time::Duration::seconds(10.0);
    cfg.max_step = time::Duration::seconds(60.0);
    return cfg;
}

}  // namespace

TEST(the_relativistic_reference_matrix_matches_independent_long_double_algebra) {
    struct Potential {
        const char* name;
        long double u_over_c2;
    };
    // Independent GM/r evaluations, rounded only here for the validation grid.
    constexpr Potential potentials[] = {
        {"deep space", 0.0L},
        {"1 AU from Sun", 9.8706287e-9L},
        {"Mercury orbit", 2.5500e-8L},
        {"LEO", 6.5430e-10L},
        {"1000 km above Jupiter", 1.754e-8L},
    };
    constexpr long double betas[] = {0.0L, 0.01L, 0.1L, 0.5L, 0.9L, 0.99L, 0.999L};
    constexpr long double mass = 1000.0L;
    constexpr long double coordinate_interval = 1000.0L;

    for (const auto& location : potentials) {
        gravity::MetricSample sample{};
        const long double psi = location.u_over_c2;
        const long double a_ref = 1.0L - 2.0L * psi + 2.0L * psi * psi;
        const long double b_ref = 1.0L + 2.0L * psi;
        sample.a = static_cast<double>(a_ref);
        sample.b = static_cast<double>(b_ref);
        sample.local_light_speed = c * std::sqrt(sample.a / sample.b);

        for (const long double beta : betas) {
            const long double gamma = 1.0L / std::sqrt(1.0L - beta * beta);
            // beta is speed measured by a static local tetrad. Coordinate and
            // proper velocities contain the metric scale factors shown here.
            const long double expected_coordinate_v =
                static_cast<long double>(c) * beta * std::sqrt(a_ref / b_ref);
            const long double expected_u =
                static_cast<long double>(c) * gamma * beta / std::sqrt(b_ref);
            const long double expected_rate = std::sqrt(a_ref) / gamma;
            const long double expected_energy = mass * gamma *
                                                static_cast<long double>(units::c_squared);
            const long double expected_momentum = mass * gamma * beta * c;

            const Vec3 u{static_cast<double>(expected_u), 0.0, 0.0};
            const Vec3 v = sample.coordinate_velocity(u);
            const Vec3 u_roundtrip = sample.proper_velocity(v);
            const double local_gamma =
                std::sqrt(sample.a) * sample.time_component(u) / c;
            const double energy = static_cast<double>(mass) * local_gamma * units::c_squared;
            const double momentum = static_cast<double>(mass) * std::sqrt(sample.b) * u.norm();
            const double proper_elapsed = coordinate_interval * sample.proper_time_rate(u);

            std::ostringstream os;
            os << location.name << ", beta=" << static_cast<double>(beta)
               << ": v=" << v.norm() << " m/s, u=" << u.norm()
               << " m/s, gamma=" << local_gamma << ", tau=" << proper_elapsed
               << " s, t=" << coordinate_interval << " s, a_coord=0, a_proper=0, E="
               << energy << " J, p=" << momentum << " kg m/s";
            INFO(os.str());

            if (beta == 0.0L) {
                CHECK_NEAR_ABS(v.norm(), 0.0, 0.0,
                               "zero local speed has exactly zero coordinate speed");
                CHECK_NEAR_ABS(u_roundtrip.norm(), 0.0, 0.0,
                               "zero velocity round trip is exact");
            } else {
                CHECK_NEAR_REL(v.norm(), static_cast<double>(expected_coordinate_v), 3.0e-14,
                               "independent local-tetrad relation v=c beta sqrt(A/B)");
                CHECK_NEAR_REL(u_roundtrip.norm(), u.norm(), 2.0e-13,
                               "curved-space v->u->v round trip; this caught the former flat-space "
                               "conversion at the propagation boundary. At beta=.999 the inverse "
                               "contains the expected gamma^2 amplification of double roundoff");
            }
            CHECK_NEAR_REL(local_gamma, static_cast<double>(gamma), 3.0e-14,
                           "sqrt(A) u0/c is the Lorentz factor measured by a static observer");
            CHECK_NEAR_REL(sample.proper_time_rate(u), static_cast<double>(expected_rate),
                           3.0e-14, "dτ/dt=sqrt(A)/gamma");
            CHECK_NEAR_REL(energy, static_cast<double>(expected_energy), 3.0e-14,
                           "local energy gamma*m*c^2");
            if (beta == 0.0L) {
                CHECK_NEAR_ABS(momentum, 0.0, 0.0, "zero beta has exactly zero momentum");
            } else {
                CHECK_NEAR_REL(momentum, static_cast<double>(expected_momentum), 3.0e-14,
                               "local momentum gamma*m*beta*c");
            }
            CHECK_NEAR_ABS(sample.geodesic_acceleration(u).norm(), 0.0, 0.0,
                           "the matrix holds potential constant, so both accelerations are zero");
        }
    }
}

TEST(retarded_time_for_a_moving_source_matches_the_closed_form_up_to_point_nine_nine_c) {
    const celestial::BodyId target{12345};
    constexpr double distance = 2.0e9;
    const auto frame = coordinates::ReferenceFrame::ssb_j2000();
    const auto reception = time::CoordinateTime::j2000();

    for (const double beta : {-0.99, -0.5, 0.0, 0.5, 0.99}) {
        // Positive velocity means the target is farther away at later times, so
        // at emission it was closer: L = R/(c+v). This is a closed-form oracle,
        // not another iteration of the equation used by apparent_position().
        LinearEphemeris provider{target, Vec3{distance, 0.0, 0.0}, Vec3{beta * c, 0.0, 0.0}};
        const auto apparent = relativity::apparent_position(
            provider, target, Vec3{}, reception, frame, 1.0e-13, 1000);
        const double expected = distance / (c * (1.0 + beta));

        REQUIRE(apparent.converged);
        CHECK_NEAR_REL(apparent.light_time, expected, 2.0e-12,
                       "constant-velocity source has exact reception light time R/(c+v)");
        CHECK_NEAR_REL(apparent.body_position.x, distance - beta * c * expected, 2.0e-12,
                       "reported emission position is x(t_observer-light_time)");
        CHECK(apparent.retarded_epoch <= reception);
    }
}

TEST(render_fps_and_time_warp_partitioning_stay_within_the_same_physical_error_budget) {
    const celestial::BodyId earth = celestial::bodies::earth;
    sft::FixedPointMassProvider provider{earth, kEarthGm, 0.0};
    const std::vector<celestial::BodyId> ids{earth};
    const auto catalog = celestial::BodyCatalog::resolve(provider, ids);
    const gravity::PointMassGravity gravity_model{
        provider, catalog, coordinates::ReferenceFrame::ssb_j2000()};

    propagation::PropagationState initial{};
    initial.mass = 1000.0;
    initial.state.position = Vec3{7.0e6, 0.0, 0.0};
    initial.state.velocity = Vec3{0.0, trajectory::circular_speed(kEarthGm, 7.0e6), 0.0};

    constexpr double duration = 600.0;
    const auto exact = sft::kepler_propagate(initial.state, kEarthGm, duration);
    const auto t0 = time::CoordinateTime::j2000();

    struct Partition { double fps; double warp; };
    constexpr Partition partitions[] = {
        {30.0, 1.0}, {60.0, 10.0}, {144.0, 100.0},
        {60.0, 1000.0}, {60.0, 10000.0}, {60.0, 100000.0},
    };

    for (const auto p : partitions) {
        propagation::PropagationState state = initial;
        auto t = t0;
        const auto end = t0 + time::Duration::seconds(duration);
        const double nominal_chunk = p.warp / p.fps;
        std::size_t frames = 0;
        while (t < end) {
            const double left = (end - t).seconds();
            const double dt = std::min(left, nominal_chunk);
            propagation::DormandPrince54Propagator propagator{gravity_model, orbital_config()};
            const auto result = propagator.propagate(state, t, t + time::Duration::seconds(dt));
            REQUIRE(result.ok());
            state = result.state;
            t = result.time;
            ++frames;
        }

        const double position_error = (state.state.position - exact.position).norm();
        const double velocity_error = (state.state.velocity - exact.velocity).norm();
        std::ostringstream os;
        os << p.fps << " FPS at " << p.warp << "x: " << frames
           << " propagation requests, |dr|=" << position_error << " m, |dv|="
           << velocity_error << " m/s";
        INFO(os.str());
        CHECK_NEAR_ABS(position_error, 0.0, 0.1,
                       "all render/warp partitions are compared with the independent universal-"
                       "variable Kepler solution, not with another integrator run");
        CHECK_NEAR_ABS(velocity_error, 0.0, 1.0e-4,
                       "same physical budget for velocity across partitionings");
        CHECK(t == end);
    }
}

TEST(the_propagator_is_bit_deterministic_and_rejects_invalid_runtime_states) {
    const NoForce no_force;
    propagation::IntegratorConfig cfg{};
    cfg.kinematics = propagation::Kinematics::SpecialRelativistic;
    cfg.relative_tolerance = 1.0e-12;
    cfg.max_step = time::Duration::days(10.0);

    propagation::PropagationState initial{};
    initial.mass = 1000.0;
    initial.state.position = Vec3{1.0e11, -2.0e11, 3.0e11};
    initial.state.velocity = Vec3{0.999 * c, 0.0, 0.0};
    const auto t0 = time::CoordinateTime::j2000();
    const auto t1 = t0 + time::Duration::julian_years(2.0);

    propagation::PropagationResult reference{};
    for (int run = 0; run < 10; ++run) {
        propagation::DormandPrince54Propagator propagator{no_force, cfg};
        auto previous_coordinate_time = t0;
        double previous_proper_time = initial.proper_time.seconds();
        bool clocks_monotonic = true;
        propagator.set_step_observer([&](const propagation::StepInfo& step) {
            clocks_monotonic = clocks_monotonic && step.time > previous_coordinate_time &&
                               step.state.proper_time.seconds() >= previous_proper_time;
            previous_coordinate_time = step.time;
            previous_proper_time = step.state.proper_time.seconds();
        });
        const auto result = propagator.propagate(initial, t0, t1);
        REQUIRE(result.ok());
        CHECK(clocks_monotonic);
        CHECK(result.time == t1);
        CHECK(result.state.state == (run == 0 ? result.state.state : reference.state.state));
        CHECK_EQ(result.state.proper_time.seconds(),
                 run == 0 ? result.state.proper_time.seconds()
                          : reference.state.proper_time.seconds());
        CHECK(result.state.state.velocity.norm() <= c);
        CHECK(result.state.mass > 0.0);
        CHECK(result.state.attitude.orientation.is_finite());
        CHECK_NEAR_ABS(result.state.attitude.orientation.norm(), 1.0, 1.0e-15,
                       "unit quaternion remains valid during the long beta=0.999 coast");
        reference = result;
    }

    auto bad_mass = initial;
    bad_mass.mass = 0.0;
    propagation::DormandPrince54Propagator mass_guard{no_force, cfg};
    CHECK(mass_guard.propagate(bad_mass, t0, t1).status ==
          propagation::PropagationStatus::InvariantViolation);

    auto bad_quaternion = initial;
    bad_quaternion.attitude.orientation = math::Quaternion{2.0, 0.0, 0.0, 0.0};
    propagation::DormandPrince54Propagator quaternion_guard{no_force, cfg};
    CHECK(quaternion_guard.propagate(bad_quaternion, t0, t1).status ==
          propagation::PropagationStatus::InvariantViolation);

    const auto non_finite_time =
        time::CoordinateTime::from_seconds_since_j2000(
            std::numeric_limits<double>::quiet_NaN());
    propagation::DormandPrince54Propagator time_guard{no_force, cfg};
    CHECK(time_guard.propagate(initial, t0, non_finite_time).status ==
          propagation::PropagationStatus::InvariantViolation);
}

TEST(the_static_metric_rejects_a_velocity_inside_c_but_outside_its_local_light_cone) {
    gravity::MetricSample sample{};
    sample.a = 0.8;
    sample.b = 1.2;
    sample.local_light_speed = c * std::sqrt(sample.a / sample.b);
    const Vec3 spacelike{0.9 * c, 0.0, 0.0};
    REQUIRE(spacelike.norm() < c);
    CHECK(!sample.is_timelike(spacelike));
    CHECK_THROWS_AS(sample.proper_velocity(spacelike), std::domain_error);
}

TEST(stress_trajectories_remain_finite_subluminal_and_physically_bounded) {
    constexpr double earth_radius = 6.378137e6;
    const celestial::BodyId earth = celestial::bodies::earth;
    sft::FixedPointMassProvider provider{earth, kEarthGm, earth_radius};
    const auto catalog = celestial::BodyCatalog::resolve(
        provider, std::vector<celestial::BodyId>{earth});
    const gravity::PointMassGravity gravity_model{
        provider, catalog, coordinates::ReferenceFrame::ssb_j2000()};

    auto cfg = orbital_config();
    cfg.relative_tolerance = 1.0e-9;
    cfg.max_step = time::Duration::seconds(3600.0);
    cfg.minimum_mass = 500.0;
    const auto t0 = time::CoordinateTime::j2000();

    struct Case {
        const char* name;
        double radius;
        double speed;
        double duration;
    };
    constexpr double eccentricity = 0.99;
    constexpr double eccentric_periapsis = earth_radius + 300.0e3;
    const double eccentric_semimajor = eccentric_periapsis / (1.0 - eccentricity);
    const double eccentric_period =
        units::two_pi * std::sqrt(std::pow(eccentric_semimajor, 3) / kEarthGm);
    const double eccentric_speed =
        std::sqrt(kEarthGm * (1.0 + eccentricity) / eccentric_periapsis);
    const double near_escape_radius = 7.0e6;
    const double near_escape_speed =
        std::sqrt(2.0 * kEarthGm / near_escape_radius) * (1.0 - 1.0e-9);
    const double flyby_periapsis = earth_radius + 10.0e3;
    const double flyby_speed = 1.2 * std::sqrt(2.0 * kEarthGm / flyby_periapsis);

    const Case cases[] = {
        {"e=0.99 full orbit", eccentric_periapsis, eccentric_speed, eccentric_period},
        {"one part per billion below escape", near_escape_radius, near_escape_speed,
         5.0 * units::seconds_per_day},
        {"10 km altitude hyperbolic flyby", flyby_periapsis, flyby_speed,
         6.0 * units::seconds_per_hour},
    };

    for (const auto& scenario : cases) {
        propagation::PropagationState initial{};
        initial.mass = 1000.0;
        initial.state.position = Vec3{scenario.radius, 0.0, 0.0};
        initial.state.velocity = Vec3{0.0, scenario.speed, 0.0};
        propagation::DormandPrince54Propagator propagator{gravity_model, cfg};
        const auto result = propagator.propagate(
            initial, t0, t0 + time::Duration::seconds(scenario.duration));
        INFO(scenario.name);
        REQUIRE(result.ok());
        CHECK(result.state.is_finite());
        CHECK(result.state.state.velocity.norm() < c);
        CHECK(result.state.mass >= cfg.minimum_mass);
        CHECK(result.state.proper_time.seconds() >= 0.0);
        CHECK(result.state.attitude.orientation.is_finite());
        CHECK_NEAR_ABS(result.state.attitude.orientation.norm(), 1.0, 1.0e-15,
                       "unforced attitude remains a valid unit quaternion");
    }

    // Much closer to c and much longer than the reference matrix: this is a
    // numerical safety test, not a claim that the weak-field gravity model is
    // valid at this speed.
    propagation::IntegratorConfig relativistic_cfg{};
    relativistic_cfg.kinematics = propagation::Kinematics::SpecialRelativistic;
    relativistic_cfg.relative_tolerance = 1.0e-12;
    relativistic_cfg.max_step = time::Duration::days(30.0);
    propagation::PropagationState relativistic_initial{};
    relativistic_initial.mass = 1000.0;
    relativistic_initial.state.velocity = Vec3{0.999999999 * c, 0.0, 0.0};
    const NoForce no_force;
    propagation::DormandPrince54Propagator relativistic_propagator{
        no_force, relativistic_cfg};
    const auto long_result = relativistic_propagator.propagate(
        relativistic_initial, t0, t0 + time::Duration::julian_years(100.0));
    REQUIRE(long_result.ok());
    CHECK(long_result.state.is_finite());
    CHECK(long_result.state.state.velocity.norm() < c);
    CHECK(long_result.state.mass > 0.0);
    CHECK(long_result.state.proper_time.seconds() > 0.0);
    CHECK(long_result.time > t0);
}
