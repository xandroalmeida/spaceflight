# spaceflight_core -- the scientific core.
#
# Hard rule: this target must never gain a dependency on Godot, on a windowing
# system, or on anything that cannot run in a headless CI job.  CSPICE is the
# only external dependency, and only core/ephemeris/*.cpp may include it.

add_library(spaceflight_core STATIC
    math/vec3.cpp
    time/coordinate_time.cpp
    celestial/body_id.cpp
    celestial/body_catalog.cpp
    ephemeris/spice_internal.cpp
    ephemeris/spice_kernel_set.cpp
    ephemeris/spice_ephemeris_provider.cpp
    ephemeris/spice_time_converter.cpp
    gravity/point_mass_gravity.cpp
    gravity/composite_force_model.cpp
    propagation/dormand_prince_54.cpp
    propagation/propagator_report.cpp
    trajectory/orbital_elements.cpp
    simulation/simulation_clock.cpp
)

add_library(spaceflight::core ALIAS spaceflight_core)

# Headers are included as "core/<module>/<file>.hpp" from the project root: the
# path in the #include line then matches the architecture diagram exactly.
target_include_directories(spaceflight_core PUBLIC "${CMAKE_SOURCE_DIR}")

target_link_libraries(spaceflight_core
    PUBLIC  spaceflight::warnings
    PRIVATE spaceflight::cspice)

target_compile_definitions(spaceflight_core PRIVATE
    SPACEFLIGHT_DEFAULT_KERNEL_DIR="${SPACEFLIGHT_DEFAULT_KERNEL_DIR}")

set_target_properties(spaceflight_core PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF)
