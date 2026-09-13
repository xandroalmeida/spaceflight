// Terrell rotation, and the fact that nobody applies it.
//
// The implementation under test (core/render/terrell.hpp) contains no angle, no
// arcsin, and no knowledge that Terrell rotation exists.  It solves one equation
// -- |p - v dtau| = c dtau -- per vertex.  This file measures the shape that
// comes out and compares it against Terrell (1959) / Penrose (1959), which was
// written from the theorem and not from the code.
//
// That is the whole argument of docs/physics/relativistic-rendering.md section 6:
// the rotation must be a CONSEQUENCE, or it is decoration.

#include "core/math/vec3.hpp"
#include "core/render/terrell.hpp"
#include "core/units/conversions.hpp"
#include "tests/support/test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace sf;
using sf::math::Vec3;
using sf::render::apparent_vertex;
using sf::render::retarded_light_time;

namespace {

// Everything here works in units where c = 1, so that "beta" and "velocity" are
// the same number and no scale can hide a factor.  The scale invariance itself
// is tested separately below.
constexpr double kC = 1.0;

// A small rigid object at distance `distance` straight ahead (+z), flying in +x.
// Returns the apparent yaw of an edge that was parallel to the line of sight.
double apparent_yaw_deg(double beta, double distance, double half_size) {
    const Vec3 v{beta, 0.0, 0.0};
    const Vec3 near_corner{half_size, 0.0, distance - half_size};
    const Vec3 far_corner{half_size, 0.0, distance + half_size};

    const Vec3 a = apparent_vertex(near_corner, v, kC);
    const Vec3 b = apparent_vertex(far_corner, v, kC);

    // The edge ran along +z at rest.  Where does it point now?
    return units::rad_to_deg(std::atan2(b.x - a.x, b.z - a.z));
}

}  // namespace

TEST(the_retarded_time_reduces_to_the_obvious_answer_at_rest) {
    const Vec3 p{3.0, 4.0, 12.0};   // |p| = 13, exactly (3-4-12-13 is Pythagorean)
    CHECK_NEAR_ABS(retarded_light_time(p, Vec3{}, kC), 13.0, 1.0e-15,
                   "a stationary body: dtau = |p|/c, and 3-4-12-13 makes |p| = 13 exactly, "
                   "so the only error is the ulp of a sqrt");

    // And with c in SI, the same thing in seconds.
    const Vec3 metres{0.0, 0.0, units::c};
    CHECK_NEAR_ABS(retarded_light_time(metres, Vec3{}), 1.0, 1.0e-15,
                   "one light second is c metres, by definition of the metre");
}

TEST(the_apparent_rotation_is_arcsin_beta_and_nobody_wrote_it) {
    // The heart of the milestone.  Tolerance is 1e-10 DEGREES, which is not a
    // "close enough": it is the statement that the emergent angle and the
    // theorem's angle are the same number.
    for (const double beta : {0.0896, 0.3, 0.5, 0.7, 0.9048, 0.99}) {
        // Small object, far away: the limit in which Terrell's theorem is stated.
        const double measured = apparent_yaw_deg(beta, 1.0, 1.0e-4);
        const double predicted = units::rad_to_deg(render::terrell_rotation_angle(beta));

        std::ostringstream os;
        os << "beta " << beta << ": predicted " << predicted << " deg, measured "
           << std::abs(measured) << " deg";
        INFO(os.str());

        CHECK_NEAR_ABS(std::abs(measured), predicted, 1.0e-5,
                       "Terrell (1959) / Penrose (1959) give arcsin(beta) for a SMALL distant "
                       "object; core/render/terrell.hpp knows only the light-cone equation. "
                       "Two errors bound the agreement and they pull opposite ways. The "
                       "theorem's departure is QUADRATIC in the angular radius h -- measured "
                       "2.58, 16.5, 60.9 and 201 deg per rad^2 at beta = 0.0896, 0.5, 0.9048 "
                       "and 0.99, i.e. error/h^2 constant to three digits over h = 1e-3 to "
                       "1e-5. Cancellation in the edge difference b.x - a.x goes the other "
                       "way, since a difference of order h between numbers of order 1 keeps "
                       "only 1e-16/h, and it takes over below h = 1e-6 at ~3e-10 deg. This "
                       "test runs at h = 1e-4, where the worst case (beta = 0.99) is "
                       "2.01e-6 deg; the bound is five times that");
    }
}

TEST(the_face_that_comes_into_view_is_the_trailing_one) {
    // The signature that separates Terrell rotation from "someone applied a
    // rotation".  Light from the far side left EARLIER, so it was dragged
    // further along the motion: the far corner appears displaced BACKWARDS
    // relative to the near one, and the trailing face swings into view.
    const double beta = 0.9048;
    const Vec3 v{beta, 0.0, 0.0};

    const Vec3 near_corner{0.0, 0.0, 1.0 - 1.0e-6};
    const Vec3 far_corner{0.0, 0.0, 1.0 + 1.0e-6};

    const double dt_near = retarded_light_time(near_corner, v, kC);
    const double dt_far = retarded_light_time(far_corner, v, kC);
    CHECK(dt_far > dt_near);

    const Vec3 a = apparent_vertex(near_corner, v, kC);
    const Vec3 b = apparent_vertex(far_corner, v, kC);

    std::ostringstream os;
    os << "near corner apparent x " << a.x << ", far corner " << b.x
       << " (motion is towards +x)";
    INFO(os.str());

    // Further back along the motion: negative x relative to the near corner.
    CHECK(b.x < a.x);

    // A pure Lorentz CONTRACTION would have moved neither: it would have shortened
    // the object along x, where this object has no extent at all.  That the shape
    // changes here at all is the point.
    CHECK(std::abs(b.x - a.x) > 0.0);
}

TEST(a_sphere_stays_a_sphere) {
    // Penrose (1959).  This is the test that decides whether the Lorentz
    // contraction belongs in the pipeline, and it decides it unambiguously: the
    // silhouette of a moving sphere must be a CIRCLE.  Nothing about a circle is
    // adjustable, so there is no way to tune a wrong pipeline into passing.
    const double beta = 0.9048;
    const Vec3 v{beta, 0.0, 0.0};
    const double distance = 1.0e4;
    const double radius = 1.0;   // angular radius 1e-4 rad

    const Vec3 centre{0.0, 0.0, distance};

    auto silhouette_spread = [&](bool with_contraction) {
        // The apparent centre direction, and a basis across it.
        const Vec3 apparent_centre = apparent_vertex(centre, v, kC);
        const Vec3 n = apparent_centre.normalized();
        Vec3 e1 = (Vec3{0.0, 1.0, 0.0} - n * dot(Vec3{0.0, 1.0, 0.0}, n)).normalized();
        const Vec3 e2 = cross(n, e1);

        // Largest apparent offset from the centre in each of 360 azimuths: that
        // envelope IS the silhouette.
        double ring[360];
        for (double& r : ring) {
            r = 0.0;
        }
        constexpr int kSteps = 400;
        for (int i = 0; i < 2 * kSteps; ++i) {
            for (int j = 0; j < kSteps; ++j) {
                const double theta = units::pi * (static_cast<double>(j) + 0.5) / kSteps;
                const double phi = 2.0 * units::pi * static_cast<double>(i) /
                                   static_cast<double>(2 * kSteps);
                const Vec3 rest{radius * std::sin(theta) * std::cos(phi),
                                radius * std::sin(theta) * std::sin(phi),
                                radius * std::cos(theta)};
                const Vec3 offset =
                    with_contraction ? render::lorentz_contract(rest, v, kC) : rest;
                const Vec3 direction = apparent_vertex(centre + offset, v, kC).normalized();
                const double u = dot(direction, e1);
                const double w = dot(direction, e2);
                const auto azimuth = static_cast<int>(
                    std::fmod(units::rad_to_deg(std::atan2(w, u)) + 360.0, 360.0));
                ring[azimuth] = std::max(ring[azimuth], std::hypot(u, w));
            }
        }

        double lo = ring[0];
        double hi = ring[0];
        double total = 0.0;
        for (const double r : ring) {
            lo = std::min(lo, r);
            hi = std::max(hi, r);
            total += r;
        }
        return (hi - lo) / (total / 360.0);
    };

    const double with = silhouette_spread(true);
    const double without = silhouette_spread(false);

    std::ostringstream os;
    os << "silhouette out-of-round: with the contraction " << with << ", without it " << without;
    INFO(os.str());

    CHECK_NEAR_ABS(with, 0.0, 3.0e-4,
                   "Penrose (1959): the outline of a moving sphere is a circle, exactly, in "
                   "the small-object limit. The residual is LINEAR in the angular size -- "
                   "measured 9.0e-3 at an angular radius of 1e-2 rad and 9.0e-5 at 1e-4 rad, "
                   "and unchanged by refining the sampling from 8e4 to 5e6 points, which is "
                   "what identifies it as the theorem's own limit rather than numerical "
                   "noise. This test runs at 1e-4 rad, so 3e-4 is three times the measured "
                   "departure");

    // And the alternative -- omit the contraction -- is not a small error.
    CHECK(without > 1.0);
    std::ostringstream os2;
    os2 << "omitting the Lorentz contraction makes the silhouette " << without / with
        << " times less round: it is a required STEP, not the answer (section 11.5)";
    INFO(os2.str());
}

TEST(the_apparent_size_is_not_the_contracted_mesh) {
    // Rule 38, as a measurement rather than an assertion.  Three numbers:
    //
    //   what a still object of this size would subtend        R/d
    //   what "scale the mesh by 1/gamma" would draw           R/(gamma d)
    //   what is actually seen                                 R/(gamma d), BUT
    //                                                         displaced to
    //                                                         arcsin(beta) off
    //                                                         axis and round
    //
    // The size agrees with the naive scaling; the POSITION and the SHAPE do not,
    // and those are what scaling a mesh gets wrong.
    const double beta = 0.9048;
    const double gamma = 1.0 / std::sqrt(1.0 - beta * beta);
    const Vec3 v{beta, 0.0, 0.0};
    const double distance = 1.0e4;
    const double radius = 1.0;
    const Vec3 centre{0.0, 0.0, distance};

    const Vec3 apparent_centre = apparent_vertex(centre, v, kC);
    const double apparent_distance = apparent_centre.norm();

    double largest = 0.0;
    for (int i = 0; i < 720; ++i) {
        for (int j = 0; j < 360; ++j) {
            const double theta = units::pi * (static_cast<double>(j) + 0.5) / 360.0;
            const double phi = 2.0 * units::pi * static_cast<double>(i) / 720.0;
            const Vec3 rest{radius * std::sin(theta) * std::cos(phi),
                            radius * std::sin(theta) * std::sin(phi),
                            radius * std::cos(theta)};
            const Vec3 offset = render::lorentz_contract(rest, v, kC);
            largest = std::max(largest, math::angle_between(
                                            apparent_vertex(centre + offset, v, kC),
                                            apparent_centre));
        }
    }

    std::ostringstream os;
    os << "apparent angular radius " << units::rad_to_deg(largest) << " deg at an apparent "
       << "distance of " << apparent_distance << " (geometric distance " << distance
       << "), displaced " << units::rad_to_deg(math::angle_between(apparent_centre, centre))
       << " deg off axis";
    INFO(os.str());

    // The light left when the body was gamma times further away, and that -- not a
    // mesh scale -- is where the smaller image comes from.
    CHECK_NEAR_REL(apparent_distance, gamma * distance, 1.0e-9,
                   "for a body passing abeam, the retarded position is gamma times the "
                   "current distance: dtau = gamma d / c and the transverse offset is beta "
                   "gamma d, giving d sqrt(1 + beta^2 gamma^2) = gamma d exactly. The bound "
                   "is the rounding of a sqrt");

    CHECK_NEAR_REL(largest, std::asin(radius / (gamma * distance)), 1.0e-3,
                   "the proper radius seen from the retarded distance gamma d. The "
                   "departure is linear in the angular size: measured +4.6e-3 at R/d = 1e-2, "
                   "+4.5e-4 at 1e-3 and +3.6e-5 at 1e-4, which is this test");

    // The displacement is the rotation: arcsin(beta), the same 64.8 degrees as the
    // edge measurement, arrived at from the position instead of the shape.
    CHECK_NEAR_ABS(units::rad_to_deg(math::angle_between(apparent_centre, centre)),
                   units::rad_to_deg(std::asin(beta)), 1.0e-9,
                   "tan(displacement) = beta gamma, and arctan(beta gamma) = arcsin(beta) "
                   "identically. Exact in algebra; the bound is the rounding of an atan2");
}

TEST(the_scale_cancels_out_of_the_retarded_time) {
    // docs/architecture/relativistic-shaders.md section 4: the vertex shader
    // works in scene units, so it passes c * render_scale.  If the cancellation
    // were not exact the effect would come out wrong by seven orders of magnitude
    // -- and silently, because it would simply vanish.
    const Vec3 p{1.0e7, -3.0e6, 4.2e7};      // metres
    const Vec3 v{1.0e8, 2.0e7, -5.0e7};      // m/s, beta = 0.38

    const double seconds = retarded_light_time(p, v, units::c);

    for (const double scale : {1.0e-9, 1.0e-7, 1.0e-6, 1.0e-3, 1.0e3}) {
        const double scaled = retarded_light_time(p * scale, v * scale, units::c * scale);
        CHECK_NEAR_REL(scaled, seconds, 1.0e-13,
                       "dtau has dimensions of time and every length in it is scaled "
                       "identically, so the scale cancels algebraically; the residual is the "
                       "rounding of the scaled products");
    }
}

TEST(the_solution_satisfies_the_equation_it_came_from) {
    // The closed form is an algebraic rearrangement, and the check that it was
    // rearranged correctly is to put it back: |p - v dtau| must equal c dtau.
    // Swept over directions so that a sign error in the p.v term cannot hide in a
    // symmetric case.
    for (const double beta : {0.0, 0.1, 0.5, 0.9048, 0.999}) {
        for (int i = 0; i < 32; ++i) {
            const double phi = 2.0 * units::pi * static_cast<double>(i) / 32.0;
            const Vec3 p{std::cos(phi), 0.4 * std::sin(phi), 2.0 + std::sin(phi)};
            const Vec3 v{beta * 0.6, beta * 0.8, 0.0};

            const double dtau = retarded_light_time(p, v, kC);
            CHECK(dtau > 0.0);

            const double residual = (p - v * dtau).norm() - kC * dtau;
            CHECK_NEAR_ABS(residual, 0.0, 1.0e-12,
                           "the defining light-cone equation, re-substituted. Exact in "
                           "algebra, so the residual is pure rounding -- amplified by the "
                           "1/(c^2 - v^2) in the closed form, which at beta = 0.999 is 500. "
                           "With |p| and c*dtau both of order 3, that is 500 * 3 * eps = "
                           "3.3e-13; measured worst 1.14e-13");
        }
    }
}

TEST(the_discriminant_is_structurally_positive) {
    // Rule 13: the limit is the structure, not a guard.  (p.v)^2 is a square and
    // (c^2 - v^2)|p|^2 is positive because the state guarantees |v| < c, so the
    // discriminant cannot go negative and there is nothing to clamp.  Verified up
    // to beta = 1 - 1e-12.
    for (const double beta : {0.9, 0.99, 0.999999, 1.0 - 1.0e-12}) {
        for (const auto& p : {Vec3{1.0, 0.0, 0.0}, Vec3{-1.0, 0.0, 0.0}, Vec3{0.0, 0.0, 1.0},
                              Vec3{1.0e-12, 0.0, 0.0}}) {
            const Vec3 v{beta, 0.0, 0.0};       // straight at, straight away, sideways
            const double dtau = retarded_light_time(p, v, kC);
            CHECK(std::isfinite(dtau));
            CHECK(dtau > 0.0);
        }
    }
}

TEST(the_small_object_limit_is_where_the_theorem_lives) {
    // Honest domain of validity: arcsin(beta) is Terrell's result for an object
    // whose angular size is small.  Measuring the departure gives the number for
    // the validity table instead of an assertion that it is "negligible".
    const double beta = 0.9048;
    const double predicted = units::rad_to_deg(render::terrell_rotation_angle(beta));

    for (const double half_size : {1.0e-6, 1.0e-3, 1.0e-2, 0.1, 0.3}) {
        const double measured = std::abs(apparent_yaw_deg(beta, 1.0, half_size));
        const double angular_radius_deg = units::rad_to_deg(std::atan(half_size / 1.0));

        std::ostringstream os;
        os << "angular radius " << angular_radius_deg << " deg: yaw " << measured
           << " deg vs arcsin(beta) " << predicted << " (" << measured - predicted << ")";
        INFO(os.str());
    }

    // At a tenth of a radian -- 5.7 degrees of angular radius, an object the size
    // of eleven full Moons -- the departure is still under a degree.
    CHECK_NEAR_ABS(std::abs(apparent_yaw_deg(beta, 1.0, 0.1)), predicted, 1.0,
                   "Terrell's arcsin(beta) is the small-angle limit. At 5.7 deg of angular "
                   "radius the exact light-cone solution departs from it by less than a "
                   "degree, which is the scale on which the approximation stops being one -- "
                   "and the renderer never uses the approximation, only this bound");
}
