#pragma once

// The weak-field metric that carries gravity for a relativistic spacecraft.
//
// Gravity here is NOT a force: it is the shape of spacetime, and the ship follows
// a geodesic of it. That is why this class is not a ForceModel -- it has no
// evaluate() returning an acceleration, and the propagator consumes it through a
// different door.
//
// Static, harmonic gauge, quasi-static sources:
//
//     g00 = -A ,  A = 1 - 2U/c^2 + 2U^2/c^4
//     gij =  B d_ij ,  B = 1 + 2U/c^2
//     g0i =  0                      <- the principal approximation, see the doc
//
// with U = sum GM_a / |x - x_a| > 0, evaluated at the bodies' positions at t.
// See docs/physics/relativistic-gravity.md.

#include "core/celestial/body_catalog.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/math/vec3.hpp"
#include "core/time/coordinate_time.hpp"

namespace sf::gravity {

struct MetricSample {
    double potential{0.0};              // U [m^2/s^2], positive
    math::Vec3 potential_gradient{};    // grad U, which IS the Newtonian acceleration
    double a{1.0};                      // A, with g00 = -A
    double b{1.0};                      // B, with gij = B delta_ij
    math::Vec3 grad_a{};
    math::Vec3 grad_b{};

    // c sqrt(A/B): the coordinate speed of light here, which is the speed a
    // massive particle approaches and never reaches. Slightly below c near a
    // mass -- 1.97e-8 below, at 1 au from the Sun.
    double local_light_speed{0.0};

    bool inside_body{false};
    celestial::BodyId inside_of{};

    // u0 from the mass-shell constraint g_uv u^u u^v = -c^2:
    //     u0 = sqrt( (c^2 + B |u|^2) / A )
    // Real and positive for every finite u, which is what makes the local speed
    // limit structural rather than policed.
    [[nodiscard]] double time_component(const math::Vec3& proper_velocity) const;

    // v = c u / u0, and dtau/dt = c / u0.
    [[nodiscard]] math::Vec3 coordinate_velocity(const math::Vec3& proper_velocity) const;
    [[nodiscard]] double proper_time_rate(const math::Vec3& proper_velocity) const;

    // The geodesic term:  du/dtau = -(1/2B)[ (u0)^2 grad_A + 2 u (grad_B . u)
    //                                        - |u|^2 grad_B ]
    [[nodiscard]] math::Vec3 geodesic_acceleration(const math::Vec3& proper_velocity) const;
};

class WeakFieldMetric {
public:
    // `provider` and `catalog` must outlive this object. The frame must be
    // inertial, for the same reason PointMassGravity insists on it.
    WeakFieldMetric(const ephemeris::EphemerisProvider& provider,
                    const celestial::BodyCatalog& catalog,
                    coordinates::ReferenceFrame frame = coordinates::ReferenceFrame::ssb_j2000());

    [[nodiscard]] MetricSample sample(const math::Vec3& position, time::CoordinateTime t) const;

    [[nodiscard]] const coordinates::ReferenceFrame& frame() const noexcept { return frame_; }
    [[nodiscard]] const celestial::BodyCatalog& catalog() const noexcept { return catalog_; }

private:
    const ephemeris::EphemerisProvider& provider_;
    const celestial::BodyCatalog& catalog_;
    coordinates::ReferenceFrame frame_;
};

}  // namespace sf::gravity
