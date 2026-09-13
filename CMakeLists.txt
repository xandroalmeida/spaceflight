cmake_minimum_required(VERSION 3.24)

project(spaceflight
    VERSION 0.1.0
    DESCRIPTION "Scientific Solar System spaceflight simulator core"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# The whole project must build and test without the graphics engine.
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
# ---------------------------------------------------------------------------

option(SPACEFLIGHT_BUILD_TESTS   "Build the test suite"                      ON)
option(SPACEFLIGHT_BUILD_TOOLS   "Build command line tools (orbit-cli)"      ON)
option(SPACEFLIGHT_BUILD_GODOT   "Build the Godot GDExtension (Milestone 2)" OFF)
option(SPACEFLIGHT_AUTO_FETCH    "Download CSPICE at configure time if absent" ON)
option(SPACEFLIGHT_WERROR        "Treat warnings as errors in our own code"  OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set; defaulting to RelWithDebInfo")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Fast math would let the compiler reorder floating point operations and assume
# no NaN/Inf.  Both are unacceptable here: reproducibility and error control are
# the product.  Recorded as an explicit refusal, not an oversight.
if(NOT MSVC)
    add_compile_options(-fno-fast-math)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(cspice)
include(warnings)

# Where the kernels live by default; overridable at runtime by
# SPACEFLIGHT_KERNEL_DIR or --kernels.
set(SPACEFLIGHT_DEFAULT_KERNEL_DIR "${CMAKE_SOURCE_DIR}/kernels/spice"
    CACHE PATH "Default SPICE kernel directory compiled into the binaries")

add_subdirectory(core)

if(SPACEFLIGHT_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(SPACEFLIGHT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SPACEFLIGHT_BUILD_GODOT)
    add_subdirectory(godot/gdextension)
endif()

message(STATUS "")
message(STATUS "spaceflight ${PROJECT_VERSION}")
message(STATUS "  build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  tests        : ${SPACEFLIGHT_BUILD_TESTS}")
message(STATUS "  tools        : ${SPACEFLIGHT_BUILD_TOOLS}")
message(STATUS "  godot        : ${SPACEFLIGHT_BUILD_GODOT}")
message(STATUS "  kernels      : ${SPACEFLIGHT_DEFAULT_KERNEL_DIR}")
message(STATUS "")
