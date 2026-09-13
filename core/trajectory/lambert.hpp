#pragma once

// Lambert's problem: the orbit connecting two positions in a given time of
// flight.  Universal variables with bisection on psi.
// Formulation, validity and refused cases: docs/physics/lambert.md.

#include "core/math/vec3.hpp"
#include "core/time/duration.hpp"

#include <stdexcept>
#include <string>

namespace sf::trajectory {

enum class TransferDirection {
    Prograde,   // the transfer goes counter-clockwise seen from +z of the frame
    Retrograde
};

struct LambertSolution {
    math::Vec3 departure_velocity{};  // [m/s] velocity needed at r1
    math::Vec3 arrival_velocity{};    // [m/s] velocity at r2
    double transfer_angle{0.0};       // [rad]
    double semi_major_axis{0.0};      // [m], negative for hyperbolic transfers
    double achieved_time_of_flight{0.0};  // [s], for checking the convergence
    int iterations{0};
};

// Raised for the geometrically degenerate inputs listed in lambert.md section 3.
// A separate type because "180 degrees apart" is a legitimate question with no
// unique answer, not a programming error.
class LambertDegenerate : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

// `r1`, `r2` are positions relative to the central body of parameter `gm`.
// Throws LambertDegenerate for collinear positions, std::invalid_argument for
// non-positive gm or time of flight, std::runtime_error if the bisection does not
// converge (which means the requested time of flight is unreachable).
LambertSolution solve_lambert(const math::Vec3& r1, const math::Vec3& r2,
                              time::Duration time_of_flight, double gm,
                              TransferDirection direction = TransferDirection::Prograde);

// Stumpff functions, exposed because they are worth testing on their own.
double stumpff_c(double psi);
double stumpff_s(double psi);

}  // namespace sf::trajectory
