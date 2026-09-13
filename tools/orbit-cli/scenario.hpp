#pragma once

// Scenario file for `orbit-cli propagate`.
//
// Everything the CLI needs to reproduce a run lives in the file: epoch, body
// set, initial state, tolerances.  Reproducibility is the point -- a scenario
// plus a git revision must give the same numbers twice.

#include "core/celestial/body_id.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/math/vec3.hpp"
#include "core/propagation/spacecraft_propagator.hpp"

#include <string>
#include <vector>

namespace orbitcli {

struct Scenario {
    std::string name{"unnamed"};
    std::string epoch_text{"2026-01-01T00:00:00"};
    double duration_seconds{3600.0};

    // Bodies whose gravity acts.  Defaults to the standard catalogue.
    std::vector<sf::celestial::BodyId> bodies;

    // The spacecraft's initial state is given relative to this body, in J2000
    // axes -- that is how humans specify orbits.  It is converted to the
    // barycentric integration frame immediately after loading.
    sf::celestial::BodyId relative_to{sf::celestial::bodies::earth};
    sf::math::Vec3 position{};   // [m]
    sf::math::Vec3 velocity{};   // [m/s]
    double mass{1000.0};         // [kg]

    sf::propagation::IntegratorConfig integrator{};

    int samples{10};
    std::string csv_path;

    static Scenario load(const std::string& path);
    [[nodiscard]] std::string describe() const;
};

}  // namespace orbitcli
