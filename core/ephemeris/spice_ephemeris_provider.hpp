#pragma once

// EphemerisProvider backed by the NASA/JPL CSPICE toolkit and DE440.
//
// This is the only class in the project that knows SPICE exists.  Everything it
// returns is SI: the km/km-per-second convention of the toolkit stops here.

#include "core/celestial/body_orientation.hpp"
#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/ephemeris/spice_kernel_set.hpp"
#include "core/math/mat3.hpp"

#include <map>
#include <string>
#include <memory>
#include <mutex>

namespace sf::ephemeris {

// Implements BodyOrientationProvider as well: the pole direction comes from the
// same kernels as the trajectories, via cidfrm_c + pxform_c.  Nothing in this
// project writes its own precession model.
class SpiceEphemerisProvider final : public EphemerisProvider,
                                     public celestial::BodyOrientationProvider {
public:
    // Loads every kernel under `kernel_dir` (default: SPACEFLIGHT_KERNEL_DIR or
    // the build-time path) and owns them for the provider's lifetime.
    static std::unique_ptr<SpiceEphemerisProvider> from_directory(const std::filesystem::path& kernel_dir);
    static std::unique_ptr<SpiceEphemerisProvider> from_default_directory();

    explicit SpiceEphemerisProvider(std::shared_ptr<const SpiceKernelSet> kernels);
    ~SpiceEphemerisProvider() override = default;

    [[nodiscard]] BodyState state(celestial::BodyId body,
                                  time::CoordinateTime time,
                                  coordinates::ReferenceFrame frame) const override;

    [[nodiscard]] math::Vec3 position(celestial::BodyId body,
                                      time::CoordinateTime time,
                                      coordinates::ReferenceFrame frame) const override;

    [[nodiscard]] double gravitational_parameter(celestial::BodyId body) const override;
    [[nodiscard]] double mean_radius(celestial::BodyId body) const override;
    [[nodiscard]] CoverageWindow coverage(celestial::BodyId body) const override;
    [[nodiscard]] bool has_body(celestial::BodyId body) const override;

    [[nodiscard]] math::Vec3 pole_direction(celestial::BodyId body,
                                            time::CoordinateTime t,
                                            coordinates::FrameAxes axes) const override;

    // The SAME state, but with SPICE's own CONVERGED light-time correction
    // ("CN") applied:
    // the target as it was when the light left, seen from the frame's origin body
    // at `t`.
    //
    // Not part of EphemerisProvider on purpose. It only answers the question for
    // an observer that IS a body, which a spacecraft is not -- for that,
    // core/relativity/light_time.hpp solves the same equation generically. This
    // exists so the two can be compared, which is the only way to know ours is
    // right (docs/physics/relativistic-rendering.md section 2).
    //
    // "CN" and not "LT": the toolkit's "LT" is deliberately a THREE-ITERATION
    // estimate, and comparing against it measures SPICE's truncation rather than
    // our error. Measured difference between the two for Mars: 252 m.
    [[nodiscard]] BodyState light_time_corrected_state(celestial::BodyId body,
                                                       time::CoordinateTime t,
                                                       coordinates::ReferenceFrame frame) const;

    // The FULL body-fixed rotation: the matrix taking a vector from the body's
    // own frame (IAU_EARTH, IAU_MOON, ...) into `axes`.
    //
    // Deliberately NOT on BodyOrientationProvider. That interface exposes the
    // pole and nothing else, and the reason is written in
    // core/celestial/body_orientation.hpp: an axially symmetric gravity field
    // does not depend on how far the body has turned, and handing a full
    // rotation to the force evaluation invites somebody to integrate in a
    // rotating frame -- which
    // docs/architecture/coordinate-system.md section 2 forbids.
    //
    // This exists for RENDERING, which has the opposite need: a textured Earth
    // that does not turn under a 400 km orbit is visibly wrong within a minute,
    // and the angle it has turned through is exactly what the dynamics is
    // allowed not to care about. Nothing in core/gravity, core/propagation or
    // core/navigation calls it, and that is the property to preserve.
    [[nodiscard]] math::Mat3 body_fixed_rotation(celestial::BodyId body,
                                                 time::CoordinateTime t,
                                                 coordinates::FrameAxes axes) const;

    [[nodiscard]] const SpiceKernelSet& kernels() const { return *kernels_; }

private:
    std::shared_ptr<const SpiceKernelSet> kernels_;

    // Kernel pool lookups are string-keyed dictionary searches inside the
    // toolkit; the gravity model asks for GM on every force evaluation, so the
    // answers are memoised.  They cannot change while the kernel set is alive.
    mutable std::mutex cache_mutex_;
    mutable std::map<int, double> gm_cache_;
    mutable std::map<int, double> radius_cache_;
    mutable std::map<int, CoverageWindow> coverage_cache_;
    mutable std::map<int, std::string> body_frame_cache_;
};

}  // namespace sf::ephemeris
