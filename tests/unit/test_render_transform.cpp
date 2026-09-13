// The double-to-float boundary.
//
// These tests do not check that the arithmetic is what the code says it is --
// they demonstrate the failure the design exists to prevent, and then show that
// the design prevents it.  See docs/architecture/rendering.md.

#include "core/render/render_transform.hpp"
#include "core/units/constants.hpp"
#include "tests/support/test_harness.hpp"

#include <limits>
#include <sstream>
#include <stdexcept>

using sf::math::Vec3;
using sf::render::RenderTransform;
using sf::render::RenderVec3;

namespace {

constexpr double kAu = 1.495978707e11;
constexpr double kFloatEpsilon = static_cast<double>(std::numeric_limits<float>::epsilon());

bool identical(const RenderVec3& a, const RenderVec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

}  // namespace

TEST(without_a_floating_origin_a_kilometre_disappears) {
    // The camera sits at the barycentre; the ship is one astronomical unit away.
    // This is the naive arrangement, and it is unusable.
    RenderTransform transform{1.0e-7};
    transform.set_camera_origin(Vec3{});

    const Vec3 ship{kAu, 0.0, 0.0};
    const Vec3 ship_moved{kAu + 1000.0, 0.0, 0.0};  // one kilometre later

    const auto a = transform.to_render(ship);
    const auto b = transform.to_render(ship_moved);

    std::ostringstream os;
    os << "camera at the barycentre, ship at 1 au: 1 km of motion renders as "
       << (static_cast<double>(b.x) - static_cast<double>(a.x)) / 1.0e-7
       << " m; the float resolution there is "
       << transform.resolution_at(ship) << " m";
    INFO(os.str());

    CHECK(identical(a, b));
    CHECK_NEAR_REL(transform.resolution_at(ship), kAu * kFloatEpsilon, 1.0e-12,
                   "resolution = distance * FLT_EPSILON = 1.496e11 * 1.19e-7 = 1.78e4 m. A "
                   "kilometre is a hundredth of one ulp, so it cannot possibly survive");
    CHECK(transform.resolution_at(ship) > 1.0e4);
}

TEST(with_the_camera_alongside_the_ship_a_millimetre_survives) {
    // Same ship, same astronomical unit, same float. The only change is where the
    // origin of the projection sits.
    RenderTransform transform{1.0e-7};

    const Vec3 ship{kAu, 0.0, 0.0};
    transform.set_camera_origin(ship + Vec3{-100.0, 0.0, 0.0});  // 100 m behind

    const Vec3 moved = ship + Vec3{0.001, 0.0, 0.0};  // one millimetre

    const auto a = transform.to_render(ship);
    const auto b = transform.to_render(moved);

    std::ostringstream os;
    os << "camera 100 m from the ship at 1 au: 1 mm of motion renders as "
       << (static_cast<double>(b.x) - static_cast<double>(a.x)) / 1.0e-7
       << " m; resolution there is " << transform.resolution_at(ship) << " m";
    INFO(os.str());

    CHECK(!identical(a, b));
    CHECK_NEAR_ABS(transform.resolution_at(ship), 100.0 * kFloatEpsilon, 1.0e-12,
                   "resolution depends ONLY on the distance from the camera: 100 m * 1.19e-7 = "
                   "1.2e-5 m. The ship's absolute distance from the barycentre is irrelevant, "
                   "because it was subtracted in double before the float ever saw it");
}

TEST(the_scale_factor_does_not_buy_precision) {
    // A tempting mistake: "use a smaller scale and the numbers fit better".  The
    // float is relative, so scaling moves the exponent and leaves the significand
    // exactly where it was.
    const Vec3 ship{kAu, 0.0, 0.0};
    const Vec3 camera = ship + Vec3{-1.0e6, 0.0, 0.0};

    double previous = -1.0;
    for (const double scale : {1.0e-3, 1.0e-7, 1.0e-12}) {
        RenderTransform transform{scale};
        transform.set_camera_origin(camera);
        const double resolution = transform.resolution_at(ship);
        if (previous >= 0.0) {
            CHECK_NEAR_REL(resolution, previous, 1.0e-15,
                           "the resolution in metres is distance*FLT_EPSILON regardless of the "
                           "scale; the scale only decides whether the numbers fit the camera's "
                           "near/far range");
        }
        previous = resolution;
    }

    std::ostringstream os;
    os << "resolution at 1e6 m from the camera, at every scale: " << previous << " m";
    INFO(os.str());
    CHECK_NEAR_REL(previous, 1.0e6 * kFloatEpsilon, 1.0e-12, "1e6 m * 1.19e-7");
}

TEST(the_projection_round_trips_within_its_own_resolution) {
    RenderTransform transform{1.0e-6};
    const Vec3 camera{-2.6e10, 1.3e11, 5.7e10};
    transform.set_camera_origin(camera);

    for (const double distance : {1.0e2, 1.0e5, 1.0e8, 4.5e12}) {
        const Vec3 point = camera + Vec3{distance * 0.6, distance * 0.8, 0.0};
        const auto rendered = transform.to_render(point);
        const Vec3 recovered = transform.to_absolute(rendered);

        const double error = (recovered - point).norm();
        const double resolution = transform.resolution_at(point);

        std::ostringstream os;
        os << "at " << distance << " m from the camera: round trip error " << error
           << " m, resolution " << resolution << " m";
        INFO(os.str());

        CHECK(error <= 2.0 * resolution + 1.0e-9);
    }
}

TEST(vectors_are_scaled_but_never_translated) {
    // Subtracting the camera origin from a velocity is a category error, and a
    // common one: it produces a velocity that depends on where the camera is.
    RenderTransform transform{1.0e-7};
    const Vec3 velocity{0.0, 7668.6, 0.0};

    transform.set_camera_origin(Vec3{});
    const auto a = transform.vector_to_render(velocity);

    transform.set_camera_origin(Vec3{kAu, -kAu, 3.0 * kAu});
    const auto b = transform.vector_to_render(velocity);

    CHECK(identical(a, b));
    CHECK_NEAR_REL(static_cast<double>(a.y), 7668.6 * 1.0e-7, 1.0e-6,
                   "a vector is only scaled; the float conversion of 7.6686e-4 keeps seven "
                   "significant digits, so 1e-6 relative is a loose bound");
}

TEST(body_radii_scale_with_the_scene_and_exaggeration_is_explicit) {
    RenderTransform transform{1.0e-7};

    const double earth_radius = 6371008.0;
    CHECK_NEAR_REL(static_cast<double>(transform.radius_to_render(earth_radius)),
                   earth_radius * 1.0e-7, 1.0e-6,
                   "the same scale as positions, so relative sizes stay honest");

    // Exaggeration is a named, deliberate presentation choice.  A simulator that
    // silently inflates planets so they are visible is lying about the geometry;
    // one that exposes the factor is making a choice the user can see.
    transform.set_body_scale_exaggeration(1000.0);
    CHECK_NEAR_REL(static_cast<double>(transform.radius_to_render(earth_radius)),
                   earth_radius * 1.0e-7 * 1000.0, 1.0e-6, "radius * scale * exaggeration");

    // Positions are NOT exaggerated: only the drawn size is.
    const auto position = transform.to_render(Vec3{1.0e9, 0.0, 0.0});
    CHECK_NEAR_REL(static_cast<double>(position.x), 1.0e9 * 1.0e-7, 1.0e-6,
                   "exaggerating positions as well would move the bodies out of their orbits");
}

TEST(the_transform_asks_to_be_recentred_before_precision_is_lost) {
    RenderTransform transform{1.0e-7};
    transform.set_camera_origin(Vec3{});

    CHECK(!transform.should_recenter(Vec3{1.0e4, 0.0, 0.0}, 1.0e6));
    CHECK(transform.should_recenter(Vec3{2.0e6, 0.0, 0.0}, 1.0e6));
    CHECK_THROWS_AS(transform.should_recenter(Vec3{}, 0.0), std::invalid_argument);
}

TEST(the_transform_rejects_nonsense_configuration) {
    CHECK_THROWS_AS(RenderTransform{0.0}, std::invalid_argument);
    CHECK_THROWS_AS(RenderTransform{-1.0e-7}, std::invalid_argument);

    RenderTransform transform{1.0e-7};
    CHECK_THROWS_AS(transform.set_scale(0.0), std::invalid_argument);
    CHECK_THROWS_AS(transform.set_body_scale_exaggeration(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(
        transform.set_camera_origin(Vec3{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}),
        std::invalid_argument);
}
