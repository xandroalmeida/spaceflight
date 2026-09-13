#pragma once

// The retarded time of a SINGLE VERTEX, in closed form.
//
// This is the piece of docs/physics/relativistic-rendering.md section 6 that
// cannot live anywhere but a vertex shader, because it is per-vertex by
// definition.  It is also the one formula in this project that exists twice --
// here and in godot/project/shaders/relativistic_body.gdshader -- and section 5
// of docs/architecture/relativistic-shaders.md says what is done about that.
//
// The equation is the same |x_obs - x(t_r)| = c (t - t_r) as
// core/relativity/light_time.hpp, with one difference that changes the method
// completely: within a rigid body the light crossing time is milliseconds
// (21 ms across the Earth, 67 ns across the ship), and over milliseconds the
// motion is a straight line to far beyond the precision of the screen.  A
// straight line makes the implicit equation a QUADRATIC, which is solved once
// instead of iterated.
//
// Nobody rotates anything here.  Terrell rotation is what comes out.

#include "core/math/vec3.hpp"
#include "core/units/constants.hpp"

#include <cmath>

namespace sf::render {

// Extra light time of a point at `offset` from the observer, for a body whose
// velocity relative to the observer is `relative_velocity`.
//
//              sqrt( (p.v)^2 + (c^2 - v^2)|p|^2 )  -  (p.v)
//     dtau  =  --------------------------------------------
//                             c^2 - v^2
//
// `light_speed` is a parameter and not units::c because the shader works in
// SCENE units, where c is c * render_scale.  The scale cancels out of dtau, so
// the two calls agree -- and passing it explicitly is what keeps them agreeing
// (docs/architecture/relativistic-shaders.md section 4).
//
// No clamp and no branch: the discriminant is structurally positive, because
// (c^2 - v^2) > 0 is guaranteed by the state and |p|^2 >= 0 always.  At v = 0 it
// returns |p|/c, as it must.
[[nodiscard]] inline double retarded_light_time(const math::Vec3& offset,
                                                const math::Vec3& relative_velocity,
                                                double light_speed = units::c) {
    const double pv = dot(offset, relative_velocity);
    const double c2_minus_v2 =
        light_speed * light_speed - relative_velocity.norm_squared();
    const double discriminant = pv * pv + c2_minus_v2 * offset.norm_squared();
    return (std::sqrt(discriminant) - pv) / c2_minus_v2;
}

// Where that point APPEARS: where it was when the light left it.
//
// Apply this to every vertex of a mesh, with `offset` measured from the
// observer, and the mesh deforms by itself.  For a small distant object the
// deformation is a rotation of arcsin(beta) -- exactly, to 1e-13, verified in
// tests/scientific/test_terrell_rotation.cpp -- and the face that comes into
// view is the TRAILING one, because its light left earliest and was dragged
// furthest along the motion.
//
// That is the whole implementation of Terrell rotation.  There is no other part.
[[nodiscard]] inline math::Vec3 apparent_vertex(const math::Vec3& offset,
                                                const math::Vec3& relative_velocity,
                                                double light_speed = units::c) {
    return offset - relative_velocity * retarded_light_time(offset, relative_velocity, light_speed);
}

// A mesh is the object's shape in ITS OWN rest frame.  Before any light-time
// question can be asked, those vertices have to become positions in the
// observer's coordinate frame at one instant -- and that is where the Lorentz
// contraction enters: shortened by 1/gamma along the motion, untouched across
// it.
//
// This is NOT the thing rule 38 forbids, and the difference is the whole point
// of docs/physics/relativistic-rendering.md section 11.5.  Forbidden is
// contraction as the ANSWER -- scaling a mesh and drawing the squashed result,
// which is a picture of a coordinate.  Required is contraction as a STEP, after
// which the per-vertex light time turns it into an image.
//
// Measured, with a sphere at beta = 0.9048 (Penrose 1959: a sphere must stay a
// sphere):
//
//   contraction omitted  -> silhouette 159% out of round.  A blob nobody sees.
//   contraction applied  -> silhouette 9.0e-5 out of round at an angular radius
//                           of 1e-4 rad, and the departure is LINEAR in the
//                           angular size, so it is the small-object limit of the
//                           theorem and not an error.
[[nodiscard]] inline math::Vec3 lorentz_contract(const math::Vec3& rest_offset,
                                                 const math::Vec3& relative_velocity,
                                                 double light_speed = units::c) {
    const double speed = relative_velocity.norm();
    if (speed <= 0.0) {
        return rest_offset;
    }
    const math::Vec3 direction = relative_velocity / speed;
    const double beta = speed / light_speed;
    // gamma from beta, and beta < 1 structurally: the propagator builds the state
    // that way (rule 13), and the scene units preserve the ratio exactly
    // (docs/architecture/relativistic-shaders.md section 4).
    const double inverse_gamma = std::sqrt(1.0 - beta * beta);

    const double along = dot(rest_offset, direction);
    const math::Vec3 across = rest_offset - direction * along;
    return across + direction * (along * inverse_gamma);
}

// The complete per-vertex transformation, and the one the vertex shader mirrors.
//
// Returns the vertex's apparent position RELATIVE TO THE BODY CENTRE's own
// apparent position -- a differential, deliberately, because the centre was
// already placed by core/relativity/light_time.hpp against the real ephemeris
// (minutes of light time, curved path) and applying the straight-line solution to
// it again would count the same retardation twice.
//
//   centre_from_observer : where the body centre is, relative to the observer
//   rest_offset          : the mesh vertex, in the body's rest frame
//
// Section 11.4 of the physics document is this function's signature written out.
[[nodiscard]] inline math::Vec3 apparent_vertex_offset(const math::Vec3& rest_offset,
                                                       const math::Vec3& centre_from_observer,
                                                       const math::Vec3& relative_velocity,
                                                       double light_speed = units::c) {
    const math::Vec3 contracted = lorentz_contract(rest_offset, relative_velocity, light_speed);
    const math::Vec3 vertex = apparent_vertex(centre_from_observer + contracted,
                                              relative_velocity, light_speed);
    const math::Vec3 centre =
        apparent_vertex(centre_from_observer, relative_velocity, light_speed);
    return vertex - centre;
}

// The rotation a small object at distance appears to have undergone, for a
// reader who wants the number rather than the mesh: arcsin(beta).
//
// NOT used to render anything -- rendering goes through apparent_vertex_offset()
// and gets this for free.  It exists so that the test has something to compare
// against that was written from the theorem (Terrell 1959, Penrose 1959) rather
// than from the code being tested.
[[nodiscard]] inline double terrell_rotation_angle(double beta) {
    return std::asin(beta);
}

}  // namespace sf::render
