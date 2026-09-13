// The differential corrector, exercised on a map whose inverse is known.
//
// The arrival function here is a two-body Kepler propagation, so the answer the
// corrector must find is exactly the velocity that generated the reference arc.
// No kernels, no integrator, no ambiguity about what "correct" means.

#include "core/navigation/targeting.hpp"
#include "core/math/mat3.hpp"
#include "core/units/constants.hpp"
#include "tests/support/kepler.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

using namespace sf;
using sf::coordinates::StateVector;
using sf::math::Mat3;
using sf::math::Vec3;

namespace {
constexpr double kGm = 3.9860043550702266e14;
}

TEST(mat3_solves_and_reports_singular_systems) {
    const Mat3 m = Mat3::from_columns(Vec3{2.0, 0.0, 0.0}, Vec3{0.0, 4.0, 0.0},
                                      Vec3{0.0, 0.0, 8.0});
    const auto solution = m.solve(Vec3{2.0, 4.0, 8.0});
    REQUIRE(solution.ok);
    CHECK_NEAR_ABS((solution.x - Vec3{1.0, 1.0, 1.0}).norm(), 0.0, 0.0,
                   "a diagonal system with exactly representable entries: every division is exact");

    CHECK_NEAR_REL(m.determinant(), 64.0, 0.0, "product of the diagonal, exactly");

    // Two identical columns: the system has no unique solution and must say so
    // rather than return infinities.
    const Mat3 singular = Mat3::from_columns(Vec3{1.0, 2.0, 3.0}, Vec3{1.0, 2.0, 3.0},
                                             Vec3{4.0, 5.0, 6.0});
    CHECK(!singular.solve(Vec3{1.0, 1.0, 1.0}).ok);
}

TEST(the_corrector_recovers_a_known_departure_velocity) {
    const double a = 1.5e7;
    const double e = 0.3;
    const double rp = a * (1.0 - e);
    const double vp = std::sqrt(kGm * (1.0 + e) / (a * (1.0 - e)));
    const double dt = 0.4 * 2.0 * units::pi * std::sqrt(a * a * a / kGm);

    const Vec3 departure{rp, 0.0, 0.0};
    const Vec3 truth{0.0, vp * 0.8, vp * 0.6};
    const Vec3 target = sft::kepler_propagate(StateVector{departure, truth}, kGm, dt).position;

    int calls = 0;
    const auto arrival = [&](const Vec3& velocity) {
        ++calls;
        return sft::kepler_propagate(StateVector{departure, velocity}, kGm, dt).position;
    };

    // Start 30 m/s off in every component -- a large error for this problem.
    const Vec3 guess = truth + Vec3{30.0, -30.0, 30.0};

    navigation::TargetingConfig config{};
    config.position_tolerance = 1.0e-3;  // one millimetre
    config.velocity_step = 1.0e-4;

    const auto result = navigation::correct_departure(arrival, guess, target, config);

    std::ostringstream os;
    os << "initial miss " << result.initial_miss << " m -> " << result.miss_distance << " m in "
       << result.iterations << " iterations (" << result.evaluations << " trajectories, " << calls
       << " calls)";
    INFO(os.str());

    CHECK(result.converged);
    CHECK_NEAR_ABS(result.miss_distance, 0.0, 1.0e-3,
                   "the requested tolerance; the map is smooth and invertible here, so Newton "
                   "reaches it in a handful of iterations");
    CHECK_NEAR_ABS((result.departure_velocity - truth).norm(), 0.0, 1.0e-6,
                   "having matched the arrival position to a millimetre on a well-conditioned "
                   "two-body arc, the recovered velocity must match the true one to the "
                   "corresponding accuracy: 1e-3 m of position over ~2000 s of flight is 1e-6 m/s");
    CHECK(result.iterations <= 6);
    CHECK_EQ(result.evaluations, calls);
}

TEST(the_departure_corrector_is_bit_deterministic) {
    const double a = 1.5e7;
    const double e = 0.3;
    const double rp = a * (1.0 - e);
    const double vp = std::sqrt(kGm * (1.0 + e) / (a * (1.0 - e)));
    const double dt = 0.4 * 2.0 * units::pi * std::sqrt(a * a * a / kGm);
    const Vec3 departure{rp, 0.0, 0.0};
    const Vec3 truth{0.0, vp * 0.8, vp * 0.6};
    const Vec3 target = sft::kepler_propagate(StateVector{departure, truth}, kGm, dt).position;
    const auto arrival = [&](const Vec3& velocity) {
        return sft::kepler_propagate(StateVector{departure, velocity}, kGm, dt).position;
    };

    navigation::TargetingConfig config{};
    config.position_tolerance = 1.0e-3;
    config.velocity_step = 1.0e-4;
    const Vec3 guess = truth + Vec3{30.0, -30.0, 30.0};
    const auto reference = navigation::correct_departure(arrival, guess, target, config);
    REQUIRE(reference.converged);

    for (int run = 0; run < 20; ++run) {
        const auto result = navigation::correct_departure(arrival, guess, target, config);
        CHECK_EQ(result.departure_velocity.x, reference.departure_velocity.x);
        CHECK_EQ(result.departure_velocity.y, reference.departure_velocity.y);
        CHECK_EQ(result.departure_velocity.z, reference.departure_velocity.z);
        CHECK_EQ(result.miss_distance, reference.miss_distance);
        CHECK_EQ(result.initial_miss, reference.initial_miss);
        CHECK_EQ(result.iterations, reference.iterations);
        CHECK_EQ(result.evaluations, reference.evaluations);
        CHECK_EQ(result.converged, reference.converged);
        CHECK_EQ(result.message, reference.message);
    }
}

TEST(the_corrector_reports_failure_instead_of_wandering) {
    const Vec3 target{1.0e7, 0.0, 0.0};

    // An arrival that ignores the departure velocity entirely: the Jacobian is
    // identically zero, so there is nothing to invert.
    const auto insensitive = [&](const Vec3&) { return Vec3{0.0, 0.0, 0.0}; };

    navigation::TargetingConfig config{};
    config.position_tolerance = 1.0;
    const auto result = navigation::correct_departure(insensitive, Vec3{1.0, 0.0, 0.0}, target, config);

    CHECK(!result.converged);
    INFO("reported: " + result.message);
    CHECK(result.message.find("singular") != std::string::npos);

    // And a map that is sensitive but wildly nonlinear: the corrector must stop
    // when backtracking stops helping, not thrash.
    const auto nasty = [&](const Vec3& v) {
        return Vec3{std::sin(v.x) * 1.0e7, std::sin(v.y) * 1.0e7, std::sin(v.z) * 1.0e7};
    };
    const auto stubborn =
        navigation::correct_departure(nasty, Vec3{3.0, 3.0, 3.0}, Vec3{2.0e7, 0.0, 0.0}, config);
    CHECK(!stubborn.converged);
    CHECK(stubborn.evaluations < 500);
    INFO("reported: " + stubborn.message);
}

TEST(the_corrector_validates_its_inputs) {
    const auto arrival = [](const Vec3& v) { return v; };

    navigation::TargetingConfig bad{};
    bad.position_tolerance = 0.0;
    CHECK_THROWS_AS(navigation::correct_departure(arrival, Vec3{}, Vec3{}, bad),
                    std::invalid_argument);

    bad = navigation::TargetingConfig{};
    bad.max_iterations = 0;
    CHECK_THROWS_AS(navigation::correct_departure(arrival, Vec3{}, Vec3{}, bad),
                    std::invalid_argument);

    CHECK_THROWS_AS(navigation::correct_departure({}, Vec3{}, Vec3{}), std::invalid_argument);
}
