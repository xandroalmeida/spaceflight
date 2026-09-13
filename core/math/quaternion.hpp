#pragma once

// Unit quaternion, scalar first, Hamilton algebra, body -> inertial.
//
//     v_inertial = q (x) v_body (x) q*
//
// All four of those choices are decisions, not defaults: see ADR-0008. The type
// is distinct from Vec3 on purpose -- the product is not componentwise, and a
// four-element vector would let the two be confused.

#include "core/math/mat3.hpp"
#include "core/math/vec3.hpp"

#include <cmath>
#include <string>

namespace sf::math {

struct EulerZYX {
    double yaw{0.0};    // about z, applied first  [rad]
    double pitch{0.0};  // about y'                [rad]
    double roll{0.0};   // about x''               [rad]
};

class Quaternion {
public:
    constexpr Quaternion() = default;
    constexpr Quaternion(double w, double x, double y, double z) : w_(w), x_(x), y_(y), z_(z) {}

    static constexpr Quaternion identity() { return Quaternion{1.0, 0.0, 0.0, 0.0}; }

    // Rotation of `angle` radians about `axis` (need not be normalised).
    static Quaternion from_axis_angle(const Vec3& axis, double angle);

    // The shortest rotation taking `from` onto `to`. Handles the antiparallel
    // case, where the axis is undefined and any perpendicular one will do.
    static Quaternion from_two_vectors(const Vec3& from, const Vec3& to);

    // Orthonormal basis whose columns are the body axes expressed in the
    // inertial frame.
    static Quaternion from_rotation_matrix(const Mat3& m);

    static Quaternion from_euler_zyx(const EulerZYX& angles);

    [[nodiscard]] constexpr double w() const { return w_; }
    [[nodiscard]] constexpr double x() const { return x_; }
    [[nodiscard]] constexpr double y() const { return y_; }
    [[nodiscard]] constexpr double z() const { return z_; }
    [[nodiscard]] constexpr Vec3 vector() const { return Vec3{x_, y_, z_}; }

    [[nodiscard]] constexpr double norm_squared() const {
        return w_ * w_ + x_ * x_ + y_ * y_ + z_ * z_;
    }
    [[nodiscard]] double norm() const { return std::sqrt(norm_squared()); }
    [[nodiscard]] Quaternion normalized() const;
    [[nodiscard]] bool is_finite() const;

    [[nodiscard]] constexpr Quaternion conjugate() const {
        return Quaternion{w_, -x_, -y_, -z_};
    }
    // For a unit quaternion the inverse IS the conjugate; this one does not
    // assume unit norm.
    [[nodiscard]] Quaternion inverse() const;

    [[nodiscard]] Vec3 rotate(const Vec3& v) const;
    [[nodiscard]] Vec3 rotate_inverse(const Vec3& v) const;
    [[nodiscard]] Mat3 to_rotation_matrix() const;

    // Display only -- never state (rule 19). Degenerate at pitch = +/-90 deg,
    // which is exactly why the state is a quaternion.
    [[nodiscard]] EulerZYX to_euler_zyx() const;

    void to_axis_angle(Vec3& axis, double& angle) const;

    // q and -q are the same rotation, so the angle between two orientations uses
    // |dot| and never the raw difference.
    [[nodiscard]] static double dot(const Quaternion& a, const Quaternion& b);
    [[nodiscard]] static double angle_between(const Quaternion& a, const Quaternion& b);

    // Returns whichever of q, -q is closer to `reference`. Used before any
    // interpolation or error computation, so the controller never takes the long
    // way round (docs/physics/attitude.md section 7).
    [[nodiscard]] static Quaternion nearest_to(const Quaternion& q, const Quaternion& reference);

    friend Quaternion operator*(const Quaternion& a, const Quaternion& b);
    friend constexpr Quaternion operator*(const Quaternion& q, double s) {
        return Quaternion{q.w_ * s, q.x_ * s, q.y_ * s, q.z_ * s};
    }
    friend constexpr Quaternion operator+(const Quaternion& a, const Quaternion& b) {
        return Quaternion{a.w_ + b.w_, a.x_ + b.x_, a.y_ + b.y_, a.z_ + b.z_};
    }
    friend constexpr Quaternion operator-(const Quaternion& q) {
        return Quaternion{-q.w_, -q.x_, -q.y_, -q.z_};
    }

    [[nodiscard]] std::string to_string() const;

private:
    double w_{1.0};
    double x_{0.0};
    double y_{0.0};
    double z_{0.0};
};

// Time derivative of the orientation for a body-frame angular velocity:
//
//     qdot = 1/2 q (x) (0, omega_body)
//
// The order matters: with the body->inertial convention, omega multiplies from
// the RIGHT. Swapping it rotates the right way about the wrong axis, which looks
// almost correct and is the classic bug of this subject.
Quaternion attitude_derivative(const Quaternion& q, const Vec3& angular_velocity_body);

}  // namespace sf::math
