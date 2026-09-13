#include "core/ephemeris/spice_ephemeris_provider.hpp"

#include "core/ephemeris/errors.hpp"
#include "core/ephemeris/spice_internal.hpp"
#include "core/units/conversions.hpp"

extern "C" {
#include "SpiceUsr.h"
}

#include <algorithm>
#include <array>
#include <limits>

namespace sf::ephemeris {
namespace {

// Geometric states, no aberration correction: dynamics needs where a body is,
// not where it appears.  See EphemerisProvider::state().
constexpr const char* kNoAberrationCorrection = "NONE";

}  // namespace

std::unique_ptr<SpiceEphemerisProvider> SpiceEphemerisProvider::from_directory(
    const std::filesystem::path& kernel_dir) {
    auto kernels = std::make_shared<const SpiceKernelSet>(SpiceKernelSet::from_directory(kernel_dir));
    return std::make_unique<SpiceEphemerisProvider>(std::move(kernels));
}

std::unique_ptr<SpiceEphemerisProvider> SpiceEphemerisProvider::from_default_directory() {
    return from_directory(SpiceKernelSet::default_directory());
}

SpiceEphemerisProvider::SpiceEphemerisProvider(std::shared_ptr<const SpiceKernelSet> kernels)
    : kernels_(std::move(kernels)) {
    if (!kernels_) {
        throw KernelLoadError("SpiceEphemerisProvider requires a loaded kernel set");
    }
    detail::ensure_spice_error_handling();
}

BodyState SpiceEphemerisProvider::state(celestial::BodyId body,
                                        time::CoordinateTime t,
                                        coordinates::ReferenceFrame frame) const {
    BodyState out{};
    out.body = body;
    out.epoch = t;
    out.frame = frame;

    if (body == frame.origin) {
        return out;  // a body is at rest at its own origin, by definition
    }

    const SpiceDouble et = t.seconds_since_j2000();
    const std::string frame_name{coordinates::spice_frame_name(frame.axes)};

    std::array<SpiceDouble, 6> sv{};
    SpiceDouble light_time = 0.0;
    {
        const std::lock_guard lock{detail::spice_mutex()};
        spkez_c(static_cast<SpiceInt>(body.naif_id()), et, frame_name.c_str(),
                kNoAberrationCorrection, static_cast<SpiceInt>(frame.origin.naif_id()),
                sv.data(), &light_time);
        detail::throw_if_spice_failed("spkez(" + body.name() + " @ " + t.to_string() + ")");
    }

    out.state.position = units::km_to_m(math::Vec3{sv[0], sv[1], sv[2]});
    out.state.velocity = units::km_to_m(math::Vec3{sv[3], sv[4], sv[5]});
    return out;
}

math::Vec3 SpiceEphemerisProvider::position(celestial::BodyId body,
                                            time::CoordinateTime t,
                                            coordinates::ReferenceFrame frame) const {
    if (body == frame.origin) {
        return math::Vec3{};
    }

    const SpiceDouble et = t.seconds_since_j2000();
    const std::string frame_name{coordinates::spice_frame_name(frame.axes)};

    std::array<SpiceDouble, 3> p{};
    SpiceDouble light_time = 0.0;
    {
        const std::lock_guard lock{detail::spice_mutex()};
        spkezp_c(static_cast<SpiceInt>(body.naif_id()), et, frame_name.c_str(),
                 kNoAberrationCorrection, static_cast<SpiceInt>(frame.origin.naif_id()),
                 p.data(), &light_time);
        detail::throw_if_spice_failed("spkezp(" + body.name() + ")");
    }
    return units::km_to_m(math::Vec3{p[0], p[1], p[2]});
}

double SpiceEphemerisProvider::gravitational_parameter(celestial::BodyId body) const {
    {
        const std::lock_guard lock{cache_mutex_};
        if (const auto it = gm_cache_.find(body.naif_id()); it != gm_cache_.end()) {
            return it->second;
        }
    }

    SpiceInt count = 0;
    std::array<SpiceDouble, 1> value{};
    {
        const std::lock_guard lock{detail::spice_mutex()};
        if (!bodfnd_c(static_cast<SpiceInt>(body.naif_id()), "GM")) {
            throw EphemerisUnavailable("no GM in the kernel pool for " + body.name() +
                                       " (is gm_de440.tpc loaded?)");
        }
        bodvcd_c(static_cast<SpiceInt>(body.naif_id()), "GM", 1, &count, value.data());
        detail::throw_if_spice_failed("bodvcd(GM, " + body.name() + ")");
    }
    if (count < 1) {
        throw EphemerisUnavailable("empty GM for " + body.name());
    }

    // Kernels store GM in km^3/s^2.
    const double gm = value[0] * 1.0e9;
    {
        const std::lock_guard lock{cache_mutex_};
        gm_cache_[body.naif_id()] = gm;
    }
    return gm;
}

double SpiceEphemerisProvider::mean_radius(celestial::BodyId body) const {
    {
        const std::lock_guard lock{cache_mutex_};
        if (const auto it = radius_cache_.find(body.naif_id()); it != radius_cache_.end()) {
            return it->second;
        }
    }

    double radius = 0.0;  // barycentres and point masses have no radii
    SpiceInt count = 0;
    std::array<SpiceDouble, 3> radii{};
    {
        const std::lock_guard lock{detail::spice_mutex()};
        if (bodfnd_c(static_cast<SpiceInt>(body.naif_id()), "RADII")) {
            bodvcd_c(static_cast<SpiceInt>(body.naif_id()), "RADII", 3, &count, radii.data());
            detail::throw_if_spice_failed("bodvcd(RADII, " + body.name() + ")");
        }
    }
    if (count == 3) {
        radius = units::km_to_m((radii[0] + radii[1] + radii[2]) / 3.0);
    }

    {
        const std::lock_guard lock{cache_mutex_};
        radius_cache_[body.naif_id()] = radius;
    }
    return radius;
}

CoverageWindow SpiceEphemerisProvider::coverage(celestial::BodyId body) const {
    {
        const std::lock_guard lock{cache_mutex_};
        if (const auto it = coverage_cache_.find(body.naif_id()); it != coverage_cache_.end()) {
            return it->second;
        }
    }

    CoverageWindow window{};
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();

    {
        const std::lock_guard lock{detail::spice_mutex()};
        for (const auto& spk : kernels_->spk_files()) {
            SPICEDOUBLE_CELL(cover, 2000);
            scard_c(0, &cover);
            spkcov_c(spk.string().c_str(), static_cast<SpiceInt>(body.naif_id()), &cover);
            if (failed_c()) {
                reset_c();
                continue;
            }
            const SpiceInt intervals = wncard_c(&cover);
            for (SpiceInt i = 0; i < intervals; ++i) {
                SpiceDouble begin = 0.0;
                SpiceDouble end = 0.0;
                wnfetd_c(&cover, i, &begin, &end);
                lo = std::min(lo, static_cast<double>(begin));
                hi = std::max(hi, static_cast<double>(end));
            }
        }
    }

    if (lo <= hi) {
        window.begin = time::CoordinateTime::from_seconds_since_j2000(lo);
        window.end = time::CoordinateTime::from_seconds_since_j2000(hi);
        window.valid = true;
    }

    {
        const std::lock_guard lock{cache_mutex_};
        coverage_cache_[body.naif_id()] = window;
    }
    return window;
}

bool SpiceEphemerisProvider::has_body(celestial::BodyId body) const {
    if (body == celestial::bodies::solar_system_barycenter) {
        return true;
    }
    return coverage(body).valid;
}

}  // namespace sf::ephemeris
