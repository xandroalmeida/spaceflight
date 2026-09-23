// The Milestone 7 presentation tests (rule 62), ported from the suite
// they replaced (the Godot suite test_m7.gd, removed with the engine) check for check.
//
// What they cover is what M7 added and nothing more: the snapshot that reaches
// the cockpit, target selection, unit formatting, camera state, the mapping of
// the RCS jets and the plume, and the trajectory points the map receives. The
// physics is verified by the scientific suite and this file does not repeat a
// single sum of it -- turning M7 into another scientific campaign is exactly
// what rule 62 forbids.
//
// No window. No assertion here looks at a pixel: what an image can decide is in
// tests/gpu and in scripts/m7_screenshots.sh, and it is judged by a person
// (rule 60).

#include "app/presentation/camera_rig.hpp"
#include "app/presentation/canvas.hpp"
#include "app/presentation/format.hpp"
#include "app/presentation/input_actions.hpp"
#include "app/presentation/instruments/displays.hpp"
#include "app/presentation/instruments/nav_display.hpp"
#include "app/presentation/instruments/orbit_map.hpp"
#include "app/presentation/scene/cockpit.hpp"
#include "app/presentation/scene/spacecraft_visual.hpp"
#include "app/session/flight_session.hpp"
#include "core/ephemeris/spice_kernel_set.hpp"
#include "tests/support/test_harness.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <set>
#include <string>

using namespace sf;
using app::Vec3;

namespace {

constexpr double kPi = std::numbers::pi;

// One session for the whole file, configured once and flown in order -- the
// suite's tests follow each other the way the Godot suite's did.
app::FlightSession& session() {
    // Deliberately never destroyed: the session's destructor takes the CSPICE
    // mutex, a function-local static created AFTER this one -- and so destroyed
    // BEFORE it at exit. Leaking the one object is the order that works.
    static app::FlightSession* instance = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        auto* s = new app::FlightSession();
        s->set_diagnostic_sink([](const std::string&, bool) {});
        if (s->configure(ephemeris::SpiceKernelSet::default_directory(), "2026-01-01T00:00:00")) {
            s->start_circular_orbit(400000.0, 51.6);
            s->align_attitude_to_flight(25.0);
            s->set_render_scale(1.0e-6);
            s->advance(0.016);
            instance = s;
        } else {
            delete s;
        }
    }
    if (instance == nullptr) {
        // Without kernels this is not a failure, it is an absence: 77 is Skipped
        // in CTest, and a machine that could not run a suite did not fail it.
        throw sft::TestSkipped{"no SPICE kernels (run scripts/fetch_kernels.sh)"};
    }
    return *instance;
}

Vec3 v(const render::RenderVec3& p) { return app::widen(p); }

double extent(const std::vector<render::RenderVec3>& track, const Vec3& centre) {
    double out = 0.0;
    for (const auto& point : track) {
        out = std::max(out, (v(point) - centre).norm());
    }
    return out;
}

std::vector<Vec3> widen_all(const std::vector<render::RenderVec3>& track) {
    std::vector<Vec3> out;
    for (const auto& p : track) {
        out.push_back(v(p));
    }
    return out;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

TEST(units_rule_74) {
    // What rule 74 explicitly forbids.
    CHECK(app::fmt::distance(3.84e8).find("384000000") == std::string::npos);
    CHECK_EQ(app::fmt::distance(842.0), std::string{"842 m"});
    CHECK_EQ(app::fmt::distance(18400.0), std::string{"18.4 km"});
    CHECK(ends_with(app::fmt::distance(1.496e11), "AU"));
    CHECK_EQ(app::fmt::speed(7680.0), std::string{"7.680 km/s"});
    CHECK(ends_with(app::fmt::speed(3.6e7), "c"));
    CHECK_EQ(app::fmt::speed(842.0), std::string{"842.0 m/s"});
    // SIGNIFICANT digits, not sixteen places.
    CHECK_EQ(app::fmt::sci(4.014566457044566e-13), std::string{"4.015e-13"});
    CHECK_EQ(app::fmt::sci(0.8072), std::string{"0.8072"});
    CHECK_EQ(app::fmt::countdown(6137.0), std::string{"T-01:42:17"});
    // A duration that does not exist says so.
    CHECK_EQ(app::fmt::duration(-1.0), std::string{"--"});
}

TEST(input_map_rule_13) {
    CHECK(app::input::find("pitch_up") != nullptr);
    CHECK(app::input::find("orbit_map") != nullptr);
    CHECK(app::input::find("mission_abort") != nullptr);   // rule 68
    // Rule 13: no keycodes scattered around. What can be tested is that every
    // action of the table reaches the groups the help and the docs are made of,
    // and that the label comes from the table.
    std::size_t count = 0;
    for (const auto& group : app::input::by_group()) {
        count += group.entries.size();
    }
    CHECK_EQ(count, app::input::bindings().size());
    CHECK_EQ(app::input::label("point_retrograde"), std::string{"Shift+P"});

    // Exact modifier matching: P is prograde and Shift+P is NOT also prograde.
    app::KeyEvent p{app::Key::P};
    app::KeyEvent shift_p{app::Key::P, true};
    CHECK(app::input::pressed_exact(p, "point_prograde"));
    CHECK(!app::input::pressed_exact(shift_p, "point_prograde"));
    CHECK(app::input::pressed_exact(shift_p, "point_retrograde"));
    // Every action has a unique (key, modifiers) pair.
    std::set<std::string> seen;
    for (const auto& binding : app::input::bindings()) {
        const std::string key = app::input::label(binding.action);
        CHECK(seen.insert(key).second);
    }
}

TEST(snapshot_reaching_the_cockpit_rule_19) {
    const auto s = session().snapshot();
    REQUIRE(s.valid);
    CHECK_NEAR_ABS(s.altitude_m, 400000.0, 2000.0, "the parking orbit is where it was asked for");
    CHECK_NEAR_ABS(s.inclination_deg, 51.6, 0.01, "inclination survives the round trip");
    CHECK(s.mass_kg > 0.0);
    CHECK(s.propellant_kg > 0.0);
    CHECK(s.speed_ms > 7000.0);
    CHECK(!s.target.empty());
    CHECK(s.beta > 0.0);
    CHECK(s.lorentz_factor_minus_one > 0.0);
}

TEST(target_selection_rule_21) {
    auto& sim = session();
    const auto targets = sim.selectable_targets();
    CHECK(!targets.empty());
    CHECK(std::find(targets.begin(), targets.end(), "Moon") != targets.end());
    // Barycentres are points in the void and not destinations. The catalogue
    // tells them apart by radius, and it is the catalogue that decides -- not a
    // list of names in the interface.
    for (const auto& name : targets) {
        INFO(name + " is a place, not a barycentre");
        CHECK(name.find("Barycenter") == std::string::npos);
    }
    CHECK(sim.set_target_body("Moon"));
    CHECK_EQ(sim.target_body(), std::string{"Moon"});
    sim.advance(0.016);
    CHECK(sim.snapshot().target_distance_m > 3.0e8);
    CHECK(!sim.set_target_body("Vulcan"));
    CHECK(sim.last_error().find("Vulcan") != std::string::npos);
}

TEST(flight_director_markers_rule_18) {
    const auto d = session().flight_directions();
    for (const char* key : {"prograde", "retrograde", "normal", "anti_normal", "radial_out", "radial_in", "nose",
                            "target"}) {
        INFO(std::string{"direction "} + key + " is published");
        CHECK(d.by_name(key).has_value());
    }
    REQUIRE(d.prograde.has_value() && d.retrograde.has_value() && d.normal.has_value() && d.nose.has_value());
    CHECK_NEAR_ABS(d.prograde->norm(), 1.0, 1.0e-5, "prograde is a unit vector");
    CHECK_NEAR_ABS(dot(*d.prograde, *d.retrograde), -1.0, 1.0e-5, "retrograde is exactly the opposite");
    CHECK_NEAR_ABS(dot(*d.prograde, *d.normal), 0.0, 1.0e-5, "normal is perpendicular to the velocity");
    CHECK_NEAR_ABS(dot(*d.radial_out, *d.radial_in), -1.0, 1.0e-5, "radial in and out are opposite");
    // The nadir bias: the nose starts 25 degrees below prograde, and that is what
    // puts the Earth in the window on the first frame (rule 66).
    CHECK_NEAR_ABS(math::angle_between(*d.nose, *d.prograde) * 180.0 / kPi, 25.0, 1.0,
                   "the nose starts 25 degrees below prograde");
}

TEST(trajectory_points_reaching_the_map_rules_54_77) {
    auto& sim = session();
    const auto track = sim.orbit_track(96);
    CHECK_EQ(track.size(), std::size_t{96});

    const auto s = sim.snapshot();
    const Vec3 centre = v(sim.body_position(sim.body_index(s.reference)));
    const double scale = sim.render_scale();
    double near = std::numeric_limits<double>::infinity();
    double far = 0.0;
    for (const auto& point : track) {
        const double r = (v(point) - centre).norm() / scale;
        near = std::min(near, r);
        far = std::max(far, r);
    }
    // The points are of the SAME ellipse the snapshot reports. If the map drew its
    // own orbit, this is where the two would disagree.
    CHECK_NEAR_ABS(near, s.periapsis_m, s.periapsis_m * 1.0e-3, "the track reaches periapsis");
    CHECK_NEAR_ABS(far, s.apoapsis_m, s.apoapsis_m * 1.0e-3, "the track reaches apoapsis");
    CHECK((v(track.front()) - v(track.back())).norm() / scale < 5.0e4);   // a bound orbit closes on itself

    const auto moon = sim.body_orbit_track(sim.body_index("Moon"), 64);
    CHECK_EQ(moon.size(), std::size_t{64});
    double lunar_far = 0.0;
    double lunar_near = std::numeric_limits<double>::infinity();
    for (const auto& point : moon) {
        const double r = (v(point) - centre).norm() / scale;
        lunar_near = std::min(lunar_near, r);
        lunar_far = std::max(lunar_far, r);
    }
    // Sampled from the EPHEMERIS, not from an ellipse: the real lunar perigee and
    // apogee are 363 300 and 405 500 km, and the spread between them is the proof
    // it is not a drawn circle.
    CHECK(lunar_near > 3.4e8 && lunar_far < 4.2e8);
    CHECK(lunar_far - lunar_near > 1.0e7);
}

TEST(a_circular_orbit_has_no_apsis_to_point_at_rule_20) {
    // ⚠️ The orbit's points reach the display in 32-bit float and scene units. In
    // a 400 km parking orbit the radius varies by METRES round the whole lap --
    // of the order of the ulp itself -- and "the farthest point of the sample"
    // is decided by rounding. Measured before the fix: the drawing's axis turned
    // up to 37 degrees from one frame to the next.
    auto& sim = session();
    const int reference = sim.body_index(sim.snapshot().reference);
    const double scale = sim.render_scale();
    const auto spread = [&] {
        const auto s = sim.snapshot();
        return std::abs(s.apoapsis_m - s.periapsis_m);
    };

    Vec3 centre = v(sim.body_position(reference));
    auto track = sim.orbit_track(128);
    CHECK(spread() < 1.0e3);   // the starting orbit is circular to under a km
    CHECK(!app::NavDisplay::apsides_resolved(spread(), extent(track, centre), scale));

    // The drawing's axis has to be the same next frame. It carries the ship and
    // the markers; if it rotates, everything on it jumps.
    double worst = 0.0;
    std::optional<Vec3> previous;
    for (int frame = 0; frame < 20; ++frame) {
        sim.advance(0.016);
        centre = v(sim.body_position(reference));
        track = sim.orbit_track(128);
        const bool resolved = app::NavDisplay::apsides_resolved(spread(), extent(track, centre), scale);
        const Vec3 axis = app::NavDisplay::plane_from(widen_all(track), centre, resolved)[0];
        if (previous.has_value()) {
            worst = std::max(worst, math::angle_between(*previous, axis) * 180.0 / kPi);
        }
        previous = axis;
    }
    CHECK_NEAR_ABS(worst, 0.0, 0.01, "the drawing's axis does not rotate between frames");

    // The middle case, which is what is seen while flying: the perturbations open
    // a few hundred metres between the apsides, the display points at them AGAIN,
    // and that is when the marker has to be still.
    for (int i = 0; i < 120; ++i) {
        sim.advance(0.5);
    }
    centre = v(sim.body_position(reference));
    track = sim.orbit_track(128);
    INFO(app::fmt::format("the perturbations opened the apsides (%.0f m)", spread()));
    CHECK(spread() > 300.0 && spread() < 5.0e3);
    CHECK(app::NavDisplay::apsides_resolved(spread(), extent(track, centre), scale));

    // Both markers live ON the line of apsides, at a radius that comes from the
    // snapshot in double precision -- not from a search of the sample. What can
    // move is the axis, and only the axis.
    worst = 0.0;
    previous.reset();
    double marker = 0.0;
    double previous_marker = std::numeric_limits<double>::infinity();
    for (int frame = 0; frame < 30; ++frame) {
        sim.advance(0.016);
        centre = v(sim.body_position(reference));
        track = sim.orbit_track(128);
        const Vec3 axis = app::NavDisplay::plane_from(widen_all(track), centre, true, true)[0];
        if (previous.has_value()) {
            worst = std::max(worst, math::angle_between(*previous, axis) * 180.0 / kPi);
        }
        previous = axis;
        const double here = sim.snapshot().apoapsis_m * scale / extent(track, centre);
        if (std::isfinite(previous_marker)) {
            marker = std::max(marker, std::abs(here - previous_marker));
        }
        previous_marker = here;
    }
    CHECK_NEAR_ABS(worst, 0.0, 1.5, "the axis stays within a degree and a half per frame");
    CHECK_NEAR_ABS(marker, 0.0, 1.0e-4, "and the marker's radius does not breathe");

    // And the other side: an orbit with a real apsis has to KEEP showing it.
    sim.set_pointing_mode("prograde");
    for (int i = 0; i < 400; ++i) {
        sim.advance(0.5);
    }
    sim.set_throttle(1.0);
    for (int i = 0; i < 600; ++i) {
        sim.advance(0.5);
    }
    sim.set_throttle(0.0);
    sim.set_pointing_mode("");   // no command: the attitude goes back to the pilot

    centre = v(sim.body_position(reference));
    track = sim.orbit_track(128);
    INFO(app::fmt::format("the burn left a frank ellipse (ecc %.3f)", sim.snapshot().eccentricity));
    CHECK(sim.snapshot().eccentricity > 0.5);
    CHECK(app::NavDisplay::apsides_resolved(spread(), extent(track, centre), scale));

    // ⚠️ And it does not jump HERE either. orbit_track samples in true anomaly
    // from -pi to +pi, and with 128 samples the periapsis (nu = 0) falls at index
    // 63.5 -- exactly between two.
    worst = 0.0;
    previous.reset();
    for (int frame = 0; frame < 20; ++frame) {
        sim.advance(0.016);
        centre = v(sim.body_position(reference));
        track = sim.orbit_track(128);
        const Vec3 axis = app::NavDisplay::plane_from(widen_all(track), centre, true)[0];
        if (previous.has_value()) {
            worst = std::max(worst, math::angle_between(*previous, axis) * 180.0 / kPi);
        }
        previous = axis;
    }
    CHECK_NEAR_ABS(worst, 0.0, 0.01, "not even on an ellipse does the axis jump by a sample");

    sim.start_circular_orbit(400000.0, 51.6);
    sim.align_attitude_to_flight(25.0);
    sim.advance(0.016);
}

TEST(the_planned_arc_is_in_the_maps_frame) {
    // The planned arc and the burn markers have to be in the SAME frame as the
    // map's other curves: relative to the origin body, anchored where it is now.
    // Milestone 6.2 returned the arc in absolute coordinates and the map drew a
    // trip to the Moon as a line out to 13.2 million km, against a Moon at 361
    // thousand.
    auto& sim = session();
    const auto plan = sim.plan_transfer("Moon", 100.0, 100.0, 2.0);
    if (!plan.valid) {
        SFT_FAIL("the planner found a transfer (" + sim.last_error() + ")");
        return;
    }
    CHECK_EQ(plan.burns, 2);   // an unarmed plan already reports its burn count
    CHECK(!plan.armed);        // planning does not arm (rule 26)
    CHECK(sim.arm_plan());
    CHECK(sim.plan().armed);

    const double scale = sim.render_scale();
    const Vec3 centre = v(sim.body_position(sim.body_index(sim.snapshot().reference)));
    const double moon_distance = (v(sim.body_position(sim.body_index("Moon"))) - centre).norm() / scale;

    const auto track = sim.planned_trajectory();
    CHECK(track.size() > 16);
    double near = std::numeric_limits<double>::infinity();
    double far = 0.0;
    for (const auto& point : track) {
        const double r = (v(point) - centre).norm() / scale;
        near = std::min(near, r);
        far = std::max(far, r);
    }
    CHECK_NEAR_ABS(near, 6.771e6, 1.0e5, "the arc starts in the parking orbit");
    INFO(app::fmt::format("the arc ends at %.0f km, the Moon at %.0f km", far / 1000.0, moon_distance / 1000.0));
    CHECK(far > moon_distance * 0.8 && far < moon_distance * 1.3);

    const auto burns = sim.maneuvers();
    REQUIRE(burns.size() == 2);
    CHECK(burns[0].located && burns[1].located);
    CHECK_NEAR_ABS((v(burns[0].position) - centre).norm() / scale, 6.771e6, 1.0e5,
                   "the injection lights in the parking orbit");
    CHECK((v(burns[1].position) - centre).norm() / scale > moon_distance * 0.8);   // the capture lights at the Moon
    CHECK(burns[0].seconds_to_ignition > 0.0);

    sim.clear_plan();
    CHECK(!sim.has_plan());   // and the plan can be abandoned (rule 68)
}

TEST(the_jets_light_what_the_actuator_lit_rule_15) {
    auto& sim = session();
    const auto thrusters = sim.rcs_thrusters();
    CHECK_EQ(thrusters.size(), std::size_t{12});
    for (const auto& spec : thrusters) {
        CHECK_NEAR_ABS(spec.position.norm(), 2.0, 1.0e-6, "sits on the 2 m arm the core uses");
        CHECK_NEAR_ABS(spec.force_direction.norm(), 1.0, 1.0e-6, "pushes along a unit vector");
    }

    app::MeshLibrary meshes;
    app::RcsVisual visual(meshes);
    visual.build(thrusters);

    // No command, no flame. The case an implementation driven by the key would
    // get right by accident, here to pin the other side.
    sim.set_manual_torque(Vec3{});
    sim.set_manual_translation(Vec3{});
    sim.set_pointing_mode("");
    sim.advance(0.016);
    visual.set_throttles(sim.rcs_throttles());
    CHECK_EQ(visual.firing_count(), 0);
    CHECK(visual.parts().empty());

    // A pure torque about +x: the allocator opens the two nozzles of the +x couple
    // and no others. Two, not four and not twelve.
    sim.set_manual_torque(Vec3{400.0, 0.0, 0.0});
    sim.advance(0.016);
    const auto pure = sim.rcs_throttles();
    visual.set_throttles(pure);
    CHECK_EQ(visual.firing_count(), 2);
    CHECK(pure[0] > 0.0 && pure[1] > 0.0);   // the +x pair the core declares first
    CHECK_EQ(visual.parts().size(), std::size_t{2});

    // A diagonal torque opens more nozzles, at DIFFERENT fractions. This is the
    // line a visual driven by the key cannot reproduce.
    sim.set_manual_torque(Vec3{400.0, 400.0, 0.0});
    sim.advance(0.016);
    visual.set_throttles(sim.rcs_throttles());
    CHECK(visual.firing_count() > 2);
    CHECK(visual.total_demand() > 0.0);

    // Translation: force without torque. The couple layout allows it, and it is
    // what rule 13's translation keys command.
    sim.set_manual_torque(Vec3{});
    sim.set_manual_translation(Vec3{0.0, 0.0, 360.0});
    sim.advance(0.016);
    visual.set_throttles(sim.rcs_throttles());
    CHECK(visual.firing_count() >= 2);
    sim.set_manual_translation(Vec3{});

    // Each jet grows ALONG ITS EXHAUST, out of the nozzle: the jet of a thruster
    // that pushes the ship +z sits on the -z side of its nozzle.
    visual.set_throttles(std::vector<double>(12, 1.0));
    const auto jets = visual.parts();
    REQUIRE(jets.size() == 12);
    for (std::size_t i = 0; i < 12; ++i) {
        const Vec3 exhaust = thrusters[i].force_direction * -1.0;
        const Vec3 along = jets[i].transform.origin - thrusters[i].position;
        CHECK(dot(along, exhaust) > 0.0);
        const Vec3 grows = jets[i].transform.basis.y.normalized();
        CHECK_NEAR_ABS(dot(grows, exhaust), 1.0, 1.0e-9, "the jet's long axis is the exhaust direction");
    }
}

TEST(the_plume_follows_the_thrust_not_the_key_rule_16) {
    app::MeshLibrary meshes;
    app::EnginePlume plume(meshes);
    plume.set_thrust(0.0);
    CHECK(!plume.visible());
    CHECK(plume.parts().empty());
    plume.set_thrust(200000.0);
    CHECK(plume.visible());
    const double full = plume.intensity();
    plume.set_thrust(50000.0);
    CHECK(plume.intensity() < full);   // a quarter of the thrust is a smaller plume

    // The GEOMETRY, not just visibility. A node's scale is applied BEFORE its
    // rotation: the version that stretched x of a mesh that grows along +y drew a
    // disc 26 m WIDE by 2.6 m long, and the demonstration photograph came out
    // with no plume at 200 kN. What decides is the transformed bounding box,
    // which is what the renderer draws.
    plume.set_thrust(200000.0);
    const auto box = app::mesh::transformed_bounds(*meshes.find(plume.cone_mesh()), plume.cone_transform());
    CHECK_NEAR_ABS(-box.min.x, app::EnginePlume::MAX_LENGTH, 0.6, "the full plume extends 14 m BEHIND the nozzle");
    CHECK(box.max.x <= 0.3);   // and starts at the nozzle, not in front of it
    CHECK(std::max(box.size().y, box.size().z) < box.size().x * 0.5);   // a plume, not a disc
    // The case that separates the thrust from the key: full throttle, empty tank.
    plume.set_thrust(0.0);
    CHECK(!plume.visible());
    CHECK_EQ(plume.light().energy, 0.0);
}

TEST(camera_modes_rule_11) {
    app::CameraRig rig(1.0e-6);
    CHECK(rig.is_cockpit());   // a new flight starts in the cockpit
    std::set<std::string> seen;
    for (std::size_t i = 0; i < app::CameraRig::MODE_NAMES.size(); ++i) {
        seen.insert(rig.mode_name());
        rig.cycle_mode();
    }
    CHECK_EQ(seen.size(), app::CameraRig::MODE_NAMES.size());   // cycling reaches every mode
    CHECK(rig.is_cockpit());                                    // and comes back round

    rig.set_mode(app::CameraRig::Mode::ExternalOrbit);
    CHECK(rig.at_preset());   // entering a mode puts the camera at its preset
    rig.orbit_azimuth += 0.6;
    CHECK(!rig.at_preset());  // and the read-out stops claiming the preset
    rig.recentre();
    CHECK(rig.at_preset());   // recentre brings it back

    // Focus on a body: the camera has to be OUTSIDE it. The first version built
    // its frame with a fixed -Z and the J2000 pole as "up", whose cross product is
    // the null vector -- the camera sat at the planet's centre and the screen was
    // black, with no error at all. A distance of zero is its signature.
    rig.focus_index = 0;
    rig.update(Vec3{6.771, 0.0, 0.0}, app::Basis{}, Vec3{0.0, 1.0e-4, 0.0}, Vec3{}, Vec3{}, 19.1);
    const double distance = rig.world_camera().position.norm();
    INFO(app::fmt::format("focusing a body puts the camera outside it (%.2f units)", distance));
    CHECK(distance > 1.0);
    CHECK(rig.world_camera().basis.z.norm() > 0.5);
    rig.focus_index = -1;

    const double before = rig.orbit_zoom;
    rig.zoom(1.0);
    CHECK(rig.orbit_zoom > before);   // the wheel zooms out
    for (int i = 0; i < 80; ++i) {
        rig.zoom(1.0);
    }
    CHECK(rig.orbit_zoom <= app::CameraRig::ZOOM_MAX);   // zoom is bounded

    // The two cameras are ONE camera in two units: same orientation, and the
    // world position is the ship plus the eye scaled.
    rig.set_mode(app::CameraRig::Mode::Cockpit);
    const Vec3 ship{10.0, 20.0, 30.0};
    rig.update(ship, app::Basis{}, Vec3{0.0, 1.0e-4, 0.0}, Vec3{}, ship, 1.0);
    const auto& world = rig.world_camera();
    const auto& near = rig.near_camera();
    CHECK_NEAR_ABS((world.position - (ship + near.position * 1.0e-6)).norm(), 0.0, 1.0e-12,
                   "the only conversion between the two scales");
    CHECK_NEAR_ABS((world.basis.z - near.basis.z).norm(), 0.0, 1.0e-15, "one orientation");
}

TEST(clickable_cockpit_controls_rules_23_24) {
    int pressed = 0;
    auto button = app::CockpitControl::button("TEST", Vec3{}, 0.24, [&](app::CockpitControl&) { ++pressed; });
    // A ray from -z, facing the face: hits.
    CHECK(button.hit_test(Vec3{0.0, 0.0, -1.0}, Vec3{0.0, 0.0, 1.0}));
    // Past the edge: misses. 0.13 > half the width, 0.12.
    CHECK(!button.hit_test(Vec3{0.13, 0.0, -1.0}, Vec3{0.0, 0.0, 1.0}));
    // Behind the panel, pointing away: misses. Without this, clicking the sky in
    // front would light a button behind the pilot's back.
    CHECK(!button.hit_test(Vec3{0.0, 0.0, 1.0}, Vec3{0.0, 0.0, 1.0}));
    // Parallel to the panel: misses, and does not divide by zero.
    CHECK(!button.hit_test(Vec3{0.0, 0.0, -1.0}, Vec3{1.0, 0.0, 0.0}));

    button.press();
    CHECK_EQ(pressed, 1);
    auto toggle = app::CockpitControl::toggle("RCS", Vec3{}, 0.24, false, [](app::CockpitControl&) {});
    toggle.press();
    CHECK(toggle.on);    // a switch toggles before it calls back
    toggle.press();
    CHECK(!toggle.on);   // and toggles back

    // The panel itself: every one of the seven keys can be hit by a ray from the
    // pilot's eye through its centre, and the ray hits THAT key.
    app::MeshLibrary meshes;
    app::ShipMaterials materials;
    app::CockpitInterior cockpit(meshes, materials);
    for (std::size_t i = 0; i < 7; ++i) {
        cockpit.configure_button(i, "K", {});
    }
    for (std::size_t i = 0; i < 7; ++i) {
        const Vec3 target = cockpit.controls()[i].transform.origin;
        const Vec3 direction = (target - app::CockpitInterior::EYE).normalized();
        CHECK_EQ(cockpit.pick(app::CockpitInterior::EYE, direction), static_cast<int>(i));
    }
    // Lamps are not buttons.
    const Vec3 lamp = cockpit.controls()[7].transform.origin;
    CHECK_EQ(cockpit.pick(app::CockpitInterior::EYE, (lamp - app::CockpitInterior::EYE).normalized()), -1);
}

TEST(instruments_draw_without_a_display_rule_62) {
    // An instrument with no data at all must not break: it is the state it is in
    // on the first frame, before the first snapshot arrives.
    auto& sim = session();
    app::InstrumentData empty{};
    app::InstrumentData full{};
    full.present = true;
    full.s = sim.snapshot();
    full.directions = sim.flight_directions();
    const auto axes = sim.spacecraft_axes();
    full.ship_basis = app::Basis{axes.x, axes.y, axes.z};
    full.orbit_track = widen_all(sim.orbit_track(64));

    app::FlightDisplay flight;
    app::NavDisplay nav;
    app::TargetDisplay target;
    app::SystemDisplay system;
    app::MinimalHud hud;
    app::OrbitMap map;
    std::vector<app::Instrument*> instruments{&flight, &nav, &target, &system, &hud, &map};
    for (auto* instrument : instruments) {
        app::RecordingCanvas canvas;
        instrument->draw(canvas, app::Vec2{480.0, 320.0}, empty);
        instrument->draw(canvas, app::Vec2{480.0, 320.0}, full);
        CHECK(!canvas.rects.empty() || !canvas.texts.empty());
    }

    // And what they draw is what the snapshot says, in the pilot's units.
    app::RecordingCanvas canvas;
    target.draw(canvas, app::Vec2{480.0, 315.0}, full);
    CHECK(canvas.contains_text("MOON"));
    CHECK(canvas.contains_text(app::fmt::distance(full.s.target_distance_m)));
    app::RecordingCanvas strip;
    system.draw(strip, app::Vec2{1280.0, 122.0}, full);
    CHECK(strip.contains_text(app::fmt::mass(full.s.propellant_kg)));
    CHECK(strip.contains_text("RCS ON"));
}
