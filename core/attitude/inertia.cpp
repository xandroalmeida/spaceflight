#include "core/attitude/inertia.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sf::attitude {
namespace {

using math::Mat3;
using math::Vec3;

void require_positive(double value, const char* what) {
    if (!(value > 0.0) || !std::isfinite(value)) {
        throw std::invalid_argument(std::string{what} + " must be finite and > 0");
    }
}

}  // namespace

InertiaTensor::InertiaTensor(const Mat3& tensor) : tensor_(tensor) {
    // Symmetry.
    for (int r = 0; r < 3; ++r) {
        for (int c = r + 1; c < 3; ++c) {
            const double scale = std::max({1.0, std::abs(tensor.at(r, c)), std::abs(tensor.at(c, r))});
            if (std::abs(tensor.at(r, c) - tensor.at(c, r)) > 1.0e-12 * scale) {
                throw std::invalid_argument("InertiaTensor: the tensor must be symmetric");
            }
        }
    }

    const double a = tensor.at(0, 0);
    const double b = tensor.at(1, 1);
    const double c = tensor.at(2, 2);
    require_positive(a, "I_xx");
    require_positive(b, "I_yy");
    require_positive(c, "I_zz");

    // Triangle inequalities. Checked on the diagonal, which is exact for a
    // principal-axis tensor and a necessary condition in general.
    const double tolerance = 1.0e-9 * std::max({a, b, c});
    if (a + b + tolerance < c || a + c + tolerance < b || b + c + tolerance < a) {
        std::ostringstream os;
        os << "InertiaTensor: the moments (" << a << ", " << b << ", " << c
           << ") violate the triangle inequalities, so they describe no physical mass "
              "distribution (docs/physics/attitude.md section 3)";
        throw std::invalid_argument(os.str());
    }

    const auto det = tensor_.determinant();
    if (!(det > 0.0)) {
        throw std::invalid_argument("InertiaTensor: the tensor must be positive definite");
    }

    // Explicit inverse of a symmetric 3x3 via the adjugate.
    Mat3 adjugate{};
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) {
            const int r1 = (r + 1) % 3;
            const int r2 = (r + 2) % 3;
            const int c1 = (col + 1) % 3;
            const int c2 = (col + 2) % 3;
            adjugate.m[static_cast<std::size_t>(col)][static_cast<std::size_t>(r)] =
                (tensor_.at(r1, c1) * tensor_.at(r2, c2) - tensor_.at(r1, c2) * tensor_.at(r2, c1)) /
                det;
        }
    }
    inverse_ = adjugate;
}

InertiaTensor InertiaTensor::principal(double i_xx, double i_yy, double i_zz) {
    Mat3 tensor{};
    tensor.m = {{{i_xx, 0.0, 0.0}, {0.0, i_yy, 0.0}, {0.0, 0.0, i_zz}}};
    return InertiaTensor{tensor};
}

InertiaTensor InertiaTensor::from_matrix(const Mat3& tensor) { return InertiaTensor{tensor}; }

InertiaTensor InertiaTensor::solid_box(double mass, const Vec3& size) {
    require_positive(mass, "mass");
    require_positive(size.x, "size.x");
    require_positive(size.y, "size.y");
    require_positive(size.z, "size.z");
    const double k = mass / 12.0;
    return principal(k * (size.y * size.y + size.z * size.z),
                     k * (size.x * size.x + size.z * size.z),
                     k * (size.x * size.x + size.y * size.y));
}

InertiaTensor InertiaTensor::solid_cylinder(double mass, double radius, double length) {
    require_positive(mass, "mass");
    require_positive(radius, "radius");
    require_positive(length, "length");
    const double transverse = mass * (3.0 * radius * radius + length * length) / 12.0;
    return principal(transverse, transverse, 0.5 * mass * radius * radius);
}

InertiaTensor InertiaTensor::solid_sphere(double mass, double radius) {
    require_positive(mass, "mass");
    require_positive(radius, "radius");
    const double i = 0.4 * mass * radius * radius;
    return principal(i, i, i);
}

Vec3 InertiaTensor::principal_moments() const {
    return Vec3{tensor_.at(0, 0), tensor_.at(1, 1), tensor_.at(2, 2)};
}

double InertiaTensor::rotational_energy(const Vec3& angular_velocity) const {
    return 0.5 * dot(angular_velocity, tensor_ * angular_velocity);
}

std::string InertiaTensor::describe() const {
    const Vec3 moments = principal_moments();
    std::ostringstream os;
    os << std::setprecision(8) << "I = (" << moments.x << ", " << moments.y << ", " << moments.z
       << ") kg m^2";
    return os.str();
}

}  // namespace sf::attitude
