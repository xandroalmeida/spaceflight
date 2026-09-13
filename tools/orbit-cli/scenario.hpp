#pragma once

// Scenario file for `orbit-cli propagate`.
//
// Everything the CLI needs to reproduce a run lives in the file: epoch, body
// set, initial state, tolerances.  Reproducibility is the point -- a scenario
// plus a git revision must give the same numbers twice.

#include "core/celestial/body_id.hpp"
#include "core/navigation/maneuver.hpp"
#include "core/spacecraft/spacecraft.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/math/vec3.hpp"
#include "core/propagation/spacecraft_propagator.hpp"

#include <optional>
#include <string>
#include <vector>

namespace orbitcli {

// A maneuver as written in the file.  Epochs are seconds after the scenario
// epoch, which keeps the file readable and avoids a second date format; and the
// burn can be given either as a duration or as a delta-v, with the rocket
// equation filling in the other (docs/architecture/navigation.md section 8).
struct ScenarioManeuver {
    std::string name{"burn"};
    double ignition_s{0.0};
    std::optional<double> duration_s;
    std::optional<double> delta_v_ms;
    double throttle{1.0};
    sf::navigation::GuidanceMode guidance{sf::navigation::GuidanceMode::Prograde};
    std::optional<sf::celestial::BodyId> reference;
    sf::math::Vec3 inertial_direction{1.0, 0.0, 0.0};
};

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
    double mass{1000.0};         // [kg]; used only when no engine is configured

    // Present when the file describes a ship that can burn.  Without it the
    // scenario is a coasting test particle, which is all Milestone 0 needed.
    std::optional<sf::spacecraft::Spacecraft> craft;
    std::vector<ScenarioManeuver> maneuvers;

    // Bodies whose J2 oblateness term is included, on top of their point mass.
    // See docs/physics/geopotential.md.
    std::vector<sf::celestial::BodyId> j2_bodies;

    sf::propagation::IntegratorConfig integrator{};

    int samples{10};
    std::string csv_path;

    [[nodiscard]] double initial_mass() const { return craft ? craft->initial_mass() : mass; }

    static Scenario load(const std::string& path);
    [[nodiscard]] std::string describe() const;
};

}  // namespace orbitcli
