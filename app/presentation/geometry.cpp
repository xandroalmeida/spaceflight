#include "app/presentation/geometry.hpp"

#include <numbers>

namespace sf::app {

Basis Basis::from_axis_angle(const Vec3& axis, double angle) {
    const Vec3 a = axis.normalized();
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    // Columns of the Rodrigues matrix.
    return {{t * a.x * a.x + c, t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y},
            {t * a.x * a.y - s * a.z, t * a.y * a.y + c, t * a.y * a.z + s * a.x},
            {t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c}};
}

Basis Basis::from_euler_degrees(const Vec3& degrees) {
    constexpr double to_rad = std::numbers::pi / 180.0;
    const Basis ry = from_axis_angle(Vec3::unit_y(), degrees.y * to_rad);
    const Basis rx = from_axis_angle(Vec3::unit_x(), degrees.x * to_rad);
    const Basis rz = from_axis_angle(Vec3::unit_z(), degrees.z * to_rad);
    return ry * rx * rz;
}

Basis Basis::orthonormalized() const {
    // Gram-Schmidt, x first, then y, then z.
    Vec3 nx = x.normalized();
    Vec3 ny = (y - nx * dot(nx, y)).normalized();
    Vec3 nz = (z - nx * dot(nx, z) - ny * dot(ny, z)).normalized();
    return {nx, ny, nz};
}

Basis Basis::inverse() const {
    // Rows of the inverse are the cross products of the columns over the
    // determinant.
    const Vec3 r0 = cross(y, z);
    const Vec3 r1 = cross(z, x);
    const Vec3 r2 = cross(x, y);
    const double det = dot(x, r0);
    const double inv = det != 0.0 ? 1.0 / det : 0.0;
    const Basis rows{r0 * inv, r1 * inv, r2 * inv};
    return rows.transposed();
}

Basis looking_at(const Vec3& direction, const Vec3& up) {
    const Vec3 v_z = (direction * -1.0).normalized();
    Vec3 v_x = cross(up, v_z).normalized();
    if (v_x.norm_squared() < 1.0e-24) {
        v_x = cross(Vec3::unit_x(), v_z).normalized();
    }
    const Vec3 v_y = cross(v_z, v_x);
    return {v_x, v_y, v_z};
}

double wrap_angle(double angle) {
    constexpr double tau = 2.0 * std::numbers::pi;
    double wrapped = std::fmod(angle + std::numbers::pi, tau);
    if (wrapped < 0.0) {
        wrapped += tau;
    }
    return wrapped - std::numbers::pi;
}


}  // namespace sf::app
