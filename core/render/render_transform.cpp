#include "core/render/render_transform.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace sf::render {
namespace {

float to_float(double value) { return static_cast<float>(value); }

}  // namespace

float RenderVec3::length() const { return std::sqrt(x * x + y * y + z * z); }

RenderTransform::RenderTransform(double scale) { set_scale(scale); }

void RenderTransform::set_camera_origin(const math::Vec3& absolute_position) {
    if (!absolute_position.is_finite()) {
        throw std::invalid_argument("RenderTransform: camera origin must be finite");
    }
    origin_ = absolute_position;
}

void RenderTransform::set_scale(double scale) {
    if (!(scale > 0.0) || !std::isfinite(scale)) {
        throw std::invalid_argument("RenderTransform: scale must be finite and > 0");
    }
    scale_ = scale;
}

void RenderTransform::set_body_scale_exaggeration(double factor) {
    if (!(factor > 0.0) || !std::isfinite(factor)) {
        throw std::invalid_argument("RenderTransform: body scale exaggeration must be > 0");
    }
    exaggeration_ = factor;
}

RenderVec3 RenderTransform::to_render(const math::Vec3& absolute_position) const {
    // The subtraction happens in double.  That is the entire technique: the
    // result is small, and only then does it meet a float.
    const math::Vec3 relative = (absolute_position - origin_) * scale_;
    return RenderVec3{to_float(relative.x), to_float(relative.y), to_float(relative.z)};
}

RenderVec3 RenderTransform::vector_to_render(const math::Vec3& vector) const {
    const math::Vec3 scaled = vector * scale_;
    return RenderVec3{to_float(scaled.x), to_float(scaled.y), to_float(scaled.z)};
}

float RenderTransform::radius_to_render(double radius_metres) const {
    return to_float(radius_metres * scale_ * exaggeration_);
}

math::Vec3 RenderTransform::to_absolute(const RenderVec3& render_position) const {
    const math::Vec3 relative{static_cast<double>(render_position.x),
                              static_cast<double>(render_position.y),
                              static_cast<double>(render_position.z)};
    return origin_ + relative / scale_;
}

double RenderTransform::resolution_at(const math::Vec3& absolute_position) const {
    const double distance = (absolute_position - origin_).norm() * scale_;
    if (distance == 0.0) {
        return 0.0;
    }
    // One float ulp at this magnitude, converted back to metres.
    const double ulp = std::abs(distance) * static_cast<double>(std::numeric_limits<float>::epsilon());
    return ulp / scale_;
}

bool RenderTransform::should_recenter(const math::Vec3& camera_absolute,
                                      double threshold_metres) const {
    if (!(threshold_metres > 0.0)) {
        throw std::invalid_argument("RenderTransform: recentre threshold must be > 0");
    }
    return (camera_absolute - origin_).norm() > threshold_metres;
}

std::string RenderTransform::describe() const {
    std::ostringstream os;
    os << std::setprecision(8);
    os << "origin " << origin_ << " m, scale " << scale_ << " units/m";
    if (exaggeration_ != 1.0) {
        os << ", body scale exaggerated " << exaggeration_ << "x";
    }
    return os.str();
}

}  // namespace sf::render
