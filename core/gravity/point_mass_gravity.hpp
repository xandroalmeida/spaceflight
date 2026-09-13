#pragma once

// Newtonian N-body point-mass gravity on a test particle.
//
//        a(r,t) = SUM_i  -GM_i (r - r_i(t)) / |r - r_i(t)|^3
//
// Every enabled body contributes at every instant: no sphere of influence, no
// switching of central body, no softening.  See docs/physics/gravity-model.md.

#include "core/celestial/body_catalog.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/gravity/force_model.hpp"

#include <vector>

namespace sf::gravity {

// Per-body breakdown, for diagnostics and for the CLI.  Not used by the
// integrator: it must not pay for reporting.
struct GravityContribution {
    celestial::BodyId body{};
    double distance{0.0};       // [m]
    math::Vec3 acceleration{};  // [m/s^2]
};

class PointMassGravity final : public ForceModel {
public:
    // `provider` and `catalog` must outlive this object.  The integration frame
    // must be inertial; a body-fixed frame would silently omit the fictitious
    // forces and is rejected.
    PointMassGravity(const ephemeris::EphemerisProvider& provider,
                     const celestial::BodyCatalog& catalog,
                     coordinates::ReferenceFrame frame = coordinates::ReferenceFrame::ssb_j2000());

    [[nodiscard]] ForceResult evaluate(const propagation::PropagationState& spacecraft,
                                       time::CoordinateTime t) const override;

    [[nodiscard]] std::string_view name() const override { return "PointMassGravity"; }

    [[nodiscard]] std::vector<GravityContribution> contributions(const math::Vec3& position,
                                                                 time::CoordinateTime t) const;

    [[nodiscard]] const coordinates::ReferenceFrame& frame() const noexcept { return frame_; }
    [[nodiscard]] const celestial::BodyCatalog& catalog() const noexcept { return catalog_; }

private:
    const ephemeris::EphemerisProvider& provider_;
    const celestial::BodyCatalog& catalog_;
    coordinates::ReferenceFrame frame_;
};

}  // namespace sf::gravity
