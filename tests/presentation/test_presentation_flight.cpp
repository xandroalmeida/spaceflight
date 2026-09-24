// The orchestrator, flown without a window (rule 62).
//
// The Godot scene could only be exercised by running the engine. FlightApp is a
// plain object: it can be built, fed key presses and frames, and asked what it
// would draw -- which is what this file does. It covers what the two ported
// suites do not: the wiring between the keys, the panels, the cockpit buttons,
// the instruments and the session, and the orientation chain from the kernels
// to the texture.

#include "app/presentation/audio_director.hpp"
#include "app/presentation/canvas.hpp"
#include "app/presentation/controls_doc.hpp"
#include "app/presentation/flight_app.hpp"
#include "app/presentation/format.hpp"
#include "app/presentation/instruments/displays.hpp"
#include "app/presentation/scene/celestial_view.hpp"
#include "app/presentation/scene/planet_textures.hpp"
#include "app/presentation/ui/debug_hud.hpp"
#include "core/ephemeris/spice_kernel_set.hpp"
#include "core/render/star_catalog.hpp"
#include "core/units/constants.hpp"
#include "tests/support/test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <set>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

using namespace sf;
using app::Vec3;

namespace {

constexpr double kPi = std::numbers::pi;

class NoKeys final : public app::KeyboardState {
public:
    [[nodiscard]] bool is_down(app::Key key) const override { return down.count(key) > 0; }
    std::set<app::Key> down;
};

// Records what was asked of the speakers, and plays nothing.
class RecordingAudio final : public app::AudioSink {
public:
    void set_loop(const std::string& clip, double level, double) override { loops[clip] = level; }
    void play(const std::string& clip, double, double) override { played.push_back(clip); }
    std::map<std::string, double> loops;
    std::vector<std::string> played;
};

struct Flight {
    RecordingAudio audio;
    app::FlightApp app{audio};
    NoKeys keys;

    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            app.frame(1.0 / 60.0, keys);
        }
    }
    void press(app::Key key, bool shift = false, bool alt = false) {
        app::KeyEvent event{key, shift, false, alt};
        app.key_event(event);
    }
};

std::unique_ptr<Flight> make_flight() {
    auto flight = std::make_unique<Flight>();
    app::FlightConfig config{};
    config.kernel_directory = ephemeris::SpiceKernelSet::default_directory();
    config.catalogue_path = render::StarCatalog::default_path();
    if (!flight->app.initialise(config)) {
        throw sft::TestSkipped{"no SPICE kernels (" + flight->app.error() + ")"};
    }
    return flight;
}

bool has_message(app::FlightApp& app, const std::string& needle) {
    for (const auto& entry : app.messages().history()) {
        if (entry.text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(a_new_flight_starts_in_the_cockpit_with_the_earth_outside_rule_66) {
    auto flight = make_flight();
    flight->frames(3);
    auto& app = flight->app;
    CHECK(app.camera().is_cockpit());
    CHECK(app.cockpit().visible);
    CHECK(app.instrument_data().present);
    CHECK(has_message(app, "NEW FLIGHT"));
    CHECK(has_message(app, "F1 for controls"));

    // The Earth is IN the window on the first frame: the nose starts nadir-biased,
    // and the cockpit camera rests eleven degrees below the nose.
    const int earth = app.celestial().index_of("Earth");
    REQUIRE(earth >= 0);
    const auto& camera = app.camera().world_camera();
    const Vec3 to_earth = (app.bodies()[static_cast<std::size_t>(earth)].position - camera.position).normalized();
    const double off_axis = math::angle_between(camera.forward(), to_earth) * 180.0 / kPi;
    const double radius_angle =
        std::asin(std::min(1.0, app.bodies()[static_cast<std::size_t>(earth)].radius /
                                    (app.bodies()[static_cast<std::size_t>(earth)].position - camera.position).norm())) *
        180.0 / kPi;
    INFO(app::fmt::format("the Earth's centre is %.1f deg off the line of sight, its disc %.1f deg in radius",
                          off_axis, radius_angle));
    // Within the vertical half-field (37.5 degrees) plus the disc's own radius.
    CHECK(off_axis - radius_angle < 37.5);

    // The ventilation is on inside, and the ship's sounds belong to the hull.
    CHECK(flight->audio.loops["ventilation"] > 0.0);
}

TEST(keys_reach_the_ship_through_the_same_paths_as_the_buttons) {
    auto flight = make_flight();
    auto& app = flight->app;
    flight->frames(2);

    flight->press(app::Key::P);
    CHECK_EQ(app.session().pointing_mode(), std::string{"PROGRADE"});
    flight->press(app::Key::P, true);
    CHECK_EQ(app.session().pointing_mode(), std::string{"RETROGRADE"});
    flight->press(app::Key::Num0);
    CHECK_EQ(app.session().pointing_mode(), std::string{"HOLD"});

    // The throttle: Z opens it, X closes it, and the engine's state is the
    // session's.
    flight->press(app::Key::Z);
    CHECK_EQ(app.session().throttle(), 1.0);
    CHECK(has_message(app, "MAIN ENGINE IGNITION"));
    flight->press(app::Key::X);
    CHECK_EQ(app.session().throttle(), 0.0);

    // The ENGINE button on the panel does exactly what the key does.
    app.cockpit().controls()[0].press();
    CHECK_EQ(app.session().throttle(), 1.0);
    app.cockpit().controls()[0].press();
    CHECK_EQ(app.session().throttle(), 0.0);

    // V switches the RCS off, the RCS switch on the panel follows, and pointing is
    // then refused out loud.
    CHECK(app.cockpit().controls()[1].on);
    flight->press(app::Key::V);
    CHECK(!app.controls().rcs_enabled());
    CHECK(!app.cockpit().controls()[1].on);
    flight->press(app::Key::P);
    CHECK(has_message(app, "RCS DISABLED"));
    flight->press(app::Key::V);
    CHECK(app.controls().rcs_enabled());

    // Warp is never changed in silence (rule 28).
    flight->press(app::Key::Period);
    CHECK_EQ(app.controls().warp(), 10.0);
    CHECK(has_message(app, "TIME WARP 10x"));
    flight->press(app::Key::Comma);
    CHECK_EQ(app.controls().warp(), 1.0);

    // Holding Shift alone trims the throttle -- after 0.2 s, and not when Shift
    // was part of a chord. (A frame with nothing held first: the Shift+P above
    // blocked the trim until the modifier is seen released.)
    flight->frames(1);
    flight->keys.down.insert(app::Key::Shift);
    flight->frames(6);   // 0.1 s: not yet
    CHECK_EQ(app.session().throttle(), 0.0);
    flight->frames(30);  // 0.5 s more
    CHECK(app.session().throttle() > 0.05);
    flight->keys.down.clear();
    flight->frames(1);
    flight->press(app::Key::X);
    flight->keys.down.insert(app::Key::Shift);
    flight->press(app::Key::P, true);   // Shift+P: retrograde, a chord
    flight->frames(40);
    CHECK_EQ(app.session().throttle(), 0.0);
    flight->keys.down.clear();
}

TEST(panels_are_exclusive_and_escape_closes_them_in_order) {
    auto flight = make_flight();
    auto& app = flight->app;
    flight->frames(2);
    using Panel = app::FlightApp::Panel;

    flight->press(app::Key::Tab);
    CHECK(app.panel_visible(Panel::Mission));
    flight->press(app::Key::F1);
    CHECK(app.panel_visible(Panel::Help));
    CHECK(!app.panel_visible(Panel::Mission));   // opening one closes the others
    flight->press(app::Key::Escape);
    CHECK(!app.panel_visible(Panel::Help));
    flight->press(app::Key::Escape);             // nothing open: escape pauses
    CHECK(app.controls().paused());
    CHECK(app.panel_visible(Panel::Pause));
    const double before = app.session().snapshot().elapsed_s;
    flight->frames(10);
    CHECK_EQ(app.session().snapshot().elapsed_s, before);   // paused: simulation time stands still
    flight->press(app::Key::Space);
    CHECK(!app.controls().paused());

    flight->press(app::Key::M);
    CHECK(app.panel_visible(Panel::Map));
    CHECK(!app.hud_visible());   // the map covers the HUD
    flight->press(app::Key::M, true);
    CHECK(app.orbit_map().mode == app::OrbitMap::Mode::System);
    CHECK(app.orbit_map().system.valid);
    CHECK(!app.orbit_map().system_paths.empty());
    flight->press(app::Key::M);
    CHECK(!app.panel_visible(Panel::Map));
}

TEST(the_mission_computer_points_the_ship_in_the_orbital_frame) {
    auto flight = make_flight();
    auto& app = flight->app;
    flight->frames(2);
    auto& panel = app.mission_panel();
    flight->press(app::Key::Tab);
    // Every button reaches the controller, and the mode read back is the one
    // asked for: the same path as the keys, through FlightControls::point.
    for (const auto& command : app::MissionPanel::ATTITUDE_COMMANDS) {
        panel.request_pointing(command.mode);
        flight->frames(1);
        const std::string expected = std::string{command.mode}.empty() ? std::string{"HOLD"}
                                                                         : app::fmt::upper(command.mode);
        CHECK_EQ(app.session().pointing_mode(), expected);
    }
}

TEST(the_mission_computer_searches_arms_and_aborts) {
    auto flight = make_flight();
    auto& app = flight->app;
    flight->frames(2);
    auto& panel = app.mission_panel();

    flight->press(app::Key::Tab);
    CHECK_EQ(panel.current_target(), std::string{"Moon"});
    CHECK_EQ(panel.orbit_label(), std::string{"100 km circular"});
    panel.request_plan();
    CHECK(panel.searching());
    CHECK_EQ(panel.plan_button_text(), std::string{"CANCEL SEARCH"});
    // The request goes out on the NEXT frame, after the screen said so.
    CHECK(!app.session().is_planning());
    flight->frames(1);
    CHECK(app.session().is_planning());

    int frames = 0;
    while (app.session().is_planning() && frames < 60 * 600) {
        flight->frames(1);
        ++frames;
    }
    flight->frames(1);
    REQUIRE(app.session().has_planned_transfer());
    CHECK(!panel.searching());
    CHECK(panel.execute_enabled());
    CHECK(!panel.summary().empty());
    CHECK(has_message(app, "TRANSFER PLANNED"));

    // EXECUTE arms; the phase is announced by the core and relayed.
    panel.on_execute();
    CHECK(app.session().plan().armed);
    // The burn list is resampled four times a second, not every frame.
    flight->frames(20);
    CHECK(app.instrument_data().plan_seconds_to_arrival > 0.0);
    CHECK(!app.instrument_data().next_event.empty());

    // The pilot may not steer while the computer does.
    flight->press(app::Key::P);
    // K with Shift abandons the plan; the ship stays where it is (rule 68).
    flight->press(app::Key::K, true);
    CHECK(!app.session().has_plan());
    CHECK(has_message(app, "MISSION ABORTED"));
}

TEST(the_cockpit_buttons_are_hit_through_the_camera) {
    auto flight = make_flight();
    auto& app = flight->app;
    app.set_viewport_size(app::Vec2{1600.0, 900.0});
    flight->frames(2);
    // Aim the ray at the NAV key by projecting its centre with the near camera.
    const auto& camera = app.camera().near_camera();
    const auto& nav = app.cockpit().controls()[3];
    const Vec3 world = app.ship_basis() * nav.transform.origin;
    const Vec3 local = camera.basis.transposed() * (world - camera.position);
    const double tan_half = std::tan(camera.fov_deg * kPi / 360.0);
    const double x = (local.x / (-local.z * tan_half * (1600.0 / 900.0)) * 0.5 + 0.5) * 1600.0;
    const double y = (0.5 - local.y / (-local.z * tan_half) * 0.5) * 900.0;
    INFO(app::fmt::format("NAV key projects to (%.0f, %.0f)", x, y));
    CHECK(local.z < 0.0);
    CHECK(x > 0.0 && x < 1600.0 && y > 0.0 && y < 900.0);

    app.mouse_motion(app::Vec2{}, app::Vec2{x, y}, false);
    CHECK_EQ(app.cockpit().hovered(), 3);
    app.mouse_button(app::MouseButton::Left, true, app::Vec2{x, y});
    CHECK(app.panel_visible(app::FlightApp::Panel::Mission));
}

TEST(the_instruments_draw_the_frames_numbers) {
    auto flight = make_flight();
    auto& app = flight->app;
    flight->frames(20);
    for (const char* name : {"flight", "nav", "target", "system"}) {
        app::RecordingCanvas canvas;
        app.draw_display(name, canvas, name == std::string{"system"} ? app::Vec2{1280.0, 122.0} : app::Vec2{480.0, 315.0});
        INFO(std::string{name} + " draws");
        CHECK(!canvas.texts.empty());
    }
    app::RecordingCanvas nav;
    app.draw_display("nav", nav, app::Vec2{480.0, 315.0});
    CHECK(nav.contains_text("EARTH"));
    CHECK(nav.contains_text(app::fmt::distance(app.session().snapshot().altitude_m)));
    CHECK(nav.strokes.size() > 100);   // the orbit is drawn, segment by segment

    app::RecordingCanvas hud;
    app.draw_hud(hud, app::Vec2{1600.0, 900.0});
    CHECK(hud.contains_text("TARGET MOON"));

    // The technical read-out keeps every section.
    const auto lines = app::debug_hud::hud_lines(app, false);
    const auto contains = [&](const std::string& needle) {
        return std::any_of(lines.begin(), lines.end(), [&](const auto& l) { return l.find(needle) != std::string::npos; });
    };
    CHECK(contains("t (TDB)"));
    CHECK(contains("forward cone"));
    CHECK(contains("Dormand-Prince"));
    CHECK(contains("(ATITUDE:"));
    CHECK(app::debug_hud::two_columns(lines).size() < lines.size());
}

TEST(the_headless_run_slews_burns_and_raises_the_apoapsis) {
    // The Milestone 2/5 verification without a screen, now without an engine
    // either: point prograde, burn, raise the apoapsis while the propellant
    // falls.
    auto flight = std::make_unique<Flight>();
    app::FlightConfig config{};
    config.kernel_directory = ephemeris::SpiceKernelSet::default_directory();
    config.catalogue_path = render::StarCatalog::default_path();
    config.headless = true;
    if (!flight->app.initialise(config)) {
        throw sft::TestSkipped{"no SPICE kernels"};
    }
    const auto start = flight->app.session().snapshot();
    // Four hundred frames: the slew at 10x, then the burn at 100x. Past that the
    // script goes to 1e7x with the engine lit and the attitude loop closed, which
    // is expensive to integrate and proves nothing more here.
    flight->frames(400);
    const auto end = flight->app.session().snapshot();
    CHECK(end.propellant_kg < start.propellant_kg);
    CHECK(end.apoapsis_m > start.apoapsis_m + 1.0e5);
    CHECK_EQ(end.pointing_mode, std::string{"PROGRADE"});
}

TEST(the_body_orientation_puts_the_sub_solar_point_where_the_calendar_does) {
    // The EXTERNAL test of the orientation: at 00:00 UTC solar noon is on the
    // antimeridian, because noon at Greenwich is at 12:00 UTC; and in January the
    // Sun is over the southern tropic. If the matrix were transposed, the answer
    // would come out near 0 instead of near 180.
    app::FlightSession sim;
    sim.set_diagnostic_sink([](const std::string&, bool) {});
    if (!sim.configure(ephemeris::SpiceKernelSet::default_directory(), "2026-01-01T00:00:00")) {
        throw sft::TestSkipped{"no SPICE kernels"};
    }
    sim.start_circular_orbit(400000.0, 51.6);
    sim.set_render_scale(1.0e-6);
    sim.advance(0.016);

    const int earth = sim.body_index("Earth");
    const int sun = sim.body_index("Sun");
    const Vec3 to_sun = (app::widen(sim.body_position(sun)) - app::widen(sim.body_position(earth))).normalized();
    const auto axes = sim.body_orientation(earth);
    // The columns are the body's axes in J2000, so their dot products take a J2000
    // vector into the body frame.
    const Vec3 body{dot(axes.x, to_sun), dot(axes.y, to_sun), dot(axes.z, to_sun)};
    const double longitude = std::atan2(body.y, body.x) * 180.0 / kPi;
    const double latitude = std::asin(std::clamp(body.z, -1.0, 1.0)) * 180.0 / kPi;
    INFO(app::fmt::format("sub-solar longitude %+.2f east, latitude %+.2f", longitude, latitude));
    CHECK(std::abs(std::abs(longitude) - 180.0) < 3.0);
    CHECK_NEAR_ABS(latitude, -23.0, 1.0, "January: the Sun over the southern tropic");

    // And the whole chain, including the mesh's UV convention: where on the
    // TEXTURE does the sub-solar point fall? At 00:00 UTC in the Pacific -- dark
    // blue -- and the anti-solar point in Africa.
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::string path = std::string{SPACEFLIGHT_SOURCE_DIR} + "/assets/textures/earth/earth_albedo.jpg";
    unsigned char* albedo = stbi_load(path.c_str(), &width, &height, &channels, 3);
    REQUIRE(albedo != nullptr);
    const app::Basis mesh = app::CelestialView::mesh_basis(axes);
    const auto sample = [&](const Vec3& direction) {
        const Vec3 local = mesh.transposed() * direction;
        double u = std::atan2(local.x, local.z) / (2.0 * kPi);
        u -= std::floor(u);
        const double vv = std::acos(std::clamp(local.y, -1.0, 1.0)) / kPi;
        const int px = std::clamp(static_cast<int>(u * width), 0, width - 1);
        const int py = std::clamp(static_cast<int>(vv * height), 0, height - 1);
        const unsigned char* p = albedo + (py * width + px) * 3;
        return Vec3{p[0] / 255.0, p[1] / 255.0, p[2] / 255.0};
    };
    const auto ocean = [](const Vec3& rgb) { return rgb.z > rgb.x + 0.07 && rgb.z > rgb.y + 0.03 && rgb.z < 0.6; };
    const Vec3 sub_solar = sample(to_sun);
    const Vec3 anti_solar = sample(to_sun * -1.0);
    INFO(app::fmt::format("sub-solar rgb %.2f %.2f %.2f, anti-solar rgb %.2f %.2f %.2f", sub_solar.x, sub_solar.y,
                          sub_solar.z, anti_solar.x, anti_solar.y, anti_solar.z));
    CHECK(ocean(sub_solar));
    CHECK(!ocean(anti_solar));
    stbi_image_free(albedo);
}

TEST(the_controls_document_comes_from_the_table_rule_75) {
    const std::string md = app::controls_doc::markdown();
    const std::string json = app::controls_doc::json();
    for (const auto& binding : app::input::bindings()) {
        INFO(std::string{"documented: "} + binding.action);
        CHECK(json.find(std::string{"\""} + binding.action + "\": {") != std::string::npos);
        CHECK(md.find("| `" + app::input::label(binding.action) + "` | " + binding.description + " |") !=
              std::string::npos);
    }
    // The committed document IS what the table generates: a key changed in the
    // table and not re-dumped fails here, not in a player's hands.
    const auto read = [](const std::string& relative) {
        std::FILE* file = std::fopen((std::string{SPACEFLIGHT_SOURCE_DIR} + "/" + relative).c_str(), "rb");
        std::string text;
        if (file != nullptr) {
            char buffer[4096];
            std::size_t n = 0;
            while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
                text.append(buffer, n);
            }
            std::fclose(file);
        }
        return text;
    };
    CHECK(read("docs/gameplay/controls.json") == json);
    CHECK(read("docs/gameplay/controls.md") == md);
}

TEST(the_mars_placeholder_is_mars_coloured) {
    const auto image = app::planet_textures::mars_albedo();
    REQUIRE(image.width == app::planet_textures::WIDTH && image.height == app::planet_textures::HEIGHT);
    // Iron oxide: red above green above blue, on average, and polar caps brighter
    // than the equator.
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        r += image.rgba[i];
        g += image.rgba[i + 1];
        b += image.rgba[i + 2];
    }
    CHECK(r > g && g > b);
    const auto brightness = [&](int y) {
        double sum = 0.0;
        for (int x = 0; x < image.width; ++x) {
            const auto* p = &image.rgba[static_cast<std::size_t>((y * image.width + x) * 4)];
            sum += p[0] + p[1] + p[2];
        }
        return sum;
    };
    CHECK(brightness(1) > brightness(image.height / 2));
}

TEST(each_engine_mode_has_its_own_sound_and_the_ship_hums_inside_rule_36) {
    auto flight = make_flight();
    auto& app = flight->app;
    auto& audio = flight->audio;
    flight->frames(2);

    // Before ignition: the hull hums, the engines are silent.
    CHECK(audio.loops["ventilation"] > 0.0);
    CHECK(audio.loops["equipment"] > 0.0);
    CHECK_EQ(audio.loops["engine_impulse"], 0.0);
    CHECK_EQ(audio.loops["engine_cruise"], 0.0);

    // IMPULSE at full throttle: its sound, not the other one.
    CHECK_EQ(app.session().engine_mode(), std::string{"IMPULSE"});
    flight->press(app::Key::Z);
    flight->frames(60);
    CHECK(audio.loops["engine_impulse"] > 0.5);
    CHECK(audio.loops["engine_cruise"] < 1e-6);

    // CRUISE with the engine lit: 11 kN is scaled by CRUISE's full thrust, so it
    // is heard, and IMPULSE fades rather than cutting.
    flight->press(app::Key::G);
    CHECK_EQ(app.session().engine_mode(), std::string{"CRUISE"});
    flight->frames(1);
    const double impulse_one_frame_later = audio.loops["engine_impulse"];
    CHECK(impulse_one_frame_later > 0.3);
    flight->frames(60);
    CHECK(audio.loops["engine_cruise"] > 0.3);
    CHECK(audio.loops["engine_impulse"] < 0.01);

    // The cut-off follows the thrust down.
    flight->press(app::Key::X);
    flight->frames(60);
    CHECK(audio.loops["engine_cruise"] < 0.01);

    // The RCS hisses for as long as a nozzle is open, and stops after.
    CHECK(audio.loops["rcs_hiss"] < 1e-6);
    flight->keys.down.insert(app::Key::W);
    flight->frames(20);
    CHECK(audio.loops["rcs_hiss"] > 0.05);
    flight->keys.down.clear();
    flight->frames(40);
    CHECK(audio.loops["rcs_hiss"] < 0.01);

    // Equipment beeps now and then -- sporadic, not a rhythm: in three minutes
    // at least two, and never closer together than the minimum interval.
    audio.played.clear();
    flight->frames(3 * 60 * 60);
    const auto beeps = std::count_if(audio.played.begin(), audio.played.end(),
                                     [](const std::string& clip) { return clip.rfind("beep_", 0) == 0; });
    INFO(app::fmt::format("%d beeps in 180 s", static_cast<int>(beeps)));
    CHECK(beeps >= 2);
    CHECK(beeps <= static_cast<long>(180.0 / app::AudioDirector::BEEP_INTERVAL_MIN_S) + 1);

    // Outside, nothing of the ship is heard: no hum, no beeps.
    flight->press(app::Key::C);
    CHECK(!app.camera().is_cockpit());
    flight->frames(2);
    CHECK_EQ(audio.loops["ventilation"], 0.0);
    CHECK_EQ(audio.loops["equipment"], 0.0);
    audio.played.clear();
    flight->frames(3 * 60 * 60);
    CHECK(std::none_of(audio.played.begin(), audio.played.end(),
                       [](const std::string& clip) { return clip.rfind("beep_", 0) == 0; }));
}

TEST(the_accelerometer_reads_what_the_crew_feels_not_gravity) {
    auto flight = make_flight();
    auto& app = flight->app;
    flight->frames(2);
    const auto proper = [&] { return app.session().proper_acceleration_body().norm(); };

    // In orbit with the engine off the ship is in free fall: the coordinate
    // acceleration is gravity's ~8.7 m/s^2, and the crew feels nothing.
    CHECK(app.session().snapshot().acceleration_ms2 > 8.0);
    CHECK_EQ(proper(), 0.0);
    CHECK_EQ(app.instrument_data().proper_acceleration_ms2, 0.0);

    // The engine at full throttle: thrust over mass, along the nose.
    flight->press(app::Key::Z);
    flight->frames(10);
    const auto s = app.session().snapshot();
    const double expected = s.thrust_n / s.mass_kg;
    INFO(app::fmt::format("proper %.9f m/s^2, thrust/mass %.9f m/s^2", proper(), expected));
    CHECK(std::abs(proper() - expected) < 1e-9 * expected);
    CHECK(app.session().proper_acceleration_body().x > 0.0);
    CHECK(expected / units::g0 > 0.5);

    // What the panel shows is that number, in g, in a cell of its own -- and
    // every text on the strip stays inside its column, at the widest reading.
    app::SystemDisplay system;
    app::RecordingCanvas strip;
    const app::Vec2 strip_size{1280.0, 122.0};
    system.draw(strip, strip_size, app.instrument_data());
    CHECK(strip.contains_text("ACCELERATION"));
    CHECK(strip.contains_text(app::fmt::g_load(expected)));
    const double pad = strip_size.y * 0.07;
    const double column = (strip_size.x - pad * 2.0) / app::SystemDisplay::COLUMNS;
    for (const auto& text : strip.texts) {
        const int index = static_cast<int>((text.baseline.x - pad) / column);
        const double right = pad + column * (index + 1);
        const double end = text.baseline.x + strip.text_width(text.text, text.px);
        INFO("'" + text.text + app::fmt::format("' ends at %.0f px, its column at %.0f px", end, right));
        CHECK(end <= right);
    }

    // Outside the cockpit the bottom strip carries it, next to the thrust, and
    // the line does not run into the propellant cell.
    app::MinimalHud hud;
    app::RecordingCanvas screen;
    auto data = app.instrument_data();
    data.cockpit_view = false;
    const app::Vec2 screen_size{1280.0, 720.0};
    hud.draw(screen, screen_size, data);
    bool found = false;
    for (const auto& text : screen.texts) {
        if (text.text.find(app::fmt::g_load(expected)) != std::string::npos) {
            found = true;
            CHECK(text.baseline.x + screen.text_width(text.text, text.px) < screen_size.x * 0.88);
        }
    }
    CHECK(found);

    // Cut-off: back to free fall, and zero again.
    flight->press(app::Key::X);
    flight->frames(2);
    CHECK_EQ(proper(), 0.0);

    // A translation by RCS is felt -- small, but not zero.
    flight->keys.down.insert(app::Key::I);
    flight->frames(10);
    const double translation = proper();
    INFO(app::fmt::format("RCS translation: %.3e m/s^2", translation));
    CHECK(translation > 0.0);
    flight->keys.down.clear();
    flight->frames(10);

    // A pure rotation is not: the RCS fires in couples, whose forces cancel.
    flight->keys.down.insert(app::Key::W);
    flight->frames(10);
    const auto open = app.session().rcs_throttles();
    CHECK(std::any_of(open.begin(), open.end(), [](double value) { return value > 0.002; }));
    INFO(app::fmt::format("RCS rotation: %.3e m/s^2", proper()));
    CHECK(proper() < 1e-9);
    flight->keys.down.clear();
}
