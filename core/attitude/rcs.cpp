#include "core/attitude/rcs.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sf::attitude {

using math::Vec3;

Vec3 RcsThruster::force_at(double throttle) const {
    return direction.normalized() * engine.thrust_at(throttle);
}

Vec3 RcsThruster::torque_at(double throttle) const { return cross(position, force_at(throttle)); }

RcsSystem::RcsSystem(std::vector<RcsThruster> thrusters) : thrusters_(std::move(thrusters)) {
    if (thrusters_.empty()) {
        throw std::invalid_argument("RcsSystem: no thrusters");
    }
    for (const auto& thruster : thrusters_) {
        if (thruster.direction.norm() <= 0.0) {
            throw std::invalid_argument("RcsSystem: thruster \"" + thruster.name +
                                        "\" has no direction");
        }
        const Vec3 torque = thruster.torque_at(1.0);
        max_torque_.x += std::abs(torque.x);
        max_torque_.y += std::abs(torque.y);
        max_torque_.z += std::abs(torque.z);
    }
}

RcsSystem RcsSystem::couples(double arm_metres, const propulsion::EngineSpec& engine) {
    if (!(arm_metres > 0.0)) {
        throw std::invalid_argument("RcsSystem::couples: arm must be > 0");
    }

    std::vector<RcsThruster> thrusters;
    const double a = arm_metres;

    // For a couple about +x: a thruster at +y pushing +z and one at -y pushing
    // -z. Each contributes torque (a*F, 0, 0) and the two forces cancel exactly.
    const struct {
        const char* name;
        Vec3 offset;
        Vec3 push;
    } layout[] = {
        {"+x a", Vec3{0.0, a, 0.0}, Vec3{0.0, 0.0, 1.0}},
        {"+x b", Vec3{0.0, -a, 0.0}, Vec3{0.0, 0.0, -1.0}},
        {"-x a", Vec3{0.0, a, 0.0}, Vec3{0.0, 0.0, -1.0}},
        {"-x b", Vec3{0.0, -a, 0.0}, Vec3{0.0, 0.0, 1.0}},
        {"+y a", Vec3{0.0, 0.0, a}, Vec3{1.0, 0.0, 0.0}},
        {"+y b", Vec3{0.0, 0.0, -a}, Vec3{-1.0, 0.0, 0.0}},
        {"-y a", Vec3{0.0, 0.0, a}, Vec3{-1.0, 0.0, 0.0}},
        {"-y b", Vec3{0.0, 0.0, -a}, Vec3{1.0, 0.0, 0.0}},
        {"+z a", Vec3{a, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}},
        {"+z b", Vec3{-a, 0.0, 0.0}, Vec3{0.0, -1.0, 0.0}},
        {"-z a", Vec3{a, 0.0, 0.0}, Vec3{0.0, -1.0, 0.0}},
        {"-z b", Vec3{-a, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}},
    };

    for (const auto& entry : layout) {
        thrusters.push_back(RcsThruster{entry.name, entry.offset, entry.push, engine});
    }
    return RcsSystem{std::move(thrusters)};
}

std::vector<double> RcsSystem::allocate(const Vec3& desired_torque_body) const {
    std::vector<double> throttles(thrusters_.size(), 0.0);
    const double magnitude = desired_torque_body.norm();
    if (!(magnitude > 0.0)) {
        return throttles;
    }
    const Vec3 wanted = desired_torque_body / magnitude;

    for (std::size_t i = 0; i < thrusters_.size(); ++i) {
        const Vec3 torque = thrusters_[i].torque_at(1.0);
        const double available = torque.norm();
        if (available <= 0.0) {
            continue;
        }
        // How much of what we want this thruster can deliver, as a fraction of
        // its own maximum. Negative means it would push the wrong way: closed.
        const double alignment = dot(torque / available, wanted);
        if (alignment <= 0.0) {
            continue;
        }
        throttles[i] = std::clamp(alignment * magnitude / available, 0.0, 1.0);
    }
    return throttles;
}

RcsOutput RcsSystem::evaluate(const std::vector<double>& throttles) const {
    if (throttles.size() != thrusters_.size()) {
        throw std::invalid_argument("RcsSystem::evaluate: one throttle per thruster is required");
    }

    RcsOutput out{};
    for (std::size_t i = 0; i < thrusters_.size(); ++i) {
        const double throttle = std::clamp(throttles[i], 0.0, 1.0);
        if (throttle <= 0.0) {
            continue;
        }
        out.force_body += thrusters_[i].force_at(throttle);
        out.torque_body += thrusters_[i].torque_at(throttle);
        out.mass_flow += thrusters_[i].engine.mass_flow_at(throttle);
    }
    return out;
}

RcsOutput RcsSystem::evaluate(const Vec3& desired_torque_body) const {
    return evaluate(allocate(desired_torque_body));
}

std::string RcsSystem::describe() const {
    std::ostringstream os;
    os << std::setprecision(6) << thrusters_.size() << " thrusters, max torque ("
       << max_torque_.x << ", " << max_torque_.y << ", " << max_torque_.z << ") N m, "
       << thrusters_.front().engine.thrust_at(1.0) << " N each";
    return os.str();
}

}  // namespace sf::attitude
