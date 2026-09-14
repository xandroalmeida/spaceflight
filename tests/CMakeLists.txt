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
spaceflight_add_test(scientific full_mission)
set_tests_properties(scientific.full_mission PROPERTIES TIMEOUT 2400)

# The equivalence test compiles the GDExtension's planner BRIDGE into itself.
#
# That is possible because the bridge header has no Godot in it, and it is the
# whole point: the test exercises the code the engine actually calls rather than
# a copy of it that happens to agree.  Nothing else in tests/ links anything from
# godot/, and nothing else should.
spaceflight_add_test(scientific planner_equivalence)
target_sources(test_planner_equivalence PRIVATE
    "${CMAKE_SOURCE_DIR}/godot/gdextension/src/mission_planner.cpp")
# Flies the full production search twice, once per caller.
set_tests_properties(scientific.planner_equivalence PROPERTIES TIMEOUT 600)
spaceflight_add_test(scientific blackbody_colour)
spaceflight_add_test(scientific terrell_rotation)
spaceflight_add_test(scientific relativistic_sky)
spaceflight_add_test(scientific verification_hardening)

spaceflight_add_test(regression reference_states)

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
#   cmake --build build --target gpu-validation    needs a real framebuffer
#
# The GPU tests SKIP (exit 77) rather than fail when Godot, the GDExtension or a
# display is missing, which is the same policy the kernel-dependent tests use:
# a green suite that verified nothing is worse than a red one.
# ---------------------------------------------------------------------------

set(SPACEFLIGHT_HEADLESS_LABELS "unit|integration|scientific|regression")

add_custom_target(ctest-headless
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
            --label-regex "${SPACEFLIGHT_HEADLESS_LABELS}"
            --output-on-failure
    COMMENT "The headless suite: arithmetic only, no claim about anything visible"
    USES_TERMINAL)

foreach(gpu_case starfield relativistic_visual)
    add_test(NAME "gpu.${gpu_case}"
             COMMAND "${CMAKE_SOURCE_DIR}/scripts/gpu_validation.sh" "${gpu_case}")
    set_tests_properties("gpu.${gpu_case}" PROPERTIES
        SKIP_RETURN_CODE 77
        LABELS "gpu"
        # Rendering a ladder of full-sky frames and reading the framebuffer back
        # is not a millisecond operation, and the default 1500 s is not enough on
        # an integrated GPU.
        TIMEOUT 1800
        ENVIRONMENT "SPACEFLIGHT_KERNEL_DIR=${SPACEFLIGHT_DEFAULT_KERNEL_DIR};SPACEFLIGHT_CATALOG_DIR=${SPACEFLIGHT_DEFAULT_CATALOG_DIR}")
endforeach()

# ---------------------------------------------------------------------------
# A terceira suíte: o Godot sem tela (Milestone 7, regra 62).
#
# Não é `unit` e não é `gpu`. Corre sem framebuffer, como a headless, mas o que
# ela verifica não é aritmética: é a CAMADA DE APRESENTAÇÃO -- que o snapshot
# chega ao cockpit com os campos que os instrumentos leem, que a seleção de alvo
# funciona, que os jatos acendem pelo acionamento e não pela tecla, que a câmera
# volta ao preset, que o mapa recebe pontos de trajetória que fecham na elipse
# que o snapshot reporta.
#
# Precisa do Godot e da GDExtension, e por isso pula (77) quando eles não
# existem, pela mesma política das outras duas: uma máquina que não pôde correr
# uma suíte não a reprovou.
#
#   cmake --build build --target godot-tests
# Uma entrada por milestone, e não uma só, porque um `godot.*` vermelho tem de
# dizer QUAL camada quebrou: a do cockpit do M7 ou a do sistema solar do M8.
foreach(godot_suite m7 m8)
    add_test(NAME "godot.${godot_suite}"
             COMMAND "${CMAKE_SOURCE_DIR}/scripts/godot_tests.sh" "${godot_suite}")
    set_tests_properties("godot.${godot_suite}" PROPERTIES
        SKIP_RETURN_CODE 77
        LABELS "godot"
        # O M8 inclui uma busca Terra->Marte cancelada a meio, que o worker leva
        # um voo a abandonar.
        TIMEOUT 600
        ENVIRONMENT "SPACEFLIGHT_KERNEL_DIR=${SPACEFLIGHT_DEFAULT_KERNEL_DIR};SPACEFLIGHT_CATALOG_DIR=${SPACEFLIGHT_DEFAULT_CATALOG_DIR}")
endforeach()

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

add_custom_target(godot-tests
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
            --label-regex "godot" --output-on-failure
    COMMENT "The presentation suite: Godot without a display"
    USES_TERMINAL)

add_custom_target(asset-validation
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
            --label-regex "assets" --output-on-failure
    COMMENT "The texture checks: no engine, no kernels, no display"
    USES_TERMINAL)

add_custom_target(gpu-validation
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
            --label-regex "gpu" --output-on-failure
    COMMENT "The GPU suite: needs a real framebuffer; qualified only on Apple M5 Pro / Metal"
    USES_TERMINAL)
