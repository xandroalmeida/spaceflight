#pragma once

// The single door through which astronomical data enters the simulation.
//
// Everything that needs to know where a body is receives a const reference to
// this interface -- never a singleton, never a global.  That is what makes it
// possible to substitute an analytic two-body provider in tests where an exact
// answer is known, and to keep CSPICE out of every other translation unit.

#include "core/celestial/body_id.hpp"
#include "core/coordinates/reference_frame.hpp"
#include "core/ephemeris/body_state.hpp"
#include "core/math/vec3.hpp"
#include "core/time/coordinate_time.hpp"

namespace sf::ephemeris {

// Inclusive interval over which a body's state is available.
struct CoverageWindow {
    time::CoordinateTime begin{};
    time::CoordinateTime end{};
    bool valid{false};

    [[nodiscard]] bool contains(time::CoordinateTime t) const {
        return valid && t >= begin && t <= end;
    }
};

class EphemerisProvider {
public:
    EphemerisProvider() = default;
    virtual ~EphemerisProvider() = default;

    EphemerisProvider(const EphemerisProvider&) = delete;
    EphemerisProvider& operator=(const EphemerisProvider&) = delete;

    // Geometric (uncorrected) state of `body` at coordinate time `time`,
    // expressed in `frame`.  Throws EphemerisUnavailable if the epoch is outside
    // the loaded data: extrapolating silently is not an option.
    //
    // Geometric, not light-time corrected: dynamics needs where the body *is*,
    // not where it is *seen*.  Apparent positions belong to rendering
    // (Milestone 5) and will be a separate, explicitly named call.
    [[nodiscard]] virtual BodyState state(celestial::BodyId body,
                                          time::CoordinateTime time,
                                          coordinates::ReferenceFrame frame) const = 0;

    // Position only.  The default implementation forwards to state(); providers
    // that can answer more cheaply may override it.  The gravity model calls
    // this once per body per force evaluation, i.e. ~7 times per step per body.
    [[nodiscard]] virtual math::Vec3 position(celestial::BodyId body,
                                              time::CoordinateTime time,
                                              coordinates::ReferenceFrame frame) const {
        return state(body, time, frame).state.position;
    }

    // Gravitational parameter GM [m^3/s^2].  Comes from the same data set as the
    // ephemeris, so the mass used in our dynamics matches the mass used to
    // generate the trajectories we integrate against.
    [[nodiscard]] virtual double gravitational_parameter(celestial::BodyId body) const = 0;

    // Mean radius [m], for proximity reporting and rendering scale.
    [[nodiscard]] virtual double mean_radius(celestial::BodyId body) const = 0;

    [[nodiscard]] virtual CoverageWindow coverage(celestial::BodyId body) const = 0;

    [[nodiscard]] virtual bool has_body(celestial::BodyId body) const = 0;
};

}  // namespace sf::ephemeris
