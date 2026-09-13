#pragma once

// EphemerisProvider backed by the NASA/JPL CSPICE toolkit and DE440.
//
// This is the only class in the project that knows SPICE exists.  Everything it
// returns is SI: the km/km-per-second convention of the toolkit stops here.

#include "core/ephemeris/ephemeris_provider.hpp"
#include "core/ephemeris/spice_kernel_set.hpp"

#include <map>
#include <memory>
#include <mutex>

namespace sf::ephemeris {

class SpiceEphemerisProvider final : public EphemerisProvider {
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
};

}  // namespace sf::ephemeris
