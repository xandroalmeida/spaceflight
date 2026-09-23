#include "app/presentation/camera_rig.hpp"

#include <algorithm>
#include <cmath>

namespace sf::app {
namespace {

constexpr double kPi = std::numbers::pi;

}  // namespace

CameraRig::CameraRig(double render_scale) : render_scale_(render_scale) {
    world_.near = WORLD_NEAR;
    world_.far = WORLD_FAR;
    world_.fov_deg = FOV;
    near_.near = NEAR_NEAR;
    near_.far = NEAR_FAR;
    near_.fov_deg = FOV;
}

bool CameraRig::preset(Mode mode, double& azimuth, double& elevation) const {
    // Where each mode puts the camera when it enters: (azimuth, elevation).
    switch (mode) {
        case Mode::ExternalOrbit: azimuth = kPi; elevation = 0.35; return true;
        case Mode::Chase: azimuth = kPi; elevation = 0.14; return true;
        case Mode::VelocityReference: azimuth = kPi; elevation = 0.0; return true;
        case Mode::TargetReference: azimuth = kPi; elevation = 0.18; return true;
        case Mode::Cockpit: break;
    }
    return false;
}

void CameraRig::set_mode(Mode mode) {
    if (mode == mode_) {
        return;
    }
    mode_ = mode;
    look_yaw = 0.0;
    look_pitch = 0.0;
    (void)preset(mode_, orbit_azimuth, orbit_elevation);
}

void CameraRig::cycle_mode() {
    set_mode(static_cast<Mode>((static_cast<std::size_t>(mode_) + 1) % MODE_NAMES.size()));
}

void CameraRig::update(const Vec3& ship_position, const Basis& ship_basis, const Vec3& beta,
                       const Vec3& target_direction, const Vec3& focus_position, double focus_natural) {
    ship_position_ = ship_position;
    ship_basis_ = ship_basis;
    focus_position_ = focus_position;
    focus_natural_ = focus_natural;

    if (focus_index >= 0) {
        place_orbiting_body();
    } else if (mode_ == Mode::Cockpit) {
        place_cockpit();
    } else {
        place_external(beta, target_direction);
    }
}

void CameraRig::place_cockpit() {
    // The head is fixed in the hull; what moves is where it looks. The ship's
    // attitude is still the core's -- looking around is NOT a manoeuvre and does
    // not change a single number of the state.
    Basis basis = ship_basis_ * COCKPIT_ALIGN;
    basis = basis.rotated(basis.y, look_yaw);
    basis = basis.rotated(basis.x, look_pitch + COCKPIT_REST_PITCH);
    commit(ship_basis_ * EYE, basis.orthonormalized());
}

void CameraRig::place_external(const Vec3& beta, const Vec3& target_direction) {
    const auto frame = orbit_frame(beta, target_direction);
    const Vec3 forward = frame[0];
    Vec3 up = frame[1];
    Vec3 right = cross(forward, up);
    if (right.norm() < 1.0e-6) {
        right = cross(forward, Vec3{0.0, 1.0, 0.0});
        if (right.norm() < 1.0e-6) {
            right = cross(forward, Vec3{1.0, 0.0, 0.0});
        }
    }
    right = right.normalized();
    up = cross(right, forward).normalized();

    const double ce = std::cos(orbit_elevation);
    const double se = std::sin(orbit_elevation);
    const double ca = std::cos(orbit_azimuth);
    const double sa = std::sin(orbit_azimuth);
    const Vec3 offset = forward * (ce * ca) + right * (ce * sa) + up * se;
    // The tangent of the meridian: the derivative of `offset` with respect to
    // the elevation. It is a unit vector and EXACTLY perpendicular to `offset`
    // for every (azimuth, elevation) -- offset.meridian = -ce se + se ce = 0,
    // algebraically. That is what makes it a safe "up" everywhere, poles
    // included, and it is why the guard that swapped the "up" vector is gone
    // rather than re-tuned: the guard WAS the 88-degree one-frame jump this
    // camera used to have.
    const Vec3 meridian = forward * (-se * ca) + right * (-se * sa) + up * ce;

    const double distance = std::max(EXTERNAL_NATURAL_M * orbit_zoom, NEAR_NEAR * 4.0);
    commit(offset * distance, aim(offset * -1.0, meridian));
}

void CameraRig::place_orbiting_body() {
    // The M5 technical view: orbit a body, in scene units. Here the near camera
    // stays where it would be -- millions of metres from the ship -- and the
    // ship simply falls out of its far plane. No special case: the two cameras
    // are still the same camera.
    //
    // ⚠️ `forward` is the direction FROM THE BODY TO THE SHIP, not a fixed axis.
    // The first version used a fixed -Z with the J2000 pole as "up" -- and
    // (0,0,-1) x (0,0,1) is the NULL VECTOR. Normalised it gives zero, the offset
    // gives zero, and the camera sat exactly at the centre of the planet: a black
    // screen, with no error at all. Caught by a whole-planet capture.
    Vec3 forward = ship_position_ - focus_position_;
    if (forward.norm() < 1.0e-9) {
        forward = Vec3{1.0, 0.0, 0.0};
    }
    forward = forward.normalized();
    Vec3 up = stable_up(forward);
    Vec3 right = cross(forward, up);
    if (right.norm() < 1.0e-6) {
        right = cross(forward, Vec3{0.0, 1.0, 0.0});
        if (right.norm() < 1.0e-6) {
            right = cross(forward, Vec3{1.0, 0.0, 0.0});
        }
    }
    right = right.normalized();
    up = cross(right, forward).normalized();

    const double ce = std::cos(orbit_elevation);
    const double se = std::sin(orbit_elevation);
    const Vec3 offset = forward * (ce * std::cos(orbit_azimuth)) + right * (ce * std::sin(orbit_azimuth)) + up * se;
    const Vec3 meridian =
        forward * (-se * std::cos(orbit_azimuth)) + right * (-se * std::sin(orbit_azimuth)) + up * ce;
    const double distance = std::max(focus_natural_ * orbit_zoom, WORLD_NEAR * 3.0);

    const Vec3 eye_scene = focus_position_ + offset * distance;
    const Basis basis = aim(offset * -1.0, meridian);
    world_.position = eye_scene;
    world_.basis = basis;
    near_.position = (eye_scene - ship_position_) / render_scale_;
    near_.basis = basis;
}

std::array<Vec3, 2> CameraRig::orbit_frame(const Vec3& beta, const Vec3& target_direction) const {
    // Returns [forward, up]; the caller builds `right` from them, so that the
    // handedness lives in one place.
    switch (mode_) {
        case Mode::ExternalOrbit:
            // The hull's own axes. Azimuth 180 is exactly behind the tail, 0 is
            // head-on at the nose. It is what is wanted to LOOK AT THE SHIP.
            return {ship_basis_.x.normalized(), ship_basis_.z.normalized()};
        case Mode::TargetReference: {
            Vec3 to_target = target_direction;
            if (to_target.norm() < 0.5) {
                to_target = ship_basis_.x.normalized();
            }
            return {to_target.normalized(), stable_up(to_target.normalized())};
        }
        default: {
            // The velocity frame: `forward` is the axis about which the sky is
            // aberrated.
            Vec3 forward = beta.normalized();
            if (forward.norm() < 0.5) {
                forward = Vec3{0.0, 0.0, -1.0};
            }
            return {forward, stable_up(forward)};
        }
    }
}

Vec3 CameraRig::stable_up(const Vec3& forward) {
    // The J2000 pole, for two reasons.
    //
    // The principle: these modes exist to look at the SKY, and the sky does not
    // rotate. An inertial reference keeps the star field still while the ship
    // goes round.
    //
    // The measurement: the obvious alternative -- outward from the reference
    // body -- is almost PARALLEL to the barycentric velocity in low orbit.
    // Measured |beta.radial| = 0.99, with which the cross product that fixes the
    // azimuth had length 0.13 and wobbled. Against the pole, |beta.z| = 0.128.
    Vec3 up{0.0, 0.0, 1.0};
    if (std::abs(dot(forward, up)) > 0.99) {
        // A trajectory leaving through the celestial pole. Nothing in the Solar
        // System does this, and switching here IS a discontinuity -- left
        // visible rather than smoothed, because pretending it is continuous would
        // be the guard's mistake again.
        up = Vec3{0.0, 1.0, 0.0};
    }
    return up;
}

Basis CameraRig::aim(const Vec3& forward, const Vec3& up) const {
    // `up` has to be perpendicular to `forward`, and the caller guarantees it by
    // construction rather than by checking.
    const Vec3 f = forward.normalized();
    const Vec3 u = up.normalized();
    const Vec3 right = cross(f, u).normalized();
    // The camera looks along -Z.
    Basis basis{right, u, f * -1.0};
    basis = basis.rotated(basis.y, look_yaw);
    basis = basis.rotated(basis.x, look_pitch);
    return basis.orthonormalized();
}

void CameraRig::commit(const Vec3& eye_metres, const Basis& basis) {
    // The only conversion between the project's two scales, and it is here.
    near_.position = eye_metres;
    near_.basis = basis;
    world_.position = ship_position_ + eye_metres * render_scale_;
    world_.basis = basis;
}

bool CameraRig::handle_mouse_button(MouseButton button, bool pressed) {
    switch (button) {
        case MouseButton::Right:
            dragging_ = pressed;
            set_capture(dragging_);
            return true;
        case MouseButton::WheelUp:
            if (pressed && !is_cockpit()) {
                zoom(-1.0);
                return true;
            }
            break;
        case MouseButton::WheelDown:
            if (pressed && !is_cockpit()) {
                zoom(1.0);
                return true;
            }
            break;
        case MouseButton::Left: break;
    }
    return false;
}

bool CameraRig::handle_mouse_motion(double dx, double dy, bool free_look) {
    if (is_cockpit()) {
        // In the cockpit the mouse looks around whenever it is captured, with no
        // button: it is the pilot's head and not an object manipulator.
        if (!(mouse_look_ || dragging_)) {
            return false;
        }
        apply_look(-dx, -dy);
        return true;
    }
    if (!dragging_) {
        return false;
    }
    if (free_look) {
        apply_look(-dx, -dy);
    } else {
        orbit_azimuth -= dx * 0.006 * mouse_sensitivity;
        orbit_elevation += dy * 0.006 * mouse_sensitivity;
        settle();
    }
    return true;
}

void CameraRig::set_mouse_look(bool enabled) {
    mouse_look_ = enabled;
    set_capture(enabled);
}

void CameraRig::set_capture(bool enabled) {
    if (on_capture_changed) {
        on_capture_changed(enabled);
    }
}

void CameraRig::apply_look(double dx, double dy) {
    const double sign_y = invert_look ? -1.0 : 1.0;
    look_yaw += dx * 0.0035 * mouse_sensitivity;
    look_pitch += dy * 0.0035 * mouse_sensitivity * sign_y;
    settle();
}

void CameraRig::apply_keys(double delta, double yaw_axis, double pitch_axis, bool free_look) {
    const double step = 1.4 * delta;
    if (is_cockpit() || free_look) {
        look_yaw += yaw_axis * step;
        look_pitch += pitch_axis * step;
    } else {
        orbit_azimuth += yaw_axis * step;
        orbit_elevation += pitch_axis * step;
    }
    settle();
}

void CameraRig::zoom(double steps) {
    orbit_zoom = std::clamp(orbit_zoom * std::pow(ZOOM_STEP, steps), ZOOM_MIN, ZOOM_MAX);
}

void CameraRig::recentre() {
    look_yaw = 0.0;
    look_pitch = 0.0;
    orbit_zoom = 1.0;
    (void)preset(mode_, orbit_azimuth, orbit_elevation);
}

bool CameraRig::at_preset() const {
    if (is_cockpit()) {
        return std::abs(look_yaw) < 0.02 && std::abs(look_pitch) < 0.02;
    }
    double azimuth = 0.0;
    double elevation = 0.0;
    if (!preset(mode_, azimuth, elevation)) {
        return false;
    }
    return std::abs(wrap_angle(orbit_azimuth - azimuth)) < 0.02 && std::abs(orbit_elevation - elevation) < 0.02 &&
           std::abs(look_yaw) < 0.02 && std::abs(look_pitch) < 0.02;
}

void CameraRig::settle() {
    orbit_azimuth = wrap_angle(orbit_azimuth);
    orbit_elevation = std::clamp(orbit_elevation, -ORBIT_ELEVATION_LIMIT, ORBIT_ELEVATION_LIMIT);
    if (is_cockpit()) {
        look_yaw = std::clamp(look_yaw, -LOOK_YAW_LIMIT, LOOK_YAW_LIMIT);
        look_pitch = std::clamp(look_pitch, -LOOK_PITCH_LIMIT, LOOK_PITCH_LIMIT);
    } else {
        look_yaw = wrap_angle(look_yaw);
        look_pitch = std::clamp(look_pitch, -ORBIT_ELEVATION_LIMIT, ORBIT_ELEVATION_LIMIT);
    }
}

}  // namespace sf::app
