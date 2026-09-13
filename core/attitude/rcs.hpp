#pragma once

// Reaction control system: thrusters that produce force AND torque (rule 19).
//
// A thruster at `position` in the body frame, pushing the ship along
// `direction`, contributes
//
//     force_body  = F * direction
//     torque_body = position x force_body
//
// Nothing here can "just rotate the ship": every rotation costs propellant out
// of the same tank, through the same F = eta*q*w as the main engine, and an
// unbalanced thruster displaces the ship as well as turning it.
// See docs/physics/attitude.md section 6.

#include "core/attitude/inertia.hpp"
#include "core/math/vec3.hpp"
#include "core/propulsion/engine.hpp"

#include <string>
#include <vector>

namespace sf::attitude {

struct RcsThruster {
    std::string name;
    math::Vec3 position{};    // body frame, relative to the centre of mass [m]
    math::Vec3 direction{};   // body frame, direction of the FORCE on the ship
    propulsion::EngineSpec engine;

    // Torque per unit throttle, body frame [N m].
    [[nodiscard]] math::Vec3 torque_at(double throttle) const;
    [[nodiscard]] math::Vec3 force_at(double throttle) const;
};

struct RcsOutput {
    math::Vec3 force_body{};   // [N]
    math::Vec3 torque_body{};  // [N m]
    double mass_flow{0.0};     // [kg/s], positive = consumption
};

class RcsSystem {
public:
    explicit RcsSystem(std::vector<RcsThruster> thrusters);

    // Twelve thrusters in six pure couples, two per body axis per direction.
    // A couple produces torque with ZERO net force, which is what a well laid
    // out RCS does; `single_thruster` exists so the tests can show what happens
    // when it is not (docs/physics/attitude.md section 6).
    static RcsSystem couples(double arm_metres, const propulsion::EngineSpec& engine);

    [[nodiscard]] const std::vector<RcsThruster>& thrusters() const noexcept { return thrusters_; }
    [[nodiscard]] std::size_t size() const noexcept { return thrusters_.size(); }

    // Largest torque available about each body axis, for the controller to
    // saturate against.
    [[nodiscard]] math::Vec3 max_torque() const noexcept { return max_torque_; }

    // Greedy allocation: each thruster opens in proportion to how much its own
    // torque direction agrees with what was asked for.
    //
    // This is NOT an optimal allocator -- a real one solves a constrained least
    // squares, and a real RCS is on/off rather than throttled, which would make
    // every command a discontinuity for the integrator to trip over
    // (docs/architecture/navigation.md section 4). Throttled and greedy is the
    // honest simplification, and it is named as one.
    [[nodiscard]] std::vector<double> allocate(const math::Vec3& desired_torque_body) const;

    [[nodiscard]] RcsOutput evaluate(const std::vector<double>& throttles) const;
    [[nodiscard]] RcsOutput evaluate(const math::Vec3& desired_torque_body) const;

    [[nodiscard]] std::string describe() const;

private:
    std::vector<RcsThruster> thrusters_;
    math::Vec3 max_torque_{};
};

}  // namespace sf::attitude
