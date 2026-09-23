// Regression: the numbers this build produces, pinned.
//
// These values are NOT independent truth -- test_spice_positions.cpp and
// test_two_body.cpp are where correctness is established.  This file answers a
// different question: did anything change?  A kernel swap, a refactor of the
// frame code, a compiler change or a reordering of the force summation all show
// up here first.
//
// Regenerate deliberately, never casually, and say why in the commit message:
//     ctest --test-dir build -R regression --output-on-failure
// prints the current values with full precision next to the pinned ones.

#include "core/celestial/body_catalog.hpp"
#include "core/gravity/point_mass_gravity.hpp"
#include "core/propagation/dormand_prince_54.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <iomanip>
#include <sstream>

using namespace sf;
using sf::math::Vec3;

namespace {

constexpr const char* kEpoch = "2026-01-01 00:00:00 TDB";

// Pinned on 2026-09-13, de440s.bsp + gm_de440.tpc + pck00011.tpc,
// Apple clang 21, RelWithDebInfo, arm64.
constexpr double kEarthPosition[3] = {-26531002415.592987, 132064399546.26964, 57268703374.963318};
constexpr double kMoonPosition[3] = {-26386676685.840954, 132353983702.95195, 57428862297.575218};
constexpr double kSunPosition[3] = {-458863967.4035421, -767304103.76017404, -311195587.25813758};

// One LEO orbit under the full ten-body catalogue; see the test for the setup.
//
// Re-pinned 2026-09-13 (Milestone 1): mass became an error-controlled state
// component when propulsion was added (docs/physics/propulsion-model.md 6.1).
// With no engine the mass error is identically zero, but the RMS norm now
// averages over seven components instead of six, which makes the estimate
// smaller by sqrt(6/7) and the steps correspondingly longer: 325 -> 317 accepted
// steps, and a final position 1.4e-3 m away -- three orders below the 1.5e-9 m
// resolution of the orbit itself, i.e. the same trajectory reached differently.
// Previous values: {6777999.9974174500, -7.3168182373046875, -5.5634918212890625},
// speed 7668.6356085481330, 325 steps.
//
// The LEO pin is per platform; the body positions above are not.  SPICE reads
// the same kernel bits everywhere, but the propagator's arithmetic does not:
// Apple clang on arm64 contracts a*b+c into FMA by default and links Apple's
// libm, while GCC on x86_64 (baseline ISA, no FMA) rounds every product.  The
// last-bit differences move the error estimate across an accept/reject boundary
// twice -- 317 -> 319 steps -- and land 0.97 mm and 6e-11 (relative) in speed
// from the arm64 values: the same trajectory reached differently, which is
// exactly what this file must not blur into its tolerance.
#if defined(__x86_64__) && defined(__linux__) && defined(__GNUC__) && !defined(__clang__)
// Pinned 2026-09-22, GCC 13.3, RelWithDebInfo, x86_64 Linux (glibc).
constexpr double kLeoFinalRelative[3] = {6777999.9984054565, -7.3178253173828125, -5.5649642944335938};
constexpr double kLeoFinalSpeed = 7668.635608084026;
constexpr std::size_t kLeoAcceptedSteps = 319;
#else
constexpr double kLeoFinalRelative[3] = {6777999.9987983704, -7.3172760009765625, -5.5642623901367188};
constexpr double kLeoFinalSpeed = 7668.6356076058055;
constexpr std::size_t kLeoAcceptedSteps = 317;
#endif

std::string full_precision(const Vec3& v) {
    std::ostringstream os;
    os << std::setprecision(17) << "{" << v.x << ", " << v.y << ", " << v.z << "}";
    return os.str();
}

}  // namespace

TEST(pinned_body_positions_have_not_moved) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t = fixture.time->parse(kEpoch);
    const auto ssb = coordinates::ReferenceFrame::ssb_j2000();

    const struct {
        const char* name;
        celestial::BodyId body;
        const double* pinned;
    } cases[] = {
        {"Earth", celestial::bodies::earth, kEarthPosition},
        {"Moon", celestial::bodies::moon, kMoonPosition},
        {"Sun", celestial::bodies::sun, kSunPosition},
    };

    for (const auto& c : cases) {
        const Vec3 current = fixture.provider->state(c.body, t, ssb).state.position;
        const Vec3 pinned{c.pinned[0], c.pinned[1], c.pinned[2]};

        INFO(std::string{c.name} + " current: " + full_precision(current));

        CHECK_NEAR_ABS((current - pinned).norm(), 0.0, 1.0e-3,
                       "same kernels, same code, same machine: the result should be bit-identical. "
                       "The 1 mm bound allows only for a change in the order of floating point "
                       "operations (for instance a different optimisation level), which cannot "
                       "move a 1.5e11 m position by more than a few ulp (3.3e-5 m). Anything "
                       "larger means the DATA or the MEANING changed, and that must be deliberate");
    }
}

TEST(pinned_leo_propagation_has_not_changed) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t0 = fixture.time->parse(kEpoch);
    const auto ssb = coordinates::ReferenceFrame::ssb_j2000();

    const auto earth = fixture.provider->state(celestial::bodies::earth, t0, ssb);
    const double gm_earth = fixture.provider->gravitational_parameter(celestial::bodies::earth);

    const double radius = 6.778e6;
    const double speed = trajectory::circular_speed(gm_earth, radius);
    const double period = trajectory::circular_period(gm_earth, radius);

    propagation::PropagationState initial{};
    initial.state.position = earth.state.position + Vec3{radius, 0.0, 0.0};
    initial.state.velocity = earth.state.velocity + Vec3{0.0, speed * 0.6, speed * 0.8};
    initial.mass = 1000.0;

    const auto catalog = celestial::BodyCatalog::default_solar_system(*fixture.provider);
    const gravity::PointMassGravity model{*fixture.provider, catalog, ssb};

    propagation::IntegratorConfig cfg{};
    cfg.relative_tolerance = 1.0e-12;
    cfg.absolute_tolerance_position = 1.0e-6;
    cfg.absolute_tolerance_velocity = 1.0e-9;
    cfg.initial_step = time::Duration::seconds(10.0);
    cfg.max_step = time::Duration::seconds(300.0);

    propagation::DormandPrince54Propagator propagator{model, cfg};
    const auto t1 = t0 + time::Duration::seconds(period);
    const auto result = propagator.propagate(initial, t0, t1);
    REQUIRE(result.ok());

    const auto earth_final = fixture.provider->state(celestial::bodies::earth, t1, ssb);
    const Vec3 relative = result.state.state.position - earth_final.state.position;
    const double relative_speed = (result.state.state.velocity - earth_final.state.velocity).norm();

    INFO("relative position: " + full_precision(relative));
    std::ostringstream os;
    os << std::setprecision(17) << "relative speed: " << relative_speed
       << ", accepted steps: " << result.stats.accepted_steps;
    INFO(os.str());

    const Vec3 pinned{kLeoFinalRelative[0], kLeoFinalRelative[1], kLeoFinalRelative[2]};
    CHECK_NEAR_ABS((relative - pinned).norm(), 0.0, 1.0e-3,
                   "the whole pipeline is deterministic -- fixed summation order in the force "
                   "model, fixed step control, no parallelism, no seeds -- so a rerun on this "
                   "machine reproduces these digits exactly. The 1 mm of slack is for the "
                   "cancellation in `relative`: subtracting two ~1.5e11 m barycentric positions "
                   "has an ulp of 3.3e-5 m, so a different optimisation level can legitimately "
                   "move the last digits. Anything larger is a change of behaviour");
    CHECK_NEAR_ABS(relative_speed, kLeoFinalSpeed, 1.0e-9,
                   "same determinism argument applied to the speed, where the cancellation of two "
                   "~3e4 m/s barycentric velocities has an ulp of 7e-12 m/s");
    CHECK_EQ(result.stats.accepted_steps, kLeoAcceptedSteps);
}
