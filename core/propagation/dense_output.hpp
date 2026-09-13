#pragma once

// Continuous extension of the propagator: the state at any instant inside an
// integration step, without re-integrating and without forcing the step size.
//
// Why this exists at all: the integrator picks its steps from error control
// (5 s near periapsis, 3600 s near apoapsis) while the renderer wants a state
// every 16.7 ms and the CLI wants samples on round epochs.  Making the integrator
// land on those instants would tie the timestep to the frame rate, which rule
// section 12 forbids.  See ADR-0006.

#include "core/propagation/propagation_state.hpp"
#include "core/time/coordinate_time.hpp"
#include "core/time/duration.hpp"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace sf::propagation {

// [x y z vx vy vz tau] -- the vector the propagator integrates.
inline constexpr std::size_t kStateDimension = 7;
using StateArray = std::array<double, kStateDimension>;

PropagationState state_from_array(const StateArray& y, double mass);
StateArray array_from_state(const PropagationState& state);

// One accepted step, stored as the five coefficient vectors of the Dormand-Prince
// 4th order interpolant.  Costs no extra force evaluations: it is built from the
// seven stages the step already computed.
struct DenseSegment {
    time::CoordinateTime begin{};
    double step_seconds{0.0};  // signed: negative when propagating backwards
    double mass{1.0};
    std::array<StateArray, 5> coefficients{};

    [[nodiscard]] time::CoordinateTime end() const {
        return begin + time::Duration::seconds(step_seconds);
    }

    // theta = (t - begin)/step_seconds, in [0, 1].  Exact at both endpoints by
    // construction: theta = 0 returns the step's initial state, theta = 1 its
    // final state.
    [[nodiscard]] PropagationState at_theta(double theta) const;
    [[nodiscard]] bool contains(time::CoordinateTime t) const;
};

// A propagated arc, queryable at arbitrary epochs.
//
// Asking for an instant outside the recorded arc throws: extrapolation is refused
// here for the same reason it is refused in EphemerisProvider (ADR-0003).
class Trajectory {
public:
    void clear();
    void reserve(std::size_t segments);
    void append(DenseSegment segment);

    [[nodiscard]] bool empty() const noexcept { return segments_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return segments_.size(); }
    [[nodiscard]] const std::vector<DenseSegment>& segments() const noexcept { return segments_; }

    // Chronological bounds, regardless of the integration direction.
    [[nodiscard]] time::CoordinateTime earliest() const;
    [[nodiscard]] time::CoordinateTime latest() const;
    [[nodiscard]] bool contains(time::CoordinateTime t) const;

    [[nodiscard]] PropagationState state_at(time::CoordinateTime t) const;

    // `count` states evenly spaced in coordinate time, endpoints included.
    [[nodiscard]] std::vector<std::pair<time::CoordinateTime, PropagationState>> sample(
        std::size_t count) const;
    [[nodiscard]] std::vector<std::pair<time::CoordinateTime, PropagationState>> sample_every(
        time::Duration interval) const;

private:
    [[nodiscard]] const DenseSegment& locate(time::CoordinateTime t) const;

    std::vector<DenseSegment> segments_;
    bool forward_{true};
};

}  // namespace sf::propagation
