# spaceflight_core -- the scientific core.
#
# Hard rule: this target must never gain a dependency on Godot, on a windowing
# system, or on anything that cannot run in a headless CI job.  CSPICE is the
# only external dependency, and only core/ephemeris/*.cpp may include it.

add_library(spaceflight_core STATIC
    config/json.cpp
    math/quaternion.cpp
    math/vec3.cpp
    time/coordinate_time.cpp
    celestial/body_id.cpp
    attitude/inertia.cpp
    attitude/pointing_controller.cpp
    attitude/rcs.cpp
    celestial/body_catalog.cpp
    celestial/oblateness.cpp
    ephemeris/spice_internal.cpp
    ephemeris/spice_kernel_set.cpp
    ephemeris/spice_ephemeris_provider.cpp
    ephemeris/spice_time_converter.cpp
    gravity/point_mass_gravity.cpp
    gravity/weak_field_metric.cpp
    gravity/composite_force_model.cpp
    gravity/oblateness_gravity.cpp
    relativity/light_time.cpp
    propulsion/engine.cpp
    propulsion/main_engine_force.cpp
    spacecraft/spacecraft.cpp
    navigation/maneuver.cpp
    navigation/maneuver_executor.cpp
    navigation/mission.cpp
    navigation/targeting.cpp
    navigation/trajectory_planner.cpp
    propagation/dense_output.cpp
    propagation/dormand_prince_54.cpp
    propagation/propagator_report.cpp
    trajectory/lambert.cpp
    trajectory/orbital_elements.cpp
    render/render_transform.cpp
    render/blackbody.cpp
    render/star_catalog.cpp
    render/relativistic_sky.cpp
    simulation/simulation_clock.cpp
    simulation/snapshot.cpp
)

add_library(spaceflight::core ALIAS spaceflight_core)

# Headers are included as "core/<module>/<file>.hpp" from the project root: the
# path in the #include line then matches the architecture diagram exactly.
target_include_directories(spaceflight_core PUBLIC "${CMAKE_SOURCE_DIR}")

# The warning set is PRIVATE: it governs how the core compiles ITSELF.  Making it
# PUBLIC would push -Wold-style-cast and friends onto every consumer, including
# the GDExtension, where godot-cpp's generated headers cannot survive it -- and a
# third-party header drowning the log is how real warnings get ignored.
target_link_libraries(spaceflight_core
    PRIVATE spaceflight::warnings
    PRIVATE spaceflight::cspice)

target_compile_definitions(spaceflight_core PRIVATE
    SPACEFLIGHT_DEFAULT_KERNEL_DIR="${SPACEFLIGHT_DEFAULT_KERNEL_DIR}"
    SPACEFLIGHT_DEFAULT_CATALOG_DIR="${SPACEFLIGHT_DEFAULT_CATALOG_DIR}")

set_target_properties(spaceflight_core PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF)
