# Builds NAIF CSPICE (N0067) from source as a static library target `cspice`.
#
# CSPICE is f2c-generated C89.  It is third-party, unmodified, and compiled with
# warnings disabled: we do not own its style, and treating its output as our own
# code would drown the signal from `spaceflight_core`.
#
# The toolkit is *not* vendored in Git (42 MB).  scripts/fetch_cspice.sh downloads
# and extracts it into external/cspice.

set(CSPICE_ROOT "${CMAKE_SOURCE_DIR}/external/cspice" CACHE PATH
    "Root of an extracted NAIF CSPICE toolkit")

if(NOT EXISTS "${CSPICE_ROOT}/include/SpiceUsr.h")
    if(SPACEFLIGHT_AUTO_FETCH)
        message(STATUS "CSPICE not found at ${CSPICE_ROOT}; running scripts/fetch_cspice.sh")
        execute_process(
            COMMAND "${CMAKE_SOURCE_DIR}/scripts/fetch_cspice.sh"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _fetch_result)
        if(NOT _fetch_result EQUAL 0)
            message(FATAL_ERROR "fetch_cspice.sh failed (${_fetch_result}).")
        endif()
    else()
        message(FATAL_ERROR
            "CSPICE not found at ${CSPICE_ROOT}.\n"
            "Run scripts/fetch_cspice.sh, or configure with -DSPACEFLIGHT_AUTO_FETCH=ON.")
    endif()
endif()

file(GLOB CSPICE_SOURCES CONFIGURE_DEPENDS "${CSPICE_ROOT}/src/cspice/*.c")
list(LENGTH CSPICE_SOURCES _cspice_count)
if(_cspice_count LESS 1000)
    message(FATAL_ERROR "Only ${_cspice_count} CSPICE sources found in ${CSPICE_ROOT}/src/cspice; extraction looks incomplete.")
endif()

add_library(cspice STATIC ${CSPICE_SOURCES})
add_library(spaceflight::cspice ALIAS cspice)

target_include_directories(cspice SYSTEM PUBLIC "${CSPICE_ROOT}/include")
target_compile_definitions(cspice PRIVATE NON_UNIX_STDIO)
set_target_properties(cspice PROPERTIES
    C_STANDARD 99
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
    UNITY_BUILD OFF)

if(NOT MSVC)
    target_compile_options(cspice PRIVATE
        -w
        -Wno-error=implicit-function-declaration
        -Wno-error=implicit-int
        -Wno-error=int-conversion
        -Wno-error=incompatible-pointer-types
        -Wno-error=deprecated-non-prototype)
endif()

message(STATUS "CSPICE: ${_cspice_count} sources from ${CSPICE_ROOT}")
