#pragma once

// Oblateness (J2) perturbation of a single body.
//
//            3 J2 GM R^2  [ (            s^2 )              ]
//   a  =  -  -----------  [ ( 1  -  5 * ---- )  r  +  2 s n ]
//               2 r^5     [ (            r^2 )              ]
//
// with n the body's pole direction and s = r . n.  The form is frame-free: an
// axially symmetric field does not care how far the body has rotated about its
// axis, only where that axis points.  Derivation and validity:
// docs/physics/geopotential.md.

#include "core/celestial/body_orientation.hpp"
#include "core/celestial/oblateness.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/gravity/force_model.hpp"

#include <memory>
#include <string>

namespace sf::gravity {

class OblatenessGravity final : public ForceModel {
public:
    // `provider` and `orientation` must outlive this object.  GM is resolved from
    // the provider, so the mass used here is the same one PointMassGravity uses.
    // Throws if the frame is non-inertial or the geopotential model is invalid.
    OblatenessGravity(const ephemeris::EphemerisProvider& provider,
                      const celestial::BodyOrientationProvider& orientation,
                      celestial::BodyId body,
                      celestial::GeopotentialModel geopotential,
                      coordinates::ReferenceFrame frame = coordinates::ReferenceFrame::ssb_j2000());

    // Convenience: looks the body up in the built-in table.  Throws if it has no
    // documented J2 -- a silently missing perturbation is worse than a refusal.
    // Returns by pointer because a ForceModel is non-copyable by design (it is
    // referenced by the integrator for the whole propagation).
    static std::unique_ptr<OblatenessGravity> for_body(
        const ephemeris::EphemerisProvider& provider,
        const celestial::BodyOrientationProvider& orientation,
        celestial::BodyId body,
        coordinates::ReferenceFrame frame = coordinates::ReferenceFrame::ssb_j2000());

    [[nodiscard]] ForceResult evaluate(const propagation::PropagationState& spacecraft,
                                       time::CoordinateTime t) const override;

    [[nodiscard]] std::string_view name() const override { return name_; }

    [[nodiscard]] celestial::BodyId body() const noexcept { return body_; }
    [[nodiscard]] const celestial::GeopotentialModel& geopotential() const noexcept {
        return geopotential_;
    }

private:
    const ephemeris::EphemerisProvider& provider_;
    const celestial::BodyOrientationProvider& orientation_;
    celestial::BodyId body_;
    celestial::GeopotentialModel geopotential_;
    coordinates::ReferenceFrame frame_;
    double gm_{0.0};
    std::string name_;
};

}  // namespace sf::gravity
