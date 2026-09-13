#pragma once

// Shared setup for tests that need the real star catalogue.
//
// Same policy as kernel_fixture.hpp: a test that cannot find its data is
// SKIPPED, never silently passed.  The catalogue is fetched, not committed
// (catalogs/MANIFEST.md), so a fresh clone has to skip rather than fail.

#include "core/render/star_catalog.hpp"
#include "tests/support/test_harness.hpp"

#include <exception>

namespace sft {

inline sf::render::StarCatalog load_catalog_or_skip() {
    try {
        auto catalog = sf::render::StarCatalog::from_bsc5_file(
            sf::render::StarCatalog::default_path());
        if (catalog.empty()) {
            throw TestSkipped{"star catalogue parsed to zero stars: " + catalog.describe()};
        }
        return catalog;
    } catch (const std::runtime_error& e) {
        throw TestSkipped{std::string{"star catalogue unavailable: "} + e.what()};
    }
}

}  // namespace sft
