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

# A handful of invariants are about the SHAPE of the source rather than about a
# number -- "the GDExtension does not solve orbits" is the one this milestone
# added (tests/support/source_fixture.hpp).  Those tests read the repository, and
# a test's working directory is wherever ctest was invoked from, so the root is
# compiled in.
target_compile_definitions(test_support PUBLIC
    SPACEFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

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
        ENVIRONMENT "SPACEFLIGHT_KERNEL_DIR=${SPACEFLIGHT_DEFAULT_KERNEL_DIR};SPACEFLIGHT_CATALOG_DIR=${SPACEFLIGHT_DEFAULT_CATALOG_DIR}")
endfunction()

spaceflight_add_test(unit vec3)
spaceflight_add_test(unit coordinate_time)
spaceflight_add_test(unit orbital_elements)
spaceflight_add_test(unit simulation_clock)
spaceflight_add_test(unit engine)
spaceflight_add_test(unit maneuver_plan)
spaceflight_add_test(unit targeting)
spaceflight_add_test(unit departure_hyperbola)
spaceflight_add_test(unit render_transform)
spaceflight_add_test(unit quaternion)
spaceflight_add_test(unit star_catalog)

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
spaceflight_add_test(scientific relativistic_optics)
spaceflight_add_test(scientific relativistic_gravity)
spaceflight_add_test(scientific spin_transport)
spaceflight_add_test(scientific b_plane)
spaceflight_add_test(scientific lunar_transfer)

# The golden mission flies the whole thing under the AUTOPILOT model, which puts
# the attitude controller and twelve RCS thrusters inside the map the two
# differential correctors invert.  That costs minutes where the same trajectory
# under ideal guidance costs seconds, and it is the cost of testing what the
# scene can actually fly rather than a cheaper thing next to it.
#
# The timeout is there to catch a hang, not to grade the hardware.  The run is
# single-threaded and almost all of it is CSPICE lookups: 853 s on an Apple M5
# Pro, 9249 s (2 h 34 min, 99 % user time) on an Intel i5-3210M from 2012 --
# both passing.  Three hours covers the slow end with margin; override with
# -DSPACEFLIGHT_FULL_MISSION_TIMEOUT=<seconds>.
set(SPACEFLIGHT_FULL_MISSION_TIMEOUT 10800 CACHE STRING
    "CTest timeout for scientific.full_mission, in seconds")
spaceflight_add_test(scientific full_mission)
set_tests_properties(scientific.full_mission PROPERTIES
    TIMEOUT ${SPACEFLIGHT_FULL_MISSION_TIMEOUT})

# The equivalence test compiles the cockpit's planner BRIDGE into itself.
#
# That is possible because the bridge has nothing but core types in it, and it
# is the whole point: the test exercises the code the cockpit actually calls
# rather than a copy of it that happens to agree.  It compiles the one source
# file and not the application library, so the scientific suite still builds
# with -DSPACEFLIGHT_BUILD_APP=OFF.
spaceflight_add_test(scientific planner_equivalence)
target_sources(test_planner_equivalence PRIVATE
    "${CMAKE_SOURCE_DIR}/app/session/transfer_bridge.cpp")
# Flies the full production search twice, once per caller.
set_tests_properties(scientific.planner_equivalence PROPERTIES TIMEOUT 600)
spaceflight_add_test(scientific blackbody_colour)
spaceflight_add_test(scientific terrell_rotation)
spaceflight_add_test(scientific relativistic_sky)
spaceflight_add_test(scientific verification_hardening)

spaceflight_add_test(regression reference_states)

# ---------------------------------------------------------------------------
# The presentation (ADR-0009): the cockpit, the instruments, the cameras, the
# panels and the orchestrator, exercised with no window and no GPU.
#
# Not `unit` and not `gpu`. It runs without a framebuffer, like the headless
# suite, but what it checks is not arithmetic: it is the PRESENTATION LAYER --
# that the snapshot reaches the cockpit with the fields the instruments read,
# that target selection works, that the jets light by actuation and not by the
# key, that the camera comes back to its preset, that the map receives
# trajectory points that close on the ellipse the snapshot reports. It replaced
# the Godot suites godot.m7 and godot.m8, check for check, when the scene left
# Godot.
# ---------------------------------------------------------------------------
if(SPACEFLIGHT_BUILD_APP)
    function(spaceflight_add_presentation_test NAME)
        set(target "test_presentation_${NAME}")
        add_executable(${target} "presentation/${target}.cpp")
        target_link_libraries(${target} PRIVATE test_support spaceflight_presentation spaceflight_stb)
        add_test(NAME "presentation.${NAME}" COMMAND ${target})
        set_tests_properties("presentation.${NAME}" PROPERTIES
            SKIP_RETURN_CODE 77
            LABELS "presentation"
            TIMEOUT 1200
            ENVIRONMENT "SPACEFLIGHT_KERNEL_DIR=${SPACEFLIGHT_DEFAULT_KERNEL_DIR};SPACEFLIGHT_CATALOG_DIR=${SPACEFLIGHT_DEFAULT_CATALOG_DIR}")
    endfunction()
    spaceflight_add_presentation_test(m7)
    spaceflight_add_presentation_test(m8)
    spaceflight_add_presentation_test(flight)
endif()

# ---------------------------------------------------------------------------
# Headless versus GPU (Milestone 6.2 section 22).
#
# Everything above runs with no display, no driver and no window, and every one
# of those tests is about ARITHMETIC.  None of them can tell you whether anything
# is visible, and that distinction is not academic: Milestone 6 shipped a
# starfield of 8 786 loaded stars, none of which could be seen, through a suite
# that was entirely green.  Two independent defects -- `render_mode unshaded`
# discarding EMISSION on the Forward+ renderer, and a sky sphere sitting below one
# quantisation step of a 24-bit depth buffer -- are invisible to arithmetic by
# construction.
#
# So the two suites are separated and LABELLED, and the labels are the point: a
# CI job that runs `ctest` and reports "all tests passed" must not be readable as
# a statement about the graphics.  It is not one.
#
#   cmake --build build --target ctest-headless    no display needed, runs anywhere
#   cmake --build build --target gpu-validation    needs a GPU (off-screen)
#
# The GPU tests SKIP (exit 77) rather than fail when there is no GPU device to
# run them on, which is the same policy the kernel-dependent tests use: a green
# suite that verified nothing is worse than a red one.
#
# They run OFF-SCREEN (ADR-0009): SDL_GPU renders into a texture and the
# harness reads it back, so "needs a screen" became "needs a GPU driver" --
# Metal, Vulkan or Direct3D 12. A CI machine without one skips them.
# ---------------------------------------------------------------------------

set(SPACEFLIGHT_HEADLESS_LABELS "unit|integration|scientific|regression|presentation")

add_custom_target(ctest-headless
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
            --label-regex "${SPACEFLIGHT_HEADLESS_LABELS}"
            --output-on-failure
    COMMENT "The headless suite: arithmetic only, no claim about anything visible"
    USES_TERMINAL)

if(SPACEFLIGHT_BUILD_APP)
    # The starfield harness (docs/validation/starfield-debug.md): ten stages, each
    # measuring the renderer's own frame against core/.
    add_executable(spaceflight_starfield_validation gpu/starfield_validation.cpp)
    target_link_libraries(spaceflight_starfield_validation PRIVATE spaceflight_gfx spaceflight::warnings)
    target_compile_definitions(spaceflight_starfield_validation PRIVATE
        SPACEFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    set(gpu_environment
        "SPACEFLIGHT_KERNEL_DIR=${SPACEFLIGHT_DEFAULT_KERNEL_DIR};SPACEFLIGHT_CATALOG_DIR=${SPACEFLIGHT_DEFAULT_CATALOG_DIR};SPACEFLIGHT_BIN_DIR=$<TARGET_FILE_DIR:spaceflight>")
    foreach(gpu_case starfield relativistic_visual)
        add_test(NAME "gpu.${gpu_case}"
                 COMMAND "${CMAKE_SOURCE_DIR}/scripts/gpu_validation.sh" "${gpu_case}")
        set_tests_properties("gpu.${gpu_case}" PROPERTIES
            SKIP_RETURN_CODE 77
            LABELS "gpu"
            TIMEOUT 1800
            ENVIRONMENT "${gpu_environment}")
    endforeach()
endif()

# ---------------------------------------------------------------------------
# As texturas, conferidas antes de serem ligadas (Milestone 7, regras 40 e 47).
#
# Não precisa de Godot, de kernels nem de tela: decodifica os PNG em Python puro
# e mede o que os prompts de docs/assets/ prometem -- proporção, costura entre as
# bordas, uniformidade dos polos, convenção de normal map, e o registro entre o
# albedo da Terra e as suas luzes noturnas.
#
# Foi ele que reprovou o `moon_normal` entregue: uma imagem que parecia um normal
# map, registrava com o albedo, e cujos canais R e G seguiam o BRILHO em vez da
# inclinação. Sem tela nem aritmética de órbita se podia ver isso; só medindo.
#
# Pula (77) quando não há texturas, que é o estado de um clone antes de alguém
# gerar os assets -- e não é uma falha.
add_test(NAME "assets.textures"
         COMMAND "${CMAKE_COMMAND}" -E env python3
                 "${CMAKE_SOURCE_DIR}/scripts/validate_textures.py")
set_tests_properties("assets.textures" PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS "assets"
    TIMEOUT 600)

add_custom_target(asset-validation
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
            --label-regex "assets" --output-on-failure
    COMMENT "The texture checks: no engine, no kernels, no display"
    USES_TERMINAL)

add_custom_target(gpu-validation
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
            --label-regex "gpu" --output-on-failure
    COMMENT "The GPU suite: needs a GPU driver (Metal / Vulkan / D3D12), no display"
    USES_TERMINAL)
