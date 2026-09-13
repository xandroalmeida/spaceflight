// The snapshot: everything a display needs, computed once, from the real state.

#include "core/gravity/point_mass_gravity.hpp"
#include "core/render/render_transform.hpp"
#include "core/simulation/snapshot.hpp"
#include "core/trajectory/orbital_elements.hpp"
#include "core/units/constants.hpp"
#include "tests/support/kernel_fixture.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <sstream>
#include <vector>

using namespace sf;
using sf::math::Vec3;

namespace {
const auto kSsb = coordinates::ReferenceFrame::ssb_j2000();
}

TEST(a_snapshot_carries_what_the_cockpit_asks_for) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t0 = fixture.time->parse("2026-01-01 00:00:00 TDB");

    const auto catalog = celestial::BodyCatalog::default_solar_system(*fixture.provider);
    const gravity::PointMassGravity forces{*fixture.provider, catalog, kSsb};

    const auto earth = fixture.provider->state(celestial::bodies::earth, t0, kSsb);
    const double gm = fixture.provider->gravitational_parameter(celestial::bodies::earth);
    const double radius = 6.778e6;
    const double speed = trajectory::circular_speed(gm, radius);

    propagation::PropagationState state{};
    state.state.position = earth.state.position + Vec3{radius, 0.0, 0.0};
    state.state.velocity = earth.state.velocity + Vec3{0.0, speed * 0.6, speed * 0.8};
    state.mass = 5000.0;
    state.proper_time = time::Duration::seconds(1234.5);

    // The state belongs to t0, so the snapshot is built at t0.  The scenario
    // epoch sits 1234.5 s earlier, which is what makes `elapsed` non-zero without
    // pretending the ship is somewhere it is not -- the first version of this test
    // built the snapshot 1234.5 s in the future from a state that had not moved,
    // and the Earth had travelled 37 000 km in the meantime.
    const auto epoch = t0 - time::Duration::seconds(1234.5);
    simulation::SnapshotBuilder builder{*fixture.provider, catalog, forces,
                                        celestial::bodies::earth, epoch, kSsb};
    builder.set_target(celestial::bodies::moon);
    builder.set_propulsion(2000.0, 8993.8);
    builder.set_time_warp(100.0);

    const auto snapshot = builder.build(state, t0);
    INFO(snapshot.describe());

    CHECK_EQ(snapshot.bodies.size(), catalog.size());
    CHECK(snapshot.find(celestial::bodies::earth) != nullptr);
    CHECK(snapshot.find(celestial::bodies::moon) != nullptr);
    CHECK(snapshot.find(celestial::BodyId{987654}) == nullptr);

    CHECK_NEAR_REL(snapshot.spacecraft.distance_to_reference, radius, 1.0e-12,
                   "the ship was placed exactly this far from the Earth's centre; the snapshot "
                   "recovers it through one subtraction of two ~1.5e11 m barycentric positions, "
                   "whose ulp is 3.3e-5 m");
    CHECK_NEAR_REL(snapshot.spacecraft.speed, state.state.velocity.norm(), 0.0,
                   "the barycentric speed is copied, not recomputed");

    // Altitude is measured above the mean radius, not from the centre.
    const double mean_radius = fixture.provider->mean_radius(celestial::bodies::earth);
    CHECK_NEAR_REL(snapshot.spacecraft.altitude, radius - mean_radius, 1.0e-9,
                   "altitude = distance - mean radius, both already checked above");
    CHECK(snapshot.spacecraft.altitude > 4.0e5);
    CHECK(snapshot.spacecraft.altitude < 4.1e5);

    // Osculating elements about the reference body, not about the barycentre.
    CHECK_NEAR_REL(snapshot.spacecraft.elements.semi_major_axis, radius, 1.0e-6,
                   "a circular orbit by construction, so a = r; the 1e-6 covers the third-body "
                   "terms already present in the relative velocity");
    CHECK(snapshot.spacecraft.elements.eccentricity < 1.0e-6);

    // Propulsion readouts.
    CHECK_NEAR_REL(snapshot.spacecraft.propellant, 3000.0, 0.0, "5000 kg total - 2000 kg dry");
    CHECK_NEAR_REL(snapshot.spacecraft.delta_v_budget, 8993.8 * std::log(5000.0 / 2000.0), 1.0e-12,
                   "Tsiolkovsky on the mass the snapshot was handed");
    CHECK_EQ(snapshot.spacecraft.thrust, 0.0);

    // Target readouts (rule 25).
    REQUIRE(snapshot.spacecraft.target.has_value());
    const auto* moon = snapshot.find(celestial::bodies::moon);
    REQUIRE(moon != nullptr);
    CHECK_NEAR_REL(snapshot.spacecraft.target_distance,
                   (state.state.position - moon->position).norm(), 1.0e-9,
                   "the same subtraction the snapshot did, redone here");
    CHECK(snapshot.spacecraft.target_distance > 3.5e8);
    CHECK(snapshot.spacecraft.target_distance < 4.1e8);

    // Clocks.
    CHECK_NEAR_ABS(snapshot.elapsed_coordinate.seconds(), 1234.5, 0.0, "t - epoch, exactly");
    CHECK_NEAR_ABS(snapshot.clock_difference.seconds(), 0.0, 1.0e-12,
                   "coordinate and proper time are equal in the Newtonian regime; the difference "
                   "is a subtraction of two identical stored values");
    CHECK_EQ(snapshot.time_warp, 100.0);
}

TEST(the_relativity_readouts_are_present_and_newtonian) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t0 = fixture.time->parse("2026-01-01 00:00:00 TDB");

    const auto catalog = celestial::BodyCatalog::default_solar_system(*fixture.provider);
    const gravity::PointMassGravity forces{*fixture.provider, catalog, kSsb};

    propagation::PropagationState state{};
    state.state.position =
        fixture.provider->position(celestial::bodies::earth, t0, kSsb) + Vec3{6.778e6, 0.0, 0.0};
    state.state.velocity = Vec3{0.0, 3.0e4, 0.0};  // barycentric, Earth-like
    state.mass = 1000.0;

    simulation::SnapshotBuilder builder{*fixture.provider, catalog, forces,
                                        celestial::bodies::earth, t0, kSsb};
    const auto snapshot = builder.build(state, t0);

    std::ostringstream os;
    os << "beta = " << snapshot.spacecraft.beta << ", gamma - 1 = "
       << snapshot.spacecraft.lorentz_factor - 1.0;
    INFO(os.str());

    CHECK_NEAR_REL(snapshot.spacecraft.beta, 3.0e4 / units::c, 1.0e-15,
                   "beta = v/c with the speed the state carries");
    const double beta = 3.0e4 / units::c;
    CHECK_NEAR_REL(snapshot.spacecraft.lorentz_factor - 1.0, 0.5 * beta * beta, 1.0e-7,
                   "Two effects, and the smaller one is the physics. (1) Truncation: "
                   "gamma - 1 = beta^2/2 + 3 beta^4/8 + ..., and at beta = 1.00069e-4 the second "
                   "term is 7.5e-9 relative. (2) Cancellation, which dominates: gamma is "
                   "1.000000005, and subtracting 1 from it leaves ~5e-9 carrying the absolute "
                   "error of a number near 1, i.e. 2.2e-16, which is 4.4e-8 RELATIVE to the "
                   "answer. Measured: 1.3e-8. This is the same trap that "
                   "docs/physics/relativity-roadmap.md section 3.1 describes from the other end -- "
                   "there, gamma computed from v loses digits as beta -> 1; here, gamma - 1 loses "
                   "them as beta -> 0. A cockpit that needs gamma - 1 at low speed must compute it "
                   "as beta^2/2 directly, not by subtracting");
    CHECK(snapshot.spacecraft.lorentz_factor > 1.0);
}

TEST(a_snapshot_projects_into_the_renderer_without_touching_the_state) {
    const auto fixture = sft::load_spice_or_skip();
    const auto t0 = fixture.time->parse("2026-01-01 00:00:00 TDB");

    const auto catalog = celestial::BodyCatalog::default_solar_system(*fixture.provider);
    const gravity::PointMassGravity forces{*fixture.provider, catalog, kSsb};

    propagation::PropagationState state{};
    state.state.position =
        fixture.provider->position(celestial::bodies::earth, t0, kSsb) + Vec3{6.778e6, 0.0, 0.0};
    state.state.velocity = Vec3{0.0, 3.0e4, 0.0};
    state.mass = 1000.0;

    simulation::SnapshotBuilder builder{*fixture.provider, catalog, forces,
                                        celestial::bodies::earth, t0, kSsb};
    const auto snapshot = builder.build(state, t0);

    // Two cameras, wildly different origins.  The physics must be identical and
    // the recovered absolute positions must agree to within each projection's own
    // resolution -- which is the quantitative statement of "floating origin does
    // not move anything".
    render::RenderTransform near_camera{1.0e-3};
    near_camera.set_camera_origin(snapshot.spacecraft.position + Vec3{-500.0, 0.0, 0.0});

    render::RenderTransform far_camera{1.0e-9};
    far_camera.set_camera_origin(Vec3{});

    const auto near_render = near_camera.to_render(snapshot.spacecraft.position);
    const auto far_render = far_camera.to_render(snapshot.spacecraft.position);

    const Vec3 from_near = near_camera.to_absolute(near_render);
    const Vec3 from_far = far_camera.to_absolute(far_render);

    std::ostringstream os;
    os << "near camera recovers the ship to " << (from_near - snapshot.spacecraft.position).norm()
       << " m (resolution " << near_camera.resolution_at(snapshot.spacecraft.position)
       << " m); far camera to " << (from_far - snapshot.spacecraft.position).norm()
       << " m (resolution " << far_camera.resolution_at(snapshot.spacecraft.position) << " m)";
    INFO(os.str());

    CHECK((from_near - snapshot.spacecraft.position).norm() <=
          2.0 * near_camera.resolution_at(snapshot.spacecraft.position) + 1.0e-9);
    CHECK((from_far - snapshot.spacecraft.position).norm() <=
          2.0 * far_camera.resolution_at(snapshot.spacecraft.position) + 1.0e-9);

    // The far camera is thousands of times worse, which is exactly why the
    // renderer re-centres.
    CHECK(far_camera.resolution_at(snapshot.spacecraft.position) >
          1000.0 * near_camera.resolution_at(snapshot.spacecraft.position));

    // And the snapshot itself is untouched by any of it.
    const auto again = builder.build(state, t0);
    CHECK_NEAR_ABS((again.spacecraft.position - snapshot.spacecraft.position).norm(), 0.0, 0.0,
                   "building a snapshot twice from the same state gives the same numbers, and "
                   "projecting one of them changed nothing: there is no write path");
}
