#pragma once

// Inertia tensor in the body frame, validated on construction.
//
// A tensor that violates the triangle inequalities corresponds to no real mass
// distribution at all, and the resulting dynamics conserves energy and angular
// momentum while doing things no body does. It is refused.
// See docs/physics/attitude.md section 3.

#include "core/math/mat3.hpp"
#include "core/math/vec3.hpp"

#include <string>

namespace sf::attitude {

class InertiaTensor {
public:
    // Principal moments about the body axes, with no products of inertia.
    static InertiaTensor principal(double i_xx, double i_yy, double i_zz);

    // Full symmetric tensor. Throws if it is not symmetric or not physical.
    static InertiaTensor from_matrix(const math::Mat3& tensor);

    // Solid rectangular box of mass m and side lengths (a, b, c):
    //     I_xx = m(b^2 + c^2)/12, and cyclic.
    static InertiaTensor solid_box(double mass, const math::Vec3& size);

    // Solid cylinder of mass m, radius r, length l, axis along +z:
    //     I_zz = m r^2 / 2,  I_xx = I_yy = m(3r^2 + l^2)/12.
    static InertiaTensor solid_cylinder(double mass, double radius, double length);

    // Solid sphere: I = 2 m r^2 / 5 about every axis.
    static InertiaTensor solid_sphere(double mass, double radius);

    [[nodiscard]] const math::Mat3& tensor() const noexcept { return tensor_; }
    [[nodiscard]] const math::Mat3& inverse() const noexcept { return inverse_; }
    [[nodiscard]] math::Vec3 principal_moments() const;

    // I * omega and I^-1 * tau, the two products the dynamics needs.
    [[nodiscard]] math::Vec3 angular_momentum(const math::Vec3& angular_velocity) const {
        return tensor_ * angular_velocity;
    }
    [[nodiscard]] math::Vec3 angular_acceleration(const math::Vec3& torque) const {
        return inverse_ * torque;
    }

    // T = 1/2 omega . (I omega)
    [[nodiscard]] double rotational_energy(const math::Vec3& angular_velocity) const;

    [[nodiscard]] std::string describe() const;

private:
    explicit InertiaTensor(const math::Mat3& tensor);

    math::Mat3 tensor_{};
    math::Mat3 inverse_{};
};

}  // namespace sf::attitude
