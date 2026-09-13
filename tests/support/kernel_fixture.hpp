#pragma once

// Shared setup for tests that need real ephemeris data.
//
// A test that cannot find kernels is SKIPPED, never silently passed: a green
// suite that verified nothing is worse than a red one.

#include "core/ephemeris/errors.hpp"
#include "core/ephemeris/spice_ephemeris_provider.hpp"
#include "core/ephemeris/spice_kernel_set.hpp"
#include "core/ephemeris/spice_time_converter.hpp"
#include "tests/support/test_harness.hpp"

#include <memory>

namespace sft {

struct SpiceFixture {
    std::shared_ptr<const sf::ephemeris::SpiceKernelSet> kernels;
    std::unique_ptr<sf::ephemeris::SpiceEphemerisProvider> provider;
    std::unique_ptr<sf::ephemeris::SpiceTimeConverter> time;
};

inline SpiceFixture load_spice_or_skip() {
    try {
        auto kernels = std::make_shared<const sf::ephemeris::SpiceKernelSet>(
            sf::ephemeris::SpiceKernelSet::from_directory(
                sf::ephemeris::SpiceKernelSet::default_directory()));

        SpiceFixture fixture{};
        fixture.kernels = kernels;
        fixture.provider = std::make_unique<sf::ephemeris::SpiceEphemerisProvider>(kernels);
        fixture.time = std::make_unique<sf::ephemeris::SpiceTimeConverter>(kernels);
        return fixture;
    } catch (const sf::ephemeris::KernelLoadError& e) {
        throw TestSkipped{std::string{"SPICE kernels unavailable: "} + e.what() +
                          " (run scripts/fetch_kernels.sh)"};
    }
}

}  // namespace sft
