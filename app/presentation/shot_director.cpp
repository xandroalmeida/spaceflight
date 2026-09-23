#include "app/presentation/shot_director.hpp"

#include "app/presentation/flight_app.hpp"
#include "app/presentation/format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <numbers>

namespace sf::app {
namespace {

constexpr double kPi = std::numbers::pi;

// Perto o bastante do alvo para a queima de captura ser o assunto da imagem.
// The Hill sphere is 61 500 km and orbit_about_target() opens there -- the right
// criterion for "there is an orbit to report" and the wrong one for "this is the
// capture": the first version photographed the capture 360 000 km from the Earth.
constexpr double CAPTURE_RANGE_M = 2.0e7;

bool phase_in(const std::string& phase, std::initializer_list<const char*> phases) {
    return std::any_of(phases.begin(), phases.end(), [&](const char* p) { return phase == p; });
}

}  // namespace

ShotDirector::ShotDirector(FlightApp& flight, std::string directory, Script script, int stop_after)
    : flight_(flight), directory_(std::move(directory)) {
    std::filesystem::create_directories(directory_);
    steps_ = script == Script::M8 ? m8_script() : m7_script();
    if (stop_after > 0 && static_cast<std::size_t>(stop_after) < steps_.size()) {
        steps_.resize(static_cast<std::size_t>(stop_after));
    }
}

void ShotDirector::set_warp(int index) { flight_.controls().set_warp_index(index); }

std::vector<ShotDirector::Step> ShotDirector::m7_script() {
    auto& f = flight_;
    std::vector<Step> steps;
    steps.push_back(Step{"cockpit in Earth orbit",
                         [&f] {
                             f.camera().set_mode(CameraRig::Mode::Cockpit);
                             f.controls().set_throttle(0.0);
                         },
                         90, {}, 1200, "cockpit-earth-orbit", {}});
    steps.push_back(Step{"cockpit instruments, nose on prograde",
                         [this, &f] {
                             // Warp 10x during the slew: at warp 1 it is more frames
                             // than this sequence has.
                             set_warp(1);
                             f.controls().point("prograde");
                         },
                         std::nullopt, [this] { return pointed(); }, 2400, "cockpit-instruments", {}});
    steps.push_back(Step{"the whole Earth, from far enough to judge it",
                         [&f] {
                             // Rule 31: the Earth has to look like an Earth, and that
                             // is not decided from 400 km, where only a patch of ocean
                             // shows. Three radii away, continents, clouds, terminator
                             // and night side are all in frame.
                             f.camera().focus_index = f.celestial().index_of("Earth");
                             f.camera().orbit_azimuth = 0.9;
                             f.camera().orbit_elevation = 0.25;
                             f.camera().orbit_zoom = 1.0;
                         },
                         40, {}, 1200, "earth-whole-disc", {}});
    steps.push_back(Step{"the whole Moon, where the relief is judgeable",
                         [&f] {
                             // The terminator is where a normal map shows: grazing
                             // light, craters casting shadow into themselves.
                             f.camera().focus_index = f.celestial().index_of("Moon");
                             f.camera().orbit_azimuth = 0.6;
                             f.camera().orbit_elevation = 0.18;
                             f.camera().orbit_zoom = 1.0;
                         },
                         40, {}, 1200, "moon-whole-disc", {}});
    steps.push_back(Step{"back to the ship", [&f] { f.camera().focus_index = -1; }, 5, {}, 1200, "", {}});
    steps.push_back(Step{"external view of the spacecraft",
                         [this, &f] {
                             f.camera().set_mode(CameraRig::Mode::ExternalOrbit);
                             // 0.7 and not 1.15: this is the figure the manual labels
                             // piece by piece.
                             f.camera().orbit_zoom = 0.7;
                             frame_sunlit(0.42);
                         },
                         40, {}, 1200, "external-spacecraft", [this] { return ship_anchors(); }});
    steps.push_back(Step{"main engine running",
                         [&f] {
                             f.controls().set_throttle(1.0);
                             // Back off: the ship is 22 m and the full plume 14 more.
                             f.camera().orbit_zoom = 1.25;
                         },
                         std::nullopt, [this] { return engine_running(); }, 120, "engine-plume", {}});
    steps.push_back(Step{"RCS firing",
                         [&f] {
                             f.controls().set_throttle(0.0);
                             // As close as the cabin allows: at 46 m a 1.35 m jet is
                             // three pixels; at 13 m WHICH nozzle is open shows.
                             f.camera().orbit_zoom = 0.38;
                             // A new pointing command makes the allocator open
                             // nozzles, and open nozzles are what the image must show
                             // -- not a pressed key (rule 15).
                             f.controls().point("normal");
                         },
                         std::nullopt, [this] { return rcs_firing(); }, 300, "rcs-firing",
                         [this] { return lit_thruster_anchor(); }});
    steps.push_back(Step{"back out for the rest", [&f] { f.camera().orbit_zoom = 1.15; }, 5, {}, 1200, "", {}});
    steps.push_back(Step{"navigation display and orbit map",
                         [&f] {
                             f.camera().set_mode(CameraRig::Mode::Cockpit);
                             f.show_panel(FlightApp::Panel::Map, true);
                         },
                         30, {}, 1200, "orbit-map-earth", {}});
    steps.push_back(Step{"target the Moon",
                         [&f] {
                             f.show_panel(FlightApp::Panel::Map, false);
                             f.set_target("Moon");
                             f.show_panel(FlightApp::Panel::Mission, true);
                         },
                         20, {}, 1200, "moon-target", {}});
    steps.push_back(Step{"plan the transfer",
                         [&f] {
                             f.controls().point("");
                             f.plan_mission("Moon");
                         },
                         // Waits for the PLAN, not a number of frames.
                         std::nullopt, [this] { return plan_ready(); }, 40000, "mission-plan", {}});
    steps.push_back(Step{"execute, and look at the transfer",
                         [this, &f] {
                             armed_ = f.arm_mission();
                             f.show_panel(FlightApp::Panel::Mission, false);
                             f.show_panel(FlightApp::Panel::Map, true);
                             f.orbit_map().zoom = 0.5;
                         },
                         30, {}, 1200, "transfer-map", {}});
    steps.push_back(Step{"coast under warp",
                         [this, &f] {
                             f.show_panel(FlightApp::Panel::Map, false);
                             f.camera().set_mode(CameraRig::Mode::TargetReference);
                             set_warp(5);
                         },
                         std::nullopt, [this] { return within_approach(); }, 6000, "lunar-approach", {}});
    steps.push_back(Step{"capture burn", [this] { set_warp(5); }, std::nullopt,
                         [this] { return close_to_target(); }, 9000, "lunar-capture", {}});
    steps.push_back(Step{"settled lunar orbit, ship against the Moon",
                         [this, &f] {
                             set_warp(3);
                             f.camera().set_mode(CameraRig::Mode::TargetReference);
                         },
                         // AFTER the burn ENDS, and not during.
                         std::nullopt, [this] { return settled_in_orbit(); }, 9000, "lunar-orbit", {}});
    steps.push_back(Step{"the ship in lunar orbit, with the Moon behind it",
                         [this, &f] {
                             set_warp(2);
                             // The TARGET's frame and not the hull's: the camera then
                             // sits opposite the Moon and frames the ship WITH the
                             // Moon behind.
                             f.camera().set_mode(CameraRig::Mode::TargetReference);
                             f.camera().orbit_azimuth = kPi;
                             f.camera().orbit_elevation = 0.34;
                             f.camera().orbit_zoom = 2.4;
                         },
                         // Wait until the ship is over the LIT side: half of a
                         // 118-minute orbit is lunar night.
                         std::nullopt, [this] { return over_sunlit_target(); }, 6000, "lunar-orbit-external", {}});
    return steps;
}

std::vector<ShotDirector::Step> ShotDirector::m8_script() {
    // Milestone 8's script (rule 109): Earth -> Mars, end to end. Separate from
    // the M7 script, because that one is evidence of a closed milestone and has to
    // keep producing the same images.
    auto& f = flight_;
    std::vector<Step> steps;
    steps.push_back(Step{"cockpit in Earth orbit",
                         [&f] {
                             f.camera().set_mode(CameraRig::Mode::Cockpit);
                             f.controls().set_throttle(0.0);
                         },
                         90, {}, 1200, "01-earth-orbit", {}});
    steps.push_back(Step{"mission computer, Mars selected",
                         [&f] {
                             f.set_target("Mars");
                             f.show_panel(FlightApp::Panel::Mission, true);
                         },
                         30, {}, 1200, "02-mission-computer-mars", {}});
    steps.push_back(Step{"searching for a transfer to Mars",
                         [&f] {
                             f.controls().point("");
                             // 500 km circular, Mars's default (rule 57).
                             f.plan_mission("Mars", 500.0, 500.0);
                         },
                         // Photographs the SEARCH running: the panel with the counts
                         // rising is the proof it does not freeze the game.
                         30, {}, 1200, "03-searching", {}});
    steps.push_back(Step{"trajectory options", [] {}, std::nullopt, [this] { return plan_ready(); }, 40000,
                         "04-trajectory-options", {}});
    steps.push_back(Step{"the solar system map, with the transfer on it",
                         [&f] {
                             f.show_panel(FlightApp::Panel::Mission, false);
                             f.show_panel(FlightApp::Panel::Map, true);
                             f.set_map_mode(OrbitMap::Mode::System);
                             f.orbit_map().zoom = 1.0;
                         },
                         40, {}, 1200, "05-solar-system-map", {}});
    steps.push_back(Step{"execute, and the departure burn",
                         [this, &f] {
                             f.show_panel(FlightApp::Panel::Map, false);
                             f.arm_mission();
                             f.camera().set_mode(CameraRig::Mode::ExternalOrbit);
                             f.camera().orbit_zoom = 1.25;
                             set_warp(2);
                         },
                         // The mission PHASE and not the engine: the thrust the
                         // snapshot reports is the MANUAL throttle's, and a burn the
                         // maneuver executor flies does not show there.
                         std::nullopt, [this] { return injection_burning(); }, 24000, "06-departure-burn", {}});
    steps.push_back(Step{"interplanetary cruise",
                         [this, &f] {
                             f.camera().set_mode(CameraRig::Mode::Cockpit);
                             set_warp(7);
                         },
                         // Far enough from the Earth for the reference to be the Sun
                         // already: that switch is what the image has to show.
                         std::nullopt, [this] { return heliocentric(); }, 12000, "07-interplanetary-cruise", {}});
    steps.push_back(Step{"the solar system map, mid-cruise",
                         [this, &f] {
                             // The mode SET, not toggled: step 5 already left the map
                             // in SYSTEM.
                             f.show_panel(FlightApp::Panel::Map, true);
                             f.set_map_mode(OrbitMap::Mode::System);
                             f.orbit_map().zoom = 1.0;
                             // And the warp comes down: forty frames at 1e7x are
                             // seventy-seven DAYS.
                             set_warp(5);
                         },
                         40, {}, 1200, "08-cruise-map", {}});
    // Two steps WITHOUT a photograph, only to brake before the arrival: at 1e6x a
    // frame is 16 700 s, and from Mars's Hill sphere to periapsis is about
    // 56 000 s -- the approach, the three burns and the insertion would all fit
    // between two frames.
    steps.push_back(Step{"first slowdown, still far out",
                         [this, &f] {
                             f.show_panel(FlightApp::Panel::Map, false);
                             set_warp(6);
                         },
                         std::nullopt, [this] { return closer_than(2.0e10); }, 24000, "", {}});
    steps.push_back(Step{"second slowdown, near the encounter", [this] { set_warp(4); }, std::nullopt,
                         [this] { return closer_than(1.0e9); }, 24000, "", {}});
    steps.push_back(Step{"Mars approach",
                         [this, &f] {
                             f.camera().set_mode(CameraRig::Mode::TargetReference);
                             f.show_panel(FlightApp::Panel::Map, false);
                             // Within a million kilometres already: 1e3x, so the Hill
                             // sphere is not crossed in one frame.
                             set_warp(3);
                         },
                         std::nullopt, [this] { return within_approach(); }, 24000, "09-mars-approach", {}});
    steps.push_back(Step{"capture burn", [this] { set_warp(3); }, std::nullopt,
                         [this] { return close_to_target(); }, 24000, "10-mars-capture", {}});
    steps.push_back(Step{"settled Mars orbit",
                         [this, &f] {
                             set_warp(3);
                             f.camera().set_mode(CameraRig::Mode::TargetReference);
                         },
                         // The PHASE, and not the geometry: in a TWO-burn capture a
                         // closed orbit with the engine off is also true between
                         // them. COMPLETE is the only unambiguous reading.
                         std::nullopt, [this] { return mission_complete(); }, 24000, "11-mars-orbit", {}});
    steps.push_back(Step{"the ship in Mars orbit, with Mars behind it",
                         [this, &f] {
                             set_warp(2);
                             f.camera().set_mode(CameraRig::Mode::TargetReference);
                             f.camera().orbit_azimuth = kPi;
                             f.camera().orbit_elevation = 0.34;
                             f.camera().orbit_zoom = 2.4;
                         },
                         std::nullopt, [this] { return over_sunlit_target(); }, 12000, "12-mars-orbit-external",
                         {}});
    return steps;
}

bool ShotDirector::closer_than(double metres) const {
    const double distance = flight_.session().snapshot().target_distance_m;
    return distance > 0.0 && distance < metres;
}

bool ShotDirector::mission_complete() const { return flight_.session().mission_phase() == "COMPLETE"; }

bool ShotDirector::injection_burning() const {
    return phase_in(flight_.session().mission_phase(), {"INJECTION_BURN", "COAST", "APPROACH"});
}

bool ShotDirector::plan_ready() const {
    return !flight_.session().is_planning() && flight_.session().has_planned_transfer();
}

bool ShotDirector::heliocentric() const { return flight_.session().reference_body() == "Sun"; }

bool ShotDirector::pointed() const { return flight_.session().pointing_error_deg() < 4.0; }

bool ShotDirector::engine_running() const { return flight_.session().snapshot().thrust_n > 0.0; }

bool ShotDirector::rcs_firing() const { return flight_.rcs_firing_count() > 0; }

bool ShotDirector::within_approach() const {
    return phase_in(flight_.session().mission_phase(), {"APPROACH", "CAPTURE", "ORBIT_INSERTION", "COMPLETE"});
}

bool ShotDirector::close_to_target() const {
    if (phase_in(flight_.session().mission_phase(), {"ORBIT_INSERTION", "COMPLETE"})) {
        return true;
    }
    const auto orbit = flight_.session().orbit_about_target();
    return orbit.valid && orbit.distance_m < CAPTURE_RANGE_M;
}

bool ShotDirector::settled_in_orbit() const {
    const auto orbit = flight_.session().orbit_about_target();
    if (!orbit.valid || !orbit.captured || orbit.distance_m >= CAPTURE_RANGE_M) {
        return false;
    }
    // And with the engine off: an orbit that has just closed is still being closed.
    return !flight_.session().plan().burning;
}

bool ShotDirector::over_sunlit_target() const {
    const auto directions = flight_.session().flight_directions();
    if (!directions.sun.has_value()) {
        return true;
    }
    const int index = flight_.celestial().index_of(flight_.session().target_body());
    if (index < 0) {
        return true;
    }
    const auto ship = flight_.session().spacecraft_position();
    const auto body = flight_.session().body_position(index);
    const Vec3 from_target = widen(ship) - widen(body);
    if (from_target.norm() < 1.0e-9) {
        return true;
    }
    return dot(from_target.normalized(), directions.sun->normalized()) > 0.30;
}

void ShotDirector::frame_sunlit(double elevation_bias) {
    // Puts the external camera on the lit side, whatever the time. Framing, not
    // physics: nothing that is seen changes, only where from.
    const auto directions = flight_.session().flight_directions();
    if (!directions.sun.has_value()) {
        return;
    }
    const Vec3 sun = directions.sun->normalized();
    const auto axes = flight_.session().spacecraft_axes();
    const Vec3 forward = axes.x.normalized();
    Vec3 up = axes.z.normalized();
    const Vec3 right = cross(forward, up).normalized();
    up = cross(right, forward).normalized();
    const double along = dot(sun, forward);
    const double across = dot(sun, right);
    const double vertical = std::clamp(dot(sun, up), -1.0, 1.0);
    flight_.camera().orbit_azimuth = std::atan2(across, along);
    // The Sun's elevation, pushed up a little: a little top view shows the
    // radiators, which is half of what makes the ship look like a ship.
    flight_.camera().orbit_elevation = std::clamp(std::asin(vertical) * 0.6 + elevation_bias, -1.3, 1.3);
}

std::map<std::string, Vec3> ShotDirector::ship_anchors() const {
    // The five parts of the ship, in metres in the body frame, from the same
    // constants that draw the hull. What is decided here is only which SIDE to
    // mark for the parts that come in pairs.
    const Basis to_body = flight_.ship_basis().transposed();
    const Vec3 eye = to_body * flight_.camera().near_camera().position;
    const double near_y = std::abs(eye.y) > 1.0e-6 ? (eye.y > 0.0 ? 1.0 : -1.0) : 1.0;
    const double near_z = std::abs(eye.z) > 1.0e-6 ? (eye.z > 0.0 ? 1.0 : -1.0) : 1.0;
    using SV = SpacecraftVisual;
    return {
        {"cockpit", Vec3{SV::NOSE_TIP - 0.7, 0.0, 0.8 * near_z}},
        {"habitat", Vec3{SV::CORE_AFT + 1.0, 0.0, SV::CORE_RADIUS * near_z}},
        {"tanks", Vec3{(SV::TANK_FORWARD + SV::TANK_AFT) * 0.5, SV::TANK_OFFSET * near_y, SV::TANK_RADIUS * 0.6 * near_z}},
        {"radiators", Vec3{-7.6, 0.0, SV::RADIATOR_SPAN * 0.75 * near_z}},
        {"engine", Vec3{SV::ENGINE_EXIT + 1.6, 0.0, SV::BELL_EXIT_RADIUS * 0.7 * near_z}},
    };
}

std::map<std::string, Vec3> ShotDirector::lit_thruster_anchor() const {
    // The open nozzle facing the camera the most, and the tip of its jet. Which
    // one depends on the allocator, not on the script.
    //
    // Two conditions, both learnt the hard way: the arm has to be RADIAL -- four
    // of the twelve thrusters sit on the long axis, inside the hull, and are
    // never seen -- and among the radial ones, the one most turned TOWARDS the
    // camera, because the nearest one can be behind a tank.
    const auto thrusters = flight_.session().rcs_thrusters();
    const auto throttles = flight_.session().rcs_throttles();
    const Basis to_body = flight_.ship_basis().transposed();
    const Vec3 eye = (to_body * flight_.camera().near_camera().position).normalized();
    int best = -1;
    double facing = -1.0;
    for (std::size_t i = 0; i < std::min(thrusters.size(), throttles.size()); ++i) {
        if (throttles[i] <= RcsVisual::THRESHOLD) {
            continue;
        }
        const Vec3 at = thrusters[i].position;
        const Vec3 radial{0.0, at.y, at.z};
        if (radial.norm() <= SpacecraftVisual::CORE_RADIUS) {
            continue;
        }
        const double towards = dot(radial.normalized(), eye);
        if (towards > facing) {
            facing = towards;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) {
        return {};
    }
    const auto& spec = thrusters[static_cast<std::size_t>(best)];
    const Vec3 exhaust = (spec.force_direction * -1.0).normalized();
    return {{"jet", spec.position + exhaust * 0.55}};
}

void ShotDirector::step(double) {
    if (step_ >= steps_.size()) {
        flight_.request_quit(0);
        return;
    }
    const auto& current = steps_[step_];
    if (settle_ == 0) {
        std::printf("[shots] %zu/%zu  %s\n", step_ + 1, steps_.size(), current.name.c_str());
        std::fflush(stdout);
        current.setup();
    }
    settle_ += 1;

    bool ready = false;
    if (current.frames.has_value()) {
        ready = settle_ >= *current.frames;
    } else {
        const bool met = current.until();
        ready = met || settle_ >= current.limit;
        if (settle_ == current.limit && !met) {
            // Photograph anyway and SAY the condition was not met. A sequence that
            // aborted here would leave no image of what actually happened, which
            // is exactly what one wants to see when something goes wrong.
            std::printf("[shots]   condition not met within %d frames -- photographing anyway\n", current.limit);
        }
    }
    if (!ready) {
        hold_ = 0;
        return;
    }
    // The condition holds; now let what it describes reach the screen.
    if (hold_ < SETTLE_FRAMES) {
        hold_ += 1;
        return;
    }
    // A step may exist only to change the state -- give the focus back to the
    // ship, say -- and then there is no photograph to take.
    if (!current.shot.empty()) {
        shoot(current);
    }
    step_ += 1;
    settle_ = 0;
    hold_ = 0;
}

void ShotDirector::shoot(const Step& step) {
    const std::string path = directory_ + "/" + step.shot + ".png";
    if (flight_.on_screenshot) {
        flight_.on_screenshot(path);
    }
    if (step.anchors) {
        write_anchors(step.shot, step.anchors());
    }
    const auto s = flight_.session().snapshot();
    std::printf("[shots]   %s.png   t+%s   alt %s   phase %s\n", step.shot.c_str(), fmt::duration(s.elapsed_s).c_str(),
                fmt::distance(s.altitude_m).c_str(), flight_.session().mission_phase().c_str());
    std::fflush(stdout);
}

void ShotDirector::write_anchors(const std::string& name, const std::map<std::string, Vec3>& anchors) const {
    // Where each part of the ship fell IN THE IMAGE, in percent of the frame. The
    // point is given in the BODY frame and projected by the camera that took the
    // photograph; the manual reads the JSON and knows no pixels.
    if (anchors.empty()) {
        return;
    }
    const auto& camera = flight_.camera().near_camera();
    const Vec2 frame = flight_.viewport_size();
    const Basis to_camera = camera.basis.transposed();
    const double tan_half = std::tan(camera.fov_deg * kPi / 360.0);
    const double aspect = frame.x / frame.y;
    std::string json = "{";
    bool first = true;
    for (const auto& [key, body_point] : anchors) {
        const Vec3 world = flight_.ship_basis() * body_point;
        const Vec3 local = to_camera * (world - camera.position);
        if (local.z >= 0.0) {
            continue;   // behind the camera
        }
        const double x_ndc = local.x / (-local.z * tan_half * aspect);
        const double y_ndc = local.y / (-local.z * tan_half);
        const double x = (x_ndc * 0.5 + 0.5) * 100.0;
        const double y = (0.5 - y_ndc * 0.5) * 100.0;
        json += fmt::format("%s\n  \"%s\": {\n    \"x\": %.1f,\n    \"y\": %.1f\n  }", first ? "" : ",", key.c_str(),
                            std::round(x * 10.0) / 10.0, std::round(y * 10.0) / 10.0);
        first = false;
    }
    json += "\n}\n";
    std::ofstream file(directory_ + "/" + name + ".anchors.json");
    if (!file) {
        std::fprintf(stderr, "[shots]   cannot write anchors for %s\n", name.c_str());
        return;
    }
    file << json;
}

}  // namespace sf::app
