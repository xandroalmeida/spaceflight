# Four categories, four questions (docs/architecture/system-architecture.md s.9):
#   unit         is the arithmetic right?
#   integration  do the pieces talk to each other?
#   scientific   does the answer match nature / an external reference?
#   regression   does today's answer still match yesterday's?

add_library(test_support STATIC
    support/test_harness.cpp
)
target_include_directories(test_support PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(test_support PUBLIC spaceflight::core spaceflight::warnings)

# Tests that need kernels exit 77 when they cannot find them; CTest then reports
# them as Skipped instead of quietly passing.
function(spaceflight_add_test CATEGORY NAME)
    set(target "test_${NAME}")
    add_executable(${target} "${CATEGORY}/${target}.cpp")
    target_link_libraries(${target} PRIVATE test_support)
    add_test(NAME "${CATEGORY}.${NAME}" COMMAND ${target})
    set_tests_properties("${CATEGORY}.${NAME}" PROPERTIES
        SKIP_RETURN_CODE 77
        LABELS "${CATEGORY}"
        ENVIRONMENT "SPACEFLIGHT_KERNEL_DIR=${SPACEFLIGHT_DEFAULT_KERNEL_DIR}")
endfunction()

spaceflight_add_test(unit vec3)
spaceflight_add_test(unit coordinate_time)
spaceflight_add_test(unit orbital_elements)
spaceflight_add_test(unit simulation_clock)
spaceflight_add_test(unit engine)
spaceflight_add_test(unit maneuver_plan)
spaceflight_add_test(unit targeting)
spaceflight_add_test(unit render_transform)
spaceflight_add_test(unit quaternion)

spaceflight_add_test(integration spice_kernels)
spaceflight_add_test(integration time_conversion)
spaceflight_add_test(integration snapshot)

spaceflight_add_test(scientific spice_positions)
spaceflight_add_test(scientific two_body)
spaceflight_add_test(scientific integrator_convergence)
spaceflight_add_test(scientific solar_system_gravity)
spaceflight_add_test(scientific j2_oblateness)
spaceflight_add_test(scientific dense_output)
spaceflight_add_test(scientific propulsion)
spaceflight_add_test(scientific lambert)
spaceflight_add_test(scientific attitude)
spaceflight_add_test(scientific relativistic_propulsion)

spaceflight_add_test(regression reference_states)
