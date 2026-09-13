#include "core/trajectory/lambert.hpp"

#include "core/units/constants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace sf::trajectory {
namespace {

using math::Vec3;

// Below this the series expansions are used instead of the closed forms, which
// lose all significance through cancellation as psi -> 0.
constexpr double kSeriesThreshold = 1.0e-6;

constexpr int kMaxIterations = 200;
constexpr double kTimeTolerance = 1.0e-10;  // relative

}  // namespace

double stumpff_c(double psi) {
    if (psi > kSeriesThreshold) {
        const double root = std::sqrt(psi);
        return (1.0 - std::cos(root)) / psi;
    }
    if (psi < -kSeriesThreshold) {
        const double root = std::sqrt(-psi);
        return (std::cosh(root) - 1.0) / (-psi);
    }
    // C(psi) = 1/2 - psi/24 + psi^2/720 - ...
    return 0.5 - psi / 24.0 + psi * psi / 720.0;
}

double stumpff_s(double psi) {
    if (psi > kSeriesThreshold) {
        const double root = std::sqrt(psi);
        return (root - std::sin(root)) / (root * root * root);
    }
    if (psi < -kSeriesThreshold) {
        const double root = std::sqrt(-psi);
        return (std::sinh(root) - root) / (root * root * root);
    }
    // S(psi) = 1/6 - psi/120 + psi^2/5040 - ...
    return 1.0 / 6.0 - psi / 120.0 + psi * psi / 5040.0;
}

LambertSolution solve_lambert(const Vec3& r1_vec, const Vec3& r2_vec,
                              time::Duration time_of_flight, double gm,
                              TransferDirection direction) {
    if (!(gm > 0.0)) {
        throw std::invalid_argument("solve_lambert: gm must be > 0");
    }
    const double tof = time_of_flight.seconds();
    if (!(tof > 0.0) || !std::isfinite(tof)) {
        throw std::invalid_argument("solve_lambert: time of flight must be finite and > 0");
    }

    const double r1 = r1_vec.norm();
    const double r2 = r2_vec.norm();
    if (!(r1 > 0.0) || !(r2 > 0.0)) {
        throw LambertDegenerate("solve_lambert: a position vector has zero length");
    }

    double cos_transfer = dot(r1_vec, r2_vec) / (r1 * r2);
    cos_transfer = std::clamp(cos_transfer, -1.0, 1.0);
    double transfer_angle = std::acos(cos_transfer);

    // Which way round?  The sign of the z component of r1 x r2 tells us whether
    // the short way is prograde in this frame.
    const Vec3 h = cross(r1_vec, r2_vec);
    const bool short_way_is_prograde = h.z >= 0.0;
    if ((direction == TransferDirection::Prograde && !short_way_is_prograde) ||
        (direction == TransferDirection::Retrograde && short_way_is_prograde)) {
        transfer_angle = units::two_pi - transfer_angle;
    }

    const double sin_transfer = std::sin(transfer_angle);
    const double denominator = 1.0 - std::cos(transfer_angle);
    if (std::abs(sin_transfer) < 1.0e-12 || denominator <= 0.0) {
        std::ostringstream os;
        os << "solve_lambert: transfer angle is " << transfer_angle
           << " rad, which is degenerate. At 0 or 2*pi the positions are collinear and the "
              "same; at exactly pi the transfer plane is undefined -- every plane containing "
              "both points is a solution. Ask for 179.9 or 180.1 degrees instead "
              "(docs/physics/lambert.md section 3)";
        throw LambertDegenerate(os.str());
    }

    const double A = sin_transfer * std::sqrt(r1 * r2 / denominator);
    if (std::abs(A) < 1.0e-12) {
        throw LambertDegenerate("solve_lambert: geometry constant A is zero; positions are collinear");
    }

    // Bracket: psi below 4*pi^2 keeps us inside one revolution; the lower bound is
    // pushed down until the hyperbolic branch is long enough.
    double psi_low = -4.0 * units::pi * units::pi;
    double psi_high = 4.0 * units::pi * units::pi;
    double psi = 0.0;

    LambertSolution solution{};
    double y = 0.0;
    double achieved = 0.0;

    int iteration = 0;
    for (; iteration < kMaxIterations; ++iteration) {
        const double c = stumpff_c(psi);
        const double s = stumpff_s(psi);

        y = r1 + r2 + A * (psi * s - 1.0) / std::sqrt(c);

        // y < 0 means the geometry is impossible for this psi; raise the floor.
        if (A > 0.0 && y < 0.0) {
            psi_low = psi;
            psi = 0.8 * psi_high + 0.2 * psi;  // creep upwards, staying bracketed
            continue;
        }

        const double chi = std::sqrt(y / c);
        achieved = (chi * chi * chi * s + A * std::sqrt(y)) / std::sqrt(gm);

        if (std::abs(achieved - tof) <= kTimeTolerance * tof) {
            break;
        }

        // dt(psi) is monotonically increasing: pure bisection, no derivative.
        if (achieved <= tof) {
            psi_low = psi;
        } else {
            psi_high = psi;
        }
        psi = 0.5 * (psi_low + psi_high);
    }

    if (iteration >= kMaxIterations) {
        std::ostringstream os;
        os << "solve_lambert: bisection did not reach the requested time of flight (" << tof
           << " s); the closest reachable within one revolution was " << achieved
           << " s. The requested transfer is probably faster than the parabolic minimum "
              "or slower than one revolution";
        throw std::runtime_error(os.str());
    }

    const double f = 1.0 - y / r1;
    const double g = A * std::sqrt(y / gm);
    const double g_dot = 1.0 - y / r2;

    solution.departure_velocity = (r2_vec - r1_vec * f) / g;
    solution.arrival_velocity = (r2_vec * g_dot - r1_vec) / g;
    solution.transfer_angle = transfer_angle;
    solution.achieved_time_of_flight = achieved;
    solution.iterations = iteration + 1;

    // Semi-major axis from the departure state, for reporting.
    const double v1_squared = solution.departure_velocity.norm_squared();
    const double energy = 0.5 * v1_squared - gm / r1;
    solution.semi_major_axis = energy != 0.0 ? -gm / (2.0 * energy)
                                             : std::numeric_limits<double>::infinity();
    return solution;
}

}  // namespace sf::trajectory
