#include "core/math/quaternion.hpp"

#include "core/units/constants.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sf::math {

Quaternion Quaternion::from_axis_angle(const Vec3& axis, units::Angle angle) {
    const double length = axis.norm();
    if (length <= 0.0) {
        return identity();
    }
    const double half = 0.5 * angle.radians();
    const double s = std::sin(half) / length;
    return Quaternion{std::cos(half), axis.x * s, axis.y * s, axis.z * s};
}

Quaternion Quaternion::from_two_vectors(const Vec3& from, const Vec3& to) {
    const Vec3 a = from.normalized();
    const Vec3 b = to.normalized();
    if (a.norm_squared() == 0.0 || b.norm_squared() == 0.0) {
        return identity();
    }

    const double cosine = std::clamp(math::dot(a, b), -1.0, 1.0);
    if (cosine > 1.0 - 1.0e-12) {
        return identity();  // already aligned
    }
    if (cosine < -1.0 + 1.0e-12) {
        // Antiparallel: the rotation is by pi about ANY perpendicular axis.
        // Pick the one least aligned with `a` so the cross product is well
        // conditioned.
        const Vec3 fallback = std::abs(a.x) < 0.9 ? Vec3::unit_x() : Vec3::unit_y();
        const Vec3 axis = cross(a, fallback).normalized();
        return Quaternion{0.0, axis.x, axis.y, axis.z};
    }

    const Vec3 axis = cross(a, b);
    const double w = 1.0 + cosine;
    return Quaternion{w, axis.x, axis.y, axis.z}.normalized();
}

Quaternion Quaternion::from_rotation_matrix(const Mat3& m) {
    // Shepperd's method: pick the branch with the largest denominator, which
    // avoids the cancellation that the naive w-branch suffers near 180 degrees.
    const double trace = m.at(0, 0) + m.at(1, 1) + m.at(2, 2);
    if (trace > 0.0) {
        const double s = 0.5 / std::sqrt(trace + 1.0);
        return Quaternion{0.25 / s, (m.at(2, 1) - m.at(1, 2)) * s, (m.at(0, 2) - m.at(2, 0)) * s,
                          (m.at(1, 0) - m.at(0, 1)) * s}
            .normalized();
    }
    if (m.at(0, 0) > m.at(1, 1) && m.at(0, 0) > m.at(2, 2)) {
        const double s = 2.0 * std::sqrt(1.0 + m.at(0, 0) - m.at(1, 1) - m.at(2, 2));
        return Quaternion{(m.at(2, 1) - m.at(1, 2)) / s, 0.25 * s, (m.at(0, 1) + m.at(1, 0)) / s,
                          (m.at(0, 2) + m.at(2, 0)) / s}
            .normalized();
    }
    if (m.at(1, 1) > m.at(2, 2)) {
        const double s = 2.0 * std::sqrt(1.0 + m.at(1, 1) - m.at(0, 0) - m.at(2, 2));
        return Quaternion{(m.at(0, 2) - m.at(2, 0)) / s, (m.at(0, 1) + m.at(1, 0)) / s, 0.25 * s,
                          (m.at(1, 2) + m.at(2, 1)) / s}
            .normalized();
    }
    const double s = 2.0 * std::sqrt(1.0 + m.at(2, 2) - m.at(0, 0) - m.at(1, 1));
    return Quaternion{(m.at(1, 0) - m.at(0, 1)) / s, (m.at(0, 2) + m.at(2, 0)) / s,
                      (m.at(1, 2) + m.at(2, 1)) / s, 0.25 * s}
        .normalized();
}

Quaternion Quaternion::from_euler_zyx(const EulerZYX& angles) {
    return from_axis_angle(Vec3::unit_z(), angles.yaw) *
           from_axis_angle(Vec3::unit_y(), angles.pitch) *
           from_axis_angle(Vec3::unit_x(), angles.roll);
}

Quaternion Quaternion::normalized() const {
    const double n = norm();
    if (!(n > 0.0)) {
        return identity();
    }
    const double inv = 1.0 / n;
    return Quaternion{w_ * inv, x_ * inv, y_ * inv, z_ * inv};
}

bool Quaternion::is_finite() const {
    return std::isfinite(w_) && std::isfinite(x_) && std::isfinite(y_) && std::isfinite(z_);
}

Quaternion Quaternion::inverse() const {
    const double n2 = norm_squared();
    if (!(n2 > 0.0)) {
        return identity();
    }
    return conjugate() * (1.0 / n2);
}

Vec3 Quaternion::rotate(const Vec3& v) const {
    // v' = v + 2w(qv x v) + 2 qv x (qv x v), which costs less than forming the
    // two quaternion products and is numerically identical for a unit q.
    const Vec3 qv{x_, y_, z_};
    const Vec3 t = cross(qv, v) * 2.0;
    return v + t * w_ + cross(qv, t);
}

Vec3 Quaternion::rotate_inverse(const Vec3& v) const { return conjugate().rotate(v); }

Mat3 Quaternion::to_rotation_matrix() const {
    const double ww = w_ * w_;
    const double xx = x_ * x_;
    const double yy = y_ * y_;
    const double zz = z_ * z_;

    Mat3 m{};
    m.m = {{{ww + xx - yy - zz, 2.0 * (x_ * y_ - w_ * z_), 2.0 * (x_ * z_ + w_ * y_)},
            {2.0 * (x_ * y_ + w_ * z_), ww - xx + yy - zz, 2.0 * (y_ * z_ - w_ * x_)},
            {2.0 * (x_ * z_ - w_ * y_), 2.0 * (y_ * z_ + w_ * x_), ww - xx - yy + zz}}};
    return m;
}

EulerZYX Quaternion::to_euler_zyx() const {
    EulerZYX angles{};
    const double sin_pitch = std::clamp(2.0 * (w_ * y_ - z_ * x_), -1.0, 1.0);
    angles.pitch = units::Angle::radians(std::asin(sin_pitch));

    // Within this much of a pole the yaw/roll split is meaningless -- that is
    // gimbal lock, and it is why the STATE is never Euler angles (ADR-0008).
    if (std::abs(sin_pitch) > 1.0 - 1.0e-12) {
        angles.yaw = units::Angle::radians(2.0 * std::atan2(x_, w_));
        angles.roll = units::Angle::radians(0.0);
        return angles;
    }

    angles.roll = units::Angle::radians(
        std::atan2(2.0 * (w_ * x_ + y_ * z_), 1.0 - 2.0 * (x_ * x_ + y_ * y_)));
    angles.yaw = units::Angle::radians(
        std::atan2(2.0 * (w_ * z_ + x_ * y_), 1.0 - 2.0 * (y_ * y_ + z_ * z_)));
    return angles;
}

AxisAngle Quaternion::to_axis_angle() const {
    const Quaternion q = normalized();
    const double w = std::clamp(q.w_, -1.0, 1.0);
    const double s = std::sqrt(std::max(0.0, 1.0 - w * w));
    if (s < 1.0e-12) {
        return AxisAngle{Vec3::unit_x(), units::Angle::radians(0.0)};
    }
    return AxisAngle{Vec3{q.x_ / s, q.y_ / s, q.z_ / s},
                     units::Angle::radians(2.0 * std::acos(w))};
}

double Quaternion::dot(const Quaternion& a, const Quaternion& b) {
    return a.w_ * b.w_ + a.x_ * b.x_ + a.y_ * b.y_ + a.z_ * b.z_;
}

units::Angle Quaternion::angle_between(const Quaternion& a, const Quaternion& b) {
    const double d = std::clamp(std::abs(dot(a.normalized(), b.normalized())), -1.0, 1.0);
    return units::Angle::radians(2.0 * std::acos(d));
}

Quaternion Quaternion::nearest_to(const Quaternion& q, const Quaternion& reference) {
    return dot(q, reference) < 0.0 ? -q : q;
}

Quaternion operator*(const Quaternion& a, const Quaternion& b) {
    // Hamilton: ij = k. The JPL convention has ij = -k and would silently give
    // the transpose of every rotation composed here (ADR-0008).
    return Quaternion{a.w_ * b.w_ - a.x_ * b.x_ - a.y_ * b.y_ - a.z_ * b.z_,
                      a.w_ * b.x_ + a.x_ * b.w_ + a.y_ * b.z_ - a.z_ * b.y_,
                      a.w_ * b.y_ - a.x_ * b.z_ + a.y_ * b.w_ + a.z_ * b.x_,
                      a.w_ * b.z_ + a.x_ * b.y_ - a.y_ * b.x_ + a.z_ * b.w_};
}

std::string Quaternion::to_string() const {
    std::ostringstream os;
    os << std::setprecision(12) << "(" << w_ << ", " << x_ << ", " << y_ << ", " << z_ << ")";
    return os.str();
}

Quaternion attitude_derivative(const Quaternion& q, const Vec3& angular_velocity_body) {
    const Quaternion omega{0.0, angular_velocity_body.x, angular_velocity_body.y,
                           angular_velocity_body.z};
    return (q * omega) * 0.5;
}

}  // namespace sf::math
