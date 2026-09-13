# The GDExtension: the ONLY place where this project links against Godot.
#
# Disabled by default.  That is not caution, it is the test: `spaceflight_core`
# and its whole suite must build and pass with no engine present, which is the
# check that ADR-0002's separation is still real.
#
#   ./scripts/fetch_godot_cpp.sh
#   cmake -S . -B build -DSPACEFLIGHT_BUILD_GODOT=ON
#
# The godot-cpp version MUST match the editor that opens the project: the
# bindings are generated from the engine's extension_api.json.

set(GODOT_CPP_ROOT "${CMAKE_SOURCE_DIR}/external/godot-cpp" CACHE PATH
    "Root of an extracted godot-cpp tree")

if(NOT EXISTS "${GODOT_CPP_ROOT}/include/godot_cpp/godot.hpp")
    message(FATAL_ERROR
        "godot-cpp not found at ${GODOT_CPP_ROOT}.\n"
        "Run scripts/fetch_godot_cpp.sh, or turn the extension off with "
        "-DSPACEFLIGHT_BUILD_GODOT=OFF (the default).")
endif()

# godot-cpp compiles itself -- and everything that links it -- with
# -fno-exceptions, because the engine does.  The core reports kernel and
# numerical failures by throwing (ADR-0003), so the bridge has to be able to
# CATCH; with the flag inherited through godot-cpp's interface it wins on the
# command line no matter what this target asks for.  Turning the option off is
# the supported way to say "this extension uses exceptions internally".
#
# Nothing escapes into Godot's C ABI: every entry point goes through the
# `guarded` helper in src/simulation_node.cpp, which turns a throw into a logged
# error and a false return.
set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL
    "The bridge catches core exceptions; see godot/gdextension/CMakeLists.txt" FORCE)

add_subdirectory("${GODOT_CPP_ROOT}" "${CMAKE_BINARY_DIR}/godot-cpp" EXCLUDE_FROM_ALL)

# Third-party headers are SYSTEM headers: their warnings are not ours to fix, and
# ninety of them per translation unit is how a real warning goes unnoticed.
if(TARGET godot-cpp AND NOT CMAKE_VERSION VERSION_LESS 3.25)
    set_target_properties(godot-cpp PROPERTIES SYSTEM ON)
endif()

add_library(spaceflight_gdextension SHARED
    src/register_types.cpp
    src/simulation_node.cpp
)

target_include_directories(spaceflight_gdextension PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(spaceflight_gdextension PRIVATE spaceflight::core godot-cpp)

set_target_properties(spaceflight_gdextension PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
    PREFIX ""
    OUTPUT_NAME "spaceflight"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/godot/project/bin")

# The scientific core's warning set is stricter than godot-cpp's headers survive;
# the bridge is thin enough that this costs little, and everything it calls is
# compiled under the strict set.
#
# Exceptions: godot-cpp compiles itself with -fno-exceptions, because the engine
# does (GODOTCPP_DISABLE_EXCEPTIONS defaults to ON).  The core, however, reports
# kernel and numerical failures by throwing (ADR-0003), so the bridge has to be
# able to CATCH.  We re-enable them for this target only, which is safe because
# there is never a godot-cpp frame between the throw and the catch: the call goes
# bridge -> core -> throw -> unwinds back into the bridge's own try block.
# Nothing escapes into Godot's C ABI; see the `guarded` helper in
# src/simulation_node.cpp.
if(NOT MSVC)
    target_compile_options(spaceflight_gdextension PRIVATE -Wall -Wextra -fexceptions)
else()
    target_compile_options(spaceflight_gdextension PRIVATE /EHsc)
endif()

message(STATUS "GDExtension: godot-cpp at ${GODOT_CPP_ROOT}")
