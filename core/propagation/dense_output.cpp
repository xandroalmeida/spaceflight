#include "core/propagation/dense_output.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace sf::propagation {

PropagationState state_from_array(const StateArray& y, double mass) {
    PropagationState state{};
    state.state.position = math::Vec3{y[0], y[1], y[2]};
    state.state.velocity = math::Vec3{y[3], y[4], y[5]};
    state.mass = mass;
    state.proper_time = time::Duration{y[6]};
    return state;
}

StateArray array_from_state(const PropagationState& state) {
    return StateArray{state.state.position.x, state.state.position.y, state.state.position.z,
                      state.state.velocity.x, state.state.velocity.y, state.state.velocity.z,
                      state.proper_time.seconds()};
}

PropagationState DenseSegment::at_theta(double theta) const {
    // Horner form of the Dormand-Prince continuous extension (Hairer, Norsett &
    // Wanner, Solving ODEs I, II.6, routine contd5):
    //
    //   y(t) = c1 + t (c2 + (1-t) (c3 + t (c4 + (1-t) c5)))
    const double th = std::clamp(theta, 0.0, 1.0);
    const double th1 = 1.0 - th;

    StateArray y{};
    for (std::size_t i = 0; i < kStateDimension; ++i) {
        y[i] = coefficients[0][i] +
               th * (coefficients[1][i] +
                     th1 * (coefficients[2][i] +
                            th * (coefficients[3][i] + th1 * coefficients[4][i])));
    }
    return state_from_array(y, mass);
}

bool DenseSegment::contains(time::CoordinateTime t) const {
    const auto finish = end();
    const auto lo = step_seconds >= 0.0 ? begin : finish;
    const auto hi = step_seconds >= 0.0 ? finish : begin;
    return t >= lo && t <= hi;
}

void Trajectory::clear() {
    segments_.clear();
    forward_ = true;
}

void Trajectory::reserve(std::size_t segments) { segments_.reserve(segments); }

void Trajectory::append(DenseSegment segment) {
    if (segments_.empty()) {
        forward_ = segment.step_seconds >= 0.0;
    }
    segments_.push_back(std::move(segment));
}

time::CoordinateTime Trajectory::earliest() const {
    if (segments_.empty()) {
        throw std::out_of_range("Trajectory::earliest on an empty trajectory");
    }
    return forward_ ? segments_.front().begin : segments_.back().end();
}

time::CoordinateTime Trajectory::latest() const {
    if (segments_.empty()) {
        throw std::out_of_range("Trajectory::latest on an empty trajectory");
    }
    return forward_ ? segments_.back().end() : segments_.front().begin;
}

bool Trajectory::contains(time::CoordinateTime t) const {
    return !segments_.empty() && t >= earliest() && t <= latest();
}

const DenseSegment& Trajectory::locate(time::CoordinateTime t) const {
    // Segments are stored in integration order, which is descending in time when
    // propagating backwards; the search follows that order rather than sorting.
    std::size_t lo = 0;
    std::size_t hi = segments_.size() - 1;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const bool beyond =
            forward_ ? (t > segments_[mid].end()) : (t < segments_[mid].end());
        if (beyond) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return segments_[lo];
}

PropagationState Trajectory::state_at(time::CoordinateTime t) const {
    if (segments_.empty()) {
        throw std::out_of_range("Trajectory::state_at on an empty trajectory");
    }
    if (!contains(t)) {
        std::ostringstream os;
        os << "Trajectory::state_at: " << t.to_string() << " lies outside the recorded arc ["
           << earliest().to_string() << " .. " << latest().to_string()
           << "]; extrapolation is not available";
        throw std::out_of_range(os.str());
    }

    const DenseSegment& segment = locate(t);
    const double theta = (t - segment.begin).seconds() / segment.step_seconds;
    return segment.at_theta(theta);
}

std::vector<std::pair<time::CoordinateTime, PropagationState>> Trajectory::sample(
    std::size_t count) const {
    if (segments_.empty() || count == 0) {
        return {};
    }

    const auto from = earliest();
    const auto to = latest();
    const double span = (to - from).seconds();

    std::vector<std::pair<time::CoordinateTime, PropagationState>> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double fraction =
            count == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(count - 1);
        const auto t = i + 1 == count ? to : from + time::Duration::seconds(span * fraction);
        out.emplace_back(t, state_at(t));
    }
    return out;
}

std::vector<std::pair<time::CoordinateTime, PropagationState>> Trajectory::sample_every(
    time::Duration interval) const {
    if (segments_.empty() || interval.seconds() <= 0.0) {
        return {};
    }

    const auto from = earliest();
    const auto to = latest();
    const double span = (to - from).seconds();
    const auto steps = static_cast<std::size_t>(std::floor(span / interval.seconds()));

    std::vector<std::pair<time::CoordinateTime, PropagationState>> out;
    out.reserve(steps + 2);
    for (std::size_t i = 0; i <= steps; ++i) {
        const auto t = from + interval * static_cast<double>(i);
        out.emplace_back(t, state_at(t));
    }
    if (out.empty() || out.back().first < to) {
        out.emplace_back(to, state_at(to));
    }
    return out;
}

}  // namespace sf::propagation
